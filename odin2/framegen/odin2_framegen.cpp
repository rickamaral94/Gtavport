// Gtavport — geração de quadros 2x (odin2_framegen.h). Do TOTKRecompAndroid (runtime/nvn/vulkan/framegen.c, MIT,
// Peiikai), sobre o plume: o framegen do lsfg-vk-android (liblsfg_framegen.so, aberta com dlopen) recebe o nosso
// VkDevice e a nossa fila e grava no modo "imagens" (sem semáforos: a ordem vem da fila, as barreiras são nossas).
#include "odin2_framegen.h"

#include <plume_vulkan.h>

#include <rex/cvar.h>
#include <rex/logging.h>

#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <vector>

REXCVAR_DEFINE_BOOL(odin2_framegen, false, "Gtavport",
                    "Geração de quadros 2x (LSFG 3.1, shaders do Lossless.dll do usuário): 30 reais -> 60 na tela");
REXCVAR_DEFINE_DOUBLE(odin2_fg_flow, 0.5, "Gtavport", "Geração de quadros: escala do fluxo (0.25..1; menor = mais leve)");
REXCVAR_DEFINE_BOOL(odin2_fg_performance, false, "Gtavport", "Geração de quadros: LSFG 3.1P (shaders mais leves)");

namespace gtav::native {
namespace {

// a interface C da liblsfg_framegen.so (odin2/framegen/lsfg_framegen_c.h)
using InitFn = int (*)(PFN_vkGetInstanceProcAddr, VkInstance, VkPhysicalDevice, VkDevice, uint32_t, VkQueue, float, int, int,
                       const char*, char*, size_t);
using CreateFn = int (*)(VkImage, VkImage, const VkImage*, int, uint32_t, uint32_t, VkFormat, int32_t*, char*, size_t);
using PresentFn = int (*)(int32_t, char*, size_t);
using DeleteFn = int (*)(int32_t, char*, size_t);
using FinalizeFn = void (*)();
constexpr int kDeviceLost = 2;  // LSFGFG_DEVICE_LOST

constexpr plume::RenderFormat kFormat = plume::RenderFormat::R8G8B8A8_UNORM;  // (storage + o que a janela recebe)

struct Image {
  std::unique_ptr<plume::RenderTexture> tex;
  std::unique_ptr<plume::RenderTextureView> view;
  std::unique_ptr<plume::RenderFramebuffer> fb;
  uint32_t descriptor = 0;
  VkImage vk() const { return tex ? static_cast<plume::VulkanTexture*>(tex.get())->vk : VK_NULL_HANDLE; }
};

std::string LsfgDir() {
  const char* root = std::getenv("GTAVPORT_LSFG_DIR");
  return root && *root ? std::string(root) + "/lsfg/spirv" : std::string();
}

// Um conjunto completo instalado pelo app (lsfg_extract.cpp: MANIFEST com "complete", extrator 1).
bool Installed(std::string* why) {
  std::string dir = LsfgDir();
  if (dir.empty()) return *why = "GTAVPORT_LSFG_DIR não definido", false;
  FILE* f = std::fopen((dir + "/MANIFEST").c_str(), "re");
  if (!f) return *why = "sem shaders: importe o Lossless.dll em Configurações > Geração de quadros", false;
  char line[512];
  bool complete = false, version = false;
  while (std::fgets(line, sizeof line, f)) {
    line[std::strcspn(line, "\n")] = 0;
    if (!std::strcmp(line, "complete")) complete = true;
    else if (!std::strncmp(line, "extractor ", 10)) version = std::atoi(line + 10) == 1;
  }
  std::fclose(f);
  if (!complete || !version) return *why = "shaders incompletos ou de outra versão: importe de novo", false;
  return true;
}

}  // namespace

struct Framegen::Impl {
  FramegenHost host;
  // biblioteca
  void* lib = nullptr;
  InitFn init = nullptr;
  CreateFn create = nullptr;
  PresentFn present = nullptr;
  DeleteFn del = nullptr;
  FinalizeFn finalize = nullptr;
  // estado (thread de render)
  Image in[2], out;
  uint32_t w = 0, h = 0;
  uint64_t count = 0;        // quadros reais dados ao framegen desde o contexto (o de número k na entrada k % 2)
  bool have_prev = false;    // a outra entrada tem o quadro real anterior
  bool layouts_ready = false;
  std::chrono::steady_clock::time_point last_real{};
  float flow = 0.5f;
  bool perf = false;
  // pedidos da thread de render para a de submissão
  std::atomic<uint32_t> want_gen{1};  // muda: (re)fazer a biblioteca e o contexto antes do próximo quadro
  std::mutex retired_mutex;
  std::vector<Image> retired;          // imagens antigas: destruídas na thread de submissão, depois do contexto
  // estado da thread de submissão
  uint32_t lib_gen = 0;
  bool lib_on = false, ctx_on = false;
  int32_t ctx = 0;
  std::atomic<bool> failed{false}, ok{false};
  std::string why;
  mutable std::mutex why_mutex;
  // contagem para o log
  uint64_t st_real = 0, st_gen = 0;
  std::chrono::steady_clock::time_point st_since = std::chrono::steady_clock::now();

  void Fail(const std::string& reason) {
    {
      std::lock_guard l(why_mutex);
      why = reason;
    }
    failed = true;
    REXLOG_WARN("[fg] geração de quadros parada nesta sessão: {}", reason);
  }

  bool LoadLib() {
    if (lib) return true;
    lib = dlopen("liblsfg_framegen.so", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return Fail(std::string("liblsfg_framegen.so: ") + dlerror()), false;
    init = reinterpret_cast<InitFn>(dlsym(lib, "lsfgfg_init"));
    create = reinterpret_cast<CreateFn>(dlsym(lib, "lsfgfg_create"));
    present = reinterpret_cast<PresentFn>(dlsym(lib, "lsfgfg_present"));
    del = reinterpret_cast<DeleteFn>(dlsym(lib, "lsfgfg_delete"));
    finalize = reinterpret_cast<FinalizeFn>(dlsym(lib, "lsfgfg_finalize"));
    if (!init || !create || !present || !del || !finalize) return Fail("liblsfg_framegen.so sem as funções lsfgfg_*"), false;
    return true;
  }

  bool MakeImage(Image& m) {
    auto* dev = host.device;
    m.tex = dev->createTexture(plume::RenderTextureDesc::ColorTarget(w, h, kFormat, plume::RenderMultisampling(), nullptr,
                                                                     plume::RenderTextureFlag::STORAGE));
    if (!m.tex) return false;
    m.view = m.tex->createTextureView(plume::RenderTextureViewDesc::Texture2D(kFormat));
    const plume::RenderTexture* color = m.tex.get();
    plume::RenderFramebufferDesc fb(&color, 1);
    m.fb = dev->createFramebuffer(fb);
    m.descriptor = host.register_texture(m.tex.get(), m.view.get());
    return m.fb != nullptr;
  }

  void Retire(Image& m) {
    if (!m.tex) return;
    if (m.descriptor) host.free_descriptor(m.descriptor);
    m.descriptor = 0;
    std::lock_guard l(retired_mutex);
    retired.push_back(std::move(m));
    m = Image{};
  }

  // (thread de submissão, com o mutex da fila) o contexto e as imagens antigas
  void DropContext(VkDevice dev, VkQueue q) {
    char err[512];
    if (ctx_on && del(ctx, err, sizeof err)) REXLOG_WARN("[fg] {}", err);
    ctx_on = false;
    std::vector<Image> old;
    {
      std::lock_guard l(retired_mutex);
      old.swap(retired);
    }
    if (!old.empty()) vkQueueWaitIdle(q);  // (as listas que usaram as imagens já foram submetidas antes daqui)
    (void)dev;
  }
};

Framegen::Framegen(FramegenHost host) : impl_(std::make_unique<Impl>()) { impl_->host = std::move(host); }

Framegen::~Framegen() {
  auto& s = *impl_;
  if (s.lib && s.lib_on) {
    auto* q = static_cast<plume::VulkanCommandQueue*>(s.host.queue);
    std::scoped_lock l(*q->queue->mutex);
    vkQueueWaitIdle(q->queue->vk);
    char err[256];
    if (s.ctx_on) s.del(s.ctx, err, sizeof err);
    s.finalize();
  }
}

bool Framegen::Enabled() { return REXCVAR_GET(odin2_framegen); }

bool Framegen::Begin(uint32_t w, uint32_t h) {
  auto& s = *impl_;
  if (!Enabled() || s.failed) {
    if (s.have_prev) s.have_prev = false;
    return false;
  }
  if (!s.lib) {  // primeira vez: tudo o que dá para conferir antes de mexer na fila
    std::string why;
    auto* vd = static_cast<plume::VulkanDevice*>(s.host.device);
    if (!Installed(&why)) {
      static bool logged;
      if (!logged) {
        REXLOG_INFO("[fg] desligada: {}", why);
        logged = true;
      }
      return false;  // (sem shaders: tenta de novo quando o usuário importar; não é falha)
    }
    if (!vd->memoryModelSupported) return s.Fail("o driver não tem vulkanMemoryModel ou imagens storage sem formato"), false;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(vd->physicalDevice, VK_FORMAT_R8G8B8A8_UNORM, &fp);
    const VkFormatFeatureFlags need =
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if ((fp.optimalTilingFeatures & need) != need) return s.Fail("RGBA8 sem suporte a imagem storage neste driver"), false;
    if (!s.LoadLib()) return false;
  }
  // opções mudaram: biblioteca e contexto refeitos antes do próximo quadro
  const float flow = float(std::clamp(REXCVAR_GET(odin2_fg_flow), 0.25, 1.0));
  const bool perf = REXCVAR_GET(odin2_fg_performance);
  bool redo = flow != s.flow || perf != s.perf;
  if (w != s.w || h != s.h || !s.in[0].tex) {
    s.Retire(s.in[0]), s.Retire(s.in[1]), s.Retire(s.out);
    s.w = w, s.h = h;
    if (!s.MakeImage(s.in[0]) || !s.MakeImage(s.in[1]) || !s.MakeImage(s.out))
      return s.Fail("sem memória de GPU para as imagens"), false;
    s.layouts_ready = false;
    redo = true;
  }
  if (redo) {
    s.flow = flow, s.perf = perf;
    s.count = 0;
    s.have_prev = false;
    s.want_gen.fetch_add(1);
  }
  auto now = std::chrono::steady_clock::now();
  if (s.last_real.time_since_epoch().count() && now - s.last_real > std::chrono::milliseconds(100)) s.have_prev = false;
  s.last_real = now;
  return true;
}

plume::RenderTexture* Framegen::Input() const { return impl_->in[impl_->count & 1].tex.get(); }
plume::RenderFramebuffer* Framegen::InputFramebuffer() const { return impl_->in[impl_->count & 1].fb.get(); }
uint32_t Framegen::InputDescriptor() const { return impl_->in[impl_->count & 1].descriptor; }
uint32_t Framegen::OutputDescriptor() const { return impl_->out.descriptor; }

void Framegen::FullBarrier(plume::RenderCommandList* list) {
  VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_MEMORY_WRITE_BIT,
                     VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
  vkCmdPipelineBarrier(static_cast<plume::VulkanCommandList*>(list)->vk, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

void Framegen::EndInput(plume::RenderCommandList* list) {
  auto& s = *impl_;
  std::vector<plume::RenderTextureBarrier> b;
  if (!s.layouts_ready) {  // (as três em GENERAL: o framegen as quer assim e as devolve assim)
    for (auto* m : {&s.in[0], &s.in[1], &s.out}) b.emplace_back(m->tex.get(), plume::RenderTextureLayout::GENERAL);
    s.layouts_ready = true;
  } else {
    b.emplace_back(Input(), plume::RenderTextureLayout::GENERAL);
  }
  list->barriers(plume::RenderBarrierStage::GRAPHICS | plume::RenderBarrierStage::COMPUTE, b.data(), uint32_t(b.size()));
  FullBarrier(list);
}

Framegen::Work Framegen::PrepareWork() const {
  auto& s = *impl_;
  Work w;
  w.gen = s.want_gen.load();
  w.w = s.w, w.h = s.h;
  w.in0 = uint64_t(s.in[0].vk()), w.in1 = uint64_t(s.in[1].vk()), w.out = uint64_t(s.out.vk());
  w.flow = s.flow, w.perf = s.perf;
  return w;
}

void Framegen::QueueWork(const Work& w) {
  auto& s = *impl_;
  if (s.failed) return;
  auto* q = static_cast<plume::VulkanCommandQueue*>(s.host.queue);
  auto* vd = static_cast<plume::VulkanDevice*>(s.host.device);
  std::scoped_lock lock(*q->queue->mutex);
  char err[512];
  if (w.gen != s.lib_gen) {  // (re)fazer: contexto antigo fora, biblioteca com as opções de agora, contexto novo
    s.DropContext(vd->vk, q->queue->vk);
    if (s.lib_on) s.finalize(), s.lib_on = false;
    int r = s.init(vkGetInstanceProcAddr, vd->renderInterface->instance, vd->physicalDevice, vd->vk, q->familyIndex,
                   q->queue->vk, w.flow, 1, w.perf ? 1 : 0, LsfgDir().c_str(), err, sizeof err);
    if (r) return s.Fail(r == kDeviceLost ? std::string("o driver perdeu o device: ") + err : err);
    s.lib_on = true;
    VkImage outs[1] = {VkImage(w.out)};
    r = s.create(VkImage(w.in0), VkImage(w.in1), outs, 1, w.w, w.h, VK_FORMAT_R8G8B8A8_UNORM, &s.ctx, err, sizeof err);
    if (r) return s.Fail(err);
    s.ctx_on = true;
    s.lib_gen = w.gen;
    REXLOG_INFO("[fg] pronta: LSFG 3.1{}, 2x, escala do fluxo {:.2f}, {}x{}", w.perf ? "P" : "", w.flow, w.w, w.h);
  }
  int r = s.present(s.ctx, err, sizeof err);
  if (r) return s.Fail(r == kDeviceLost ? std::string("o driver perdeu o device: ") + err : err);
  s.ok = true;
}

bool Framegen::ShowGenerated() const { return impl_->have_prev && impl_->ok && !impl_->failed; }

void Framegen::EndFrame() {
  auto& s = *impl_;
  s.st_real++;
  if (ShowGenerated()) s.st_gen++;
  s.count++;
  s.have_prev = true;
  auto now = std::chrono::steady_clock::now();
  if (now - s.st_since >= std::chrono::seconds(10)) {
    REXLOG_INFO("[fg] 10 s: {} quadros reais, {} gerados mostrados", s.st_real, s.st_gen);
    s.st_real = s.st_gen = 0;
    s.st_since = now;
  }
}

void Framegen::Reset() { impl_->have_prev = false; }

std::string Framegen::Status() const {
  auto& s = *impl_;
  if (!Enabled()) return "desligada";
  if (s.failed) {
    std::lock_guard l(s.why_mutex);
    return "parada: " + s.why;
  }
  std::string why;
  if (!Installed(&why)) return why;
  return s.ok ? "ligada (2x)" : "preparando";
}

}  // namespace gtav::native
