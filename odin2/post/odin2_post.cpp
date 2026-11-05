// Gtavport — pós-processamento antes da apresentação. Ver odin2_post.h.
// Mesma ordem do BOTWRecompAndroid (runtime/nvn/vulkan/exec.c, vk_present_draw): AA no tamanho da
// imagem do jogo, depois o upscaler até o tamanho dela na tela (só quando é ampliação).
#include "odin2_post.h"

#include "odin2_post_shaders.h"  // gerado pelo DXC na build (odin2_post.cmake)
#include "third_party/smaa/AreaTex.h"
#include "third_party/smaa/SearchTex.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

REXCVAR_DEFINE_STRING(odin2_antialias, "off", "Gtavport",
                      "Anti-aliasing da imagem do jogo antes da apresentação: off, fxaa, smaa (vale na hora)");
REXCVAR_DEFINE_STRING(odin2_upscaler, "bilinear", "Gtavport",
                      "Como a imagem do jogo chega ao tamanho da tela: bilinear (o do upstream), fsr1 (AMD FSR 1: "
                      "EASU + RCAS), sgsr1 (Snapdragon Game Super Resolution 1, uma passada) (vale na hora)");
REXCVAR_DEFINE_DOUBLE(odin2_fsr_sharpness, 0.2, "Gtavport",
                      "FSR 1 RCAS: nitidez em stops (0 = máxima, 2 = mínima; 0.2 é o padrão da AMD)");

namespace gtav::native {

using namespace plume;

namespace {
enum Pass { kFxaa, kSmaaEdge, kSmaaWeight, kSmaaBlend, kEasu, kRcas, kSgsr1, kPassCount };

struct PassShader {
  const uint8_t* code;
  size_t size;
  const char* entry;
};
const PassShader kShaders[kPassCount] = {
    {g_odin2_PSFxaa, sizeof(g_odin2_PSFxaa), "PSFxaa"},
    {g_odin2_PSSmaaEdge, sizeof(g_odin2_PSSmaaEdge), "PSSmaaEdge"},
    {g_odin2_PSSmaaWeight, sizeof(g_odin2_PSSmaaWeight), "PSSmaaWeight"},
    {g_odin2_PSSmaaBlend, sizeof(g_odin2_PSSmaaBlend), "PSSmaaBlend"},
    {g_odin2_PSEasu, sizeof(g_odin2_PSEasu), "PSEasu"},
    {g_odin2_PSRcas, sizeof(g_odin2_PSRcas), "PSRcas"},
    {g_odin2_PSSgsr1, sizeof(g_odin2_PSSgsr1), "PSSgsr1"},
};

struct Push {  // = PushConstants de odin2_post.hlsl
  float a[4];
  float b[4];
  uint32_t t0, t1, t2, linear_sampler, point_sampler, pad[3];
};
static_assert(sizeof(Push) == 64);

// Imagens intermediárias. 0/1/2: SMAA bordas / pesos / saída (ou FXAA), 3/4: EASU / RCAS ou SGSR.
enum Image { kEdges, kWeights, kAaOut, kUpA, kUpB, kImageCount };
}  // namespace

struct Post::Impl {
  PostHost host;
  std::unique_ptr<RenderShader> vs;
  std::unique_ptr<RenderShader> ps[kPassCount];
  std::unique_ptr<RenderPipelineLayout> layout;
  std::unordered_map<uint64_t, std::unique_ptr<RenderPipeline>> pipelines;  // (pass, formato)
  uint32_t linear = 0, point = 0;
  bool ok = false, logged_fail = false;

  struct Target {
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderTextureView> view;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    uint32_t w = 0, h = 0, descriptor = 0;
    RenderFormat format = RenderFormat::UNKNOWN;
    RenderTextureLayout layout = RenderTextureLayout::UNKNOWN;
  } images[kImageCount];

  // Tabelas do SMAA (area 160x560 RG8, search 64x16 R8), enviadas uma vez.
  Target area, search;
  std::unique_ptr<RenderBuffer> lut_upload;
  bool luts_ready = false;

  bool Init() {
    auto* dev = host.device;
    vs = dev->createShader(g_odin2_VSPost, sizeof(g_odin2_VSPost), "VSPost", RenderShaderFormat::SPIRV);
    for (int i = 0; i < kPassCount; i++)
      ps[i] = dev->createShader(kShaders[i].code, kShaders[i].size, kShaders[i].entry, RenderShaderFormat::SPIRV);
    // Layout compatível com os heaps do backend (mesmas definições): texturas no set 0, samplers no 1.
    RenderDescriptorSetBuilder tex, smp;
    tex.begin();
    tex.addTexture(0, host.texture_heap_size);
    tex.end(true, host.texture_heap_size);
    smp.begin();
    smp.addSampler(0, host.sampler_heap_size);
    smp.end(true, host.sampler_heap_size);
    RenderPipelineLayoutBuilder lb;
    lb.begin(false, false);
    lb.addDescriptorSet(tex);
    lb.addDescriptorSet(smp);
    lb.addPushConstant(0, 2, sizeof(Push), RenderShaderStageFlag::VERTEX | RenderShaderStageFlag::PIXEL);
    lb.end();
    layout = lb.create(dev);
    RenderSamplerDesc s;
    s.addressU = s.addressV = s.addressW = RenderTextureAddressMode::CLAMP;
    s.magFilter = s.minFilter = RenderFilter::LINEAR;
    linear = host.sampler(s);
    s.magFilter = s.minFilter = RenderFilter::NEAREST;
    point = host.sampler(s);
    ok = vs && layout;
    for (auto& p : ps) ok = ok && p;
    if (!ok) REXLOG_ERROR("[odin2] pós-processamento: shaders ou layout não criados; usando o blit do upstream");
    return ok;
  }

  RenderPipeline* Pipeline(int pass, RenderFormat format) {
    uint64_t key = uint64_t(pass) << 32 | uint32_t(format);
    auto& p = pipelines[key];
    if (!p) {
      RenderGraphicsPipelineDesc d;
      d.pipelineLayout = layout.get();
      d.vertexShader = vs.get();
      d.pixelShader = ps[pass].get();
      d.renderTargetCount = 1;
      d.renderTargetFormat[0] = format;
      d.renderTargetBlend[0] = RenderBlendDesc::Copy();
      p = host.device->createGraphicsPipeline(d);
    }
    return p.get();
  }

  void Ensure(Target& t, uint32_t w, uint32_t h, RenderFormat format, bool render_target) {
    if (t.texture && t.w == w && t.h == h && t.format == format) return;
    // Recriado só quando o tamanho muda (troca de opção / rotação): o quadro anterior já foi enviado
    // à fila, e a fila executa em ordem, então liberar aqui não pega nada em uso pela GPU... exceto
    // os quadros ainda em voo; por isso a textura antiga vai para `retired` por alguns quadros.
    if (t.texture) Retire(t);
    t.texture = host.device->createTexture(render_target ? RenderTextureDesc::ColorTarget(w, h, format)
                                                         : RenderTextureDesc::Texture2D(w, h, 1, format));
    t.view = t.texture->createTextureView(RenderTextureViewDesc::Texture2D(format));
    if (render_target) {
      const RenderTexture* color = t.texture.get();
      RenderFramebufferDesc fb;
      fb.colorAttachments = &color;
      fb.colorAttachmentsCount = 1;
      t.framebuffer = host.device->createFramebuffer(fb);
    }
    if (!t.descriptor) t.descriptor = host.alloc_descriptor();
    host.textures->setTexture(t.descriptor, t.texture.get(), RenderTextureLayout::SHADER_READ, t.view.get());
    t.w = w;
    t.h = h;
    t.format = format;
    t.layout = RenderTextureLayout::UNKNOWN;
  }

  // Texturas trocadas ficam vivas por alguns quadros (os quadros em voo ainda podem lê-las).
  struct Retired {
    std::unique_ptr<RenderTexture> texture;
    std::unique_ptr<RenderTextureView> view;
    std::unique_ptr<RenderFramebuffer> framebuffer;
    uint32_t frames_left;
  };
  std::vector<Retired> retired;
  void Retire(Target& t) {
    retired.push_back({std::move(t.texture), std::move(t.view), std::move(t.framebuffer), 4});
  }
  void AgeRetired() {
    for (size_t i = 0; i < retired.size();)
      if (--retired[i].frames_left == 0) {
        retired[i] = std::move(retired.back());
        retired.pop_back();
      } else {
        i++;
      }
  }

  void To(RenderCommandList* list, Target& t, RenderTextureLayout layout) {
    if (t.layout == layout) return;
    list->barriers(layout == RenderTextureLayout::COPY_DEST ? RenderBarrierStage::COPY : RenderBarrierStage::GRAPHICS,
                   RenderTextureBarrier(t.texture.get(), layout));
    t.layout = layout;
  }

  void UploadLuts(RenderCommandList* list) {
    if (luts_ready) return;
    Ensure(area, AREATEX_WIDTH, AREATEX_HEIGHT, RenderFormat::R8G8_UNORM, false);
    Ensure(search, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, RenderFormat::R8_UNORM, false);
    const uint64_t area_size = uint64_t(AREATEX_WIDTH) * AREATEX_HEIGHT * 2;
    const uint64_t search_size = uint64_t(SEARCHTEX_WIDTH) * SEARCHTEX_HEIGHT;
    lut_upload = host.device->createBuffer(RenderBufferDesc::UploadBuffer(area_size + search_size));
    auto* p = static_cast<uint8_t*>(lut_upload->map());
    std::memcpy(p, areaTexBytes, size_t(area_size));
    std::memcpy(p + area_size, searchTexBytes, size_t(search_size));
    lut_upload->unmap();
    To(list, area, RenderTextureLayout::COPY_DEST);
    To(list, search, RenderTextureLayout::COPY_DEST);
    list->copyTextureRegion(RenderTextureCopyLocation::Subresource(area.texture.get()),
                            RenderTextureCopyLocation::PlacedFootprint(lut_upload.get(), RenderFormat::R8G8_UNORM,
                                                                       AREATEX_WIDTH, AREATEX_HEIGHT, 1, AREATEX_WIDTH, 0));
    list->copyTextureRegion(RenderTextureCopyLocation::Subresource(search.texture.get()),
                            RenderTextureCopyLocation::PlacedFootprint(lut_upload.get(), RenderFormat::R8_UNORM,
                                                                       SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 1,
                                                                       SEARCHTEX_WIDTH, area_size));
    To(list, area, RenderTextureLayout::SHADER_READ);
    To(list, search, RenderTextureLayout::SHADER_READ);
    luts_ready = true;
  }

  // Um passe de tela cheia em `out`, lendo t0..t2 (descritores).
  void Draw(RenderCommandList* list, int pass, Target& out, uint32_t t0, uint32_t t1, uint32_t t2, const float a[4],
            const float b[4]) {
    To(list, out, RenderTextureLayout::COLOR_WRITE);
    list->setFramebuffer(out.framebuffer.get());
    list->setViewports(RenderViewport(0, 0, float(out.w), float(out.h)));
    list->setScissors(RenderRect(0, 0, int32_t(out.w), int32_t(out.h)));
    list->setGraphicsPipelineLayout(layout.get());
    list->setPipeline(Pipeline(pass, out.format));
    list->setGraphicsDescriptorSet(host.textures, 0);
    list->setGraphicsDescriptorSet(host.samplers, 1);
    Push p{};
    std::memcpy(p.a, a, sizeof(p.a));
    std::memcpy(p.b, b, sizeof(p.b));
    p.t0 = t0;
    p.t1 = t1;
    p.t2 = t2;
    p.linear_sampler = linear;
    p.point_sampler = point;
    list->setGraphicsPushConstants(0, &p, 0, sizeof(p));
    list->drawInstanced(3, 1, 0, 0);
    list->setFramebuffer(nullptr);
    To(list, out, RenderTextureLayout::SHADER_READ);
  }
};

Post::Post(PostHost host) : impl_(std::make_unique<Impl>()) {
  impl_->host = std::move(host);
  impl_->Init();
}
Post::~Post() = default;

uint32_t Post::Run(RenderCommandList* list, uint32_t descriptor, uint32_t src_w, uint32_t src_h, uint32_t out_w,
                   uint32_t out_h) {
  auto& s = *impl_;
  s.AgeRetired();
  const std::string aa = REXCVAR_GET(odin2_antialias), up = REXCVAR_GET(odin2_upscaler);
  const bool fxaa = aa == "fxaa", smaa = aa == "smaa";
  // Upscaler só quando a imagem é ampliada (num tamanho igual ou menor o blit bilinear já é o certo).
  const bool enlarge = out_w > src_w + 1 || out_h > src_h + 1;
  const bool fsr = up == "fsr1" && enlarge, sgsr = up == "sgsr1" && enlarge;
  last_chain_ = "off";
  if ((!fxaa && !smaa && !fsr && !sgsr) || !s.ok || !src_w || !src_h || !out_w || !out_h) return descriptor;

  constexpr RenderFormat kColor = RenderFormat::R8G8B8A8_UNORM;
  const float iw = 1.0f / float(src_w), ih = 1.0f / float(src_h);
  const float metrics[4] = {iw, ih, float(src_w), float(src_h)}, zero[4] = {};
  uint32_t cur = descriptor;
  if (smaa) {
    s.UploadLuts(list);
    s.Ensure(s.images[kEdges], src_w, src_h, RenderFormat::R8G8_UNORM, true);
    s.Ensure(s.images[kWeights], src_w, src_h, kColor, true);
    s.Ensure(s.images[kAaOut], src_w, src_h, kColor, true);
    s.Draw(list, kSmaaEdge, s.images[kEdges], cur, 0, 0, metrics, zero);
    s.Draw(list, kSmaaWeight, s.images[kWeights], s.images[kEdges].descriptor, s.area.descriptor, s.search.descriptor,
           metrics, zero);
    s.Draw(list, kSmaaBlend, s.images[kAaOut], cur, s.images[kWeights].descriptor, 0, metrics, zero);
    cur = s.images[kAaOut].descriptor;
  } else if (fxaa) {
    s.Ensure(s.images[kAaOut], src_w, src_h, kColor, true);
    s.Draw(list, kFxaa, s.images[kAaOut], cur, 0, 0, metrics, zero);
    cur = s.images[kAaOut].descriptor;
  }
  if (sgsr) {
    s.Ensure(s.images[kUpA], out_w, out_h, kColor, true);
    const float b[4] = {float(out_w), float(out_h), 0, 0};
    s.Draw(list, kSgsr1, s.images[kUpA], cur, 0, 0, metrics, b);
    cur = s.images[kUpA].descriptor;
  } else if (fsr) {
    s.Ensure(s.images[kUpA], out_w, out_h, kColor, true);
    s.Ensure(s.images[kUpB], out_w, out_h, kColor, true);
    // EASU: posição de origem = (pixel de saída) * (entrada/saída) + deslocamento de meio texel.
    const float sx = float(src_w) / float(out_w), sy = float(src_h) / float(out_h);
    const float a[4] = {sx, sy, 0.5f * sx - 0.5f, 0.5f * sy - 0.5f};
    const float b[4] = {float(src_w), float(src_h), 0, 0};
    s.Draw(list, kEasu, s.images[kUpA], cur, 0, 0, a, b);
    const float r[4] = {std::exp2(-float(REXCVAR_GET(odin2_fsr_sharpness))), 0, 0, 0};
    const float rb[4] = {float(out_w), float(out_h), 0, 0};
    s.Draw(list, kRcas, s.images[kUpB], s.images[kUpA].descriptor, 0, 0, r, rb);
    cur = s.images[kUpB].descriptor;
  }
  s.host.invalidate_draw_state();
  last_chain_ = smaa ? (fsr ? "smaa+fsr1" : sgsr ? "smaa+sgsr1" : "smaa")
                : fxaa ? (fsr ? "fxaa+fsr1" : sgsr ? "fxaa+sgsr1" : "fxaa")
                       : (fsr ? "fsr1" : "sgsr1");
  return cur;
}

}  // namespace gtav::native
