// Gtavport — "Medir desempenho" (odin2/perf), portado da ideia do diagnóstico do TOTK/BOTW
// (rickamaral94/Totknspcxi, runtime/diag.c e docs/DESEMPENHO-DIAG.md; MIT, LAS Recomp/Peiikai), adaptado
// ao GTA V: sem ganchos no kernel emulado do ReXGlue, tudo medido de fora das threads do jogo.
//
// Fluxo: Medir (menu do jogo, ou a tela Configurações do app, que deixa diag_request.txt para o próximo
// início) -> espera 5 s seguidos de jogo (quadros com cena 3D, menu fechado) -> mede N segundos de jogo ->
// relatório em linhas "[diag]" no gtav.log e em logs/perf/perf-diag-<data>.txt (tela Logs de desempenho).
//
// O que o relatório traz:
//   run/summary   aparelho, driver, configurações; fps, p50/p95/p99/máx do intervalo entre quadros, GPU por
//                 quadro, tempo do renderer (gravando / esperando o jogo / esperando a GPU), MHz médios de
//                 cada cluster e da GPU, temperatura, status térmico e headroom.
//   threads       por thread do processo (as 14 que mais usaram CPU): CPU própria, % do tempo rodando ou na
//                 fila (R), dormindo/esperando (S), esperando disco (D), na fila sem núcleo (R - CPU) e em que
//                 cluster rodou (eficiência / performance / prime). MB lidos e faltas de página.
//   funções       amostras de PC das 4 threads que mais usam CPU (SIGPROF pedido de fora, a cada ~4 ms, só a
//                 quem está em R, só durante a medição; ligado e desligado em blocos de 5 s para medir o
//                 custo): o endereço vira sub_<endereço no Xbox 360> pela .symtab da libgtav_game.so, o resto
//                 vira símbolo exportado ou módulo+offset. Também os "chamadores" (registrador de retorno).
//
// Custo desligado: um teste de flag por quadro. Nenhuma thread extra fica rodando fora da medição.
#include "odin2_perf.h"
#include "odin2_diag_stats.h"
#include "odin2_elfsym.h"
#include "odin2_procstat.h"

#include "paths.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <csignal>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

#include <dlfcn.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

namespace gtav::native::perf {

namespace {

using Clock = std::chrono::steady_clock;
using namespace diag;

constexpr double kWarmupMs = 5000;       // jogo contínuo antes de começar
constexpr uint32_t kGameplayDraws = 300;  // menos que isso: tela de loading, menu, vídeo (no jogo: 580-1000+)
constexpr int kTargets = 4;               // threads amostradas por PC

enum class State { kIdle, kWaiting, kRecording, kReporting };

std::mutex g_mutex;
std::atomic<State> g_state{State::kIdle};
int g_seconds = 0;
double g_warm_ms = 0;          // jogo contínuo acumulado na espera
double g_game_ms = 0;          // jogo medido
Clock::time_point g_rec_start, g_armed_at;
std::string g_last_report;     // caminho do último relatório (para o menu)
bool g_from_file = false;      // pedido do app: o arquivo só sai quando houver relatório
bool g_always = false;         // "em todo início": o arquivo fica
bool g_menu_open_hint = false;

// Quadros da medição
struct FrameRec {
  double interval, record, idle, gpu_wait, submit;
  uint32_t draws;
  bool sampler_on;
  int variant;  // A/B: índice em kVariants (0 = como está)
};
std::vector<FrameRec> g_frames;
std::vector<double> g_gpu;  // tempo de GPU por quadro (chega depois)
// A/B ("Comparar" no app): a cada 10 s de medição troca uma opção suspeita e o relatório compara os lados.
// Feito para as fases em que o passe principal (alvo HDR 1280x721 com profundidade) ia de ~3 ms a ~75 ms.
struct Variant {
  std::string name;
  std::string cvar;   // vazio: como está
  std::string value;
};
const Variant kVariants[] = {
    {"como está", "", ""},
    {"luzes a 2x2 (light_coarse=2)", "gtav_native_light_coarse", "2"},
    {"teste depois do shader, como antes (early_tests=false)", "gtav_native_early_tests", "false"},
    {"tudo a 2x2 (coarse_shading=2)", "gtav_native_coarse_shading", "2"},
};
// Variantes da medição atual (A/B: as de cima; "hdr": como está + um pixel shader do passe HDR desligado
// por vez) e a ordem do rodízio (índices em g_vars; "como está" volta a cada poucos blocos no modo hdr, para
// a comparação não depender de o jogador ter mudado de cena).
std::vector<Variant> g_vars;
std::vector<int> g_schedule;
size_t g_slot = 0;
std::atomic<int> g_variant{0};
std::vector<std::string> g_ab_saved;
bool g_ab = false;
bool g_hdr = false;  // "hdr": censo dos pixel shaders do passe HDR e rodízio com cada um desligado
// Censo (backend -> NoteHdrDraw): draws por pixel shader no alvo HDR (R16G16B16A16_FLOAT >= 640 + depth).
std::mutex g_census_mutex;
std::unordered_map<uint64_t, uint64_t> g_census;
std::atomic<uint64_t> g_census_frames{0};
std::atomic<bool> g_census_on{false};
std::vector<std::pair<uint64_t, uint64_t>> CensusSorted();  // (pixel shader, draws), do que mais desenha
void SaveLogcat(const std::filesystem::path& out);  // logcat do processo, filtrado (driver e crashes)
constexpr size_t kHdrCandidates = 24;  // 24 x 10 s + "como está" a cada 4: ~5 min dos 10
std::vector<std::pair<int, double>> g_gpu_variant;  // (variante, GPU ms)
// Linha do tempo (a cada 2 s) e os passes de GPU da janela de 300 quadros mais rápida e da mais lenta:
// as travadas de GPU vêm em fases (11 ms -> 60 ms por quadro com os mesmos passes), e a média escondia.
std::vector<std::string> g_timeline;
size_t g_tl_frame = 0, g_tl_gpu = 0;
double g_tl_start_ms = 0;
std::vector<std::pair<std::string, double>> g_pass_best, g_pass_worst;
double g_pass_best_total = 0, g_pass_worst_total = 0;
std::string g_pass_flag_before;
uint64_t g_nongame_frames = 0;

// Amostras de PC (SIGPROF): anel sem lock, escrito pelo handler.
constexpr int kFrames = 5;  // retornos seguidos pela cadeia de frame pointers (fp, lr)
struct PcSample {
  int32_t tid;
  uint64_t pc, lr;
  uint64_t frames[kFrames];
};

// Lê 16 bytes do próprio processo sem risco: process_vm_readv devolve erro (EFAULT) num endereço ruim em vez
// de um SIGSEGV dentro do handler de sinal (uma cadeia de frames quebrada não derruba o jogo).
inline bool SafeRead(uint64_t addr, uint64_t out[2]) {
  struct iovec local = {out, 16}, remote = {reinterpret_cast<void*>(addr), 16};
  return syscall(SYS_process_vm_readv, getpid(), &local, 1UL, &remote, 1UL, 0UL) == 16;
}

// arm64 do Android assina endereços de retorno (PAC): os bits altos não são endereço.
inline uint64_t StripPac(uint64_t a) { return a & 0x0000FFFFFFFFFFFFull; }
constexpr uint32_t kPcRing = 1u << 17;
PcSample g_pc[kPcRing];
std::atomic<uint32_t> g_pc_n{0};
std::atomic<uint32_t> g_pc_lost{0};
std::atomic<bool> g_sampler_on{false};
int g_targets[kTargets] = {0, 0, 0, 0};
std::atomic<int> g_target_count{0};
bool g_handler_installed = false;

void ProfHandler(int, siginfo_t*, void* uc_void) {
  const int saved = errno;
  uint32_t i = g_pc_n.fetch_add(1, std::memory_order_relaxed);
  if (i < kPcRing) {
    auto* uc = static_cast<ucontext_t*>(uc_void);
#if defined(__aarch64__)
    PcSample& out = g_pc[i];
    out.tid = int32_t(syscall(SYS_gettid));
    out.pc = uint64_t(uc->uc_mcontext.pc);
    out.lr = StripPac(uint64_t(uc->uc_mcontext.regs[30]));
    // Cadeia de frames: [fp] = fp anterior, [fp + 8] = retorno. Só dentro da pilha desta thread (entre sp e
    // sp + 8 MB, alinhado, sempre subindo); fora disso para (o código sem frame pointer quebra a cadeia).
    const uint64_t sp = uint64_t(uc->uc_mcontext.sp);
    uint64_t fp = uint64_t(uc->uc_mcontext.regs[29]);
    for (int k = 0; k < kFrames; k++) {
      out.frames[k] = 0;
      if (fp < sp || fp >= sp + (8u << 20) || (fp & 15)) continue;
      uint64_t f[2];
      if (!SafeRead(fp, f)) {
        fp = 0;
        continue;
      }
      out.frames[k] = StripPac(f[1]);
      const uint64_t next = f[0];
      if (next <= fp) {
        fp = 0;
        continue;
      }
      fp = next;
    }
#elif defined(__x86_64__)
    g_pc[i] = {int32_t(syscall(SYS_gettid)), uint64_t(uc->uc_mcontext.gregs[REG_RIP]), 0, {}};
#else
    g_pc[i] = {int32_t(syscall(SYS_gettid)), 0, 0, {}};
#endif
  } else {
    g_pc_lost.fetch_add(1, std::memory_order_relaxed);
  }
  errno = saved;
}

void InstallHandler() {
  if (g_handler_installed) return;
  struct sigaction sa = {};
  sa.sa_sigaction = ProfHandler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, nullptr);
  g_handler_installed = true;
}

// Clusters dos núcleos (0 eficiência, 1 performance, 2 prime).
std::vector<int> g_cluster;
void FindClusters() {
  std::vector<long> f;
  for (int c = 0; c < 16; c++) {
    char p[96], b[64];
    std::snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
    f.push_back(ProcReadFile(p, b, sizeof b) ? std::atol(b) : -1);
  }
  g_cluster = ClustersFromMaxFreq(f);
}

std::string Timestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char b[32];
  std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm);
  return b;
}

std::string Fmt(const char* f, double a) {
  char b[64];
  std::snprintf(b, sizeof b, f, a);
  return b;
}

// ---------------- símbolos ----------------
struct Module {
  std::string path;
  uintptr_t base = 0;
  bool loaded = false;
  ElfSymbols syms;
};
std::unordered_map<std::string, Module> g_modules;

std::string Symbolize(uint64_t addr) {
  Dl_info info{};
  if (!addr || !dladdr(reinterpret_cast<void*>(addr), &info) || !info.dli_fname) {
    char b[32];
    std::snprintf(b, sizeof b, "?0x%llx", (unsigned long long)(addr & ~uint64_t(63)));
    return b;
  }
  const std::string path = info.dli_fname;
  const uint64_t rel = addr - uint64_t(reinterpret_cast<uintptr_t>(info.dli_fbase));
  Module& m = g_modules[path];
  if (!m.loaded) {
    m.loaded = true;
    m.path = path;
    // A .symtab só da biblioteca do jogo (e das nossas): as do sistema não têm, e o dladdr basta.
    if (path.find("libgtav") != std::string::npos || path.find("librex") != std::string::npos) m.syms.Load(path);
  }
  if (const std::string* n = m.syms.Find(rel)) return *n;
  if (info.dli_sname && info.dli_saddr) return BaseName(path) + ":" + info.dli_sname;
  return ModuleOffset(path, rel);
}

// ---------------- medição ----------------
struct Device {
  std::vector<double> cpu[3], gpu_mhz, temp, headroom;
  int thermal_max = -1;
};

void Report(std::map<int, ThreadUse> use, const ProcSnapshot& first, const ProcSnapshot& last, bool have_snaps,
            const Device& dev, double window_ms) {
  std::vector<FrameRec> frames;
  std::vector<double> gpu;
  uint64_t nongame;
  {
    std::lock_guard lock(g_mutex);
    frames.swap(g_frames);
    gpu.swap(g_gpu);
    nongame = g_nongame_frames;
  }
  std::ostringstream out;
  auto line = [&](const std::string& s) {
    out << s << '\n';
    REXLOG_INFO("[diag] {}", s);
  };
  line("run: " + RunDescription() + " | medição " + std::to_string(g_seconds) + " s de jogo");
  line("sistema: " + SystemDescription());
  line("driver: " + DriverDescription());

  std::vector<double> iv, iv_on, iv_off, rec, idle, gw;
  double game_ms = 0;
  for (const FrameRec& f : frames) {
    iv.push_back(f.interval);
    (f.sampler_on ? iv_on : iv_off).push_back(f.interval);
    rec.push_back(f.record);
    idle.push_back(f.idle);
    gw.push_back(f.gpu_wait);
    game_ms += f.interval;
  }
  const double fps = game_ms > 0 ? 1000.0 * double(frames.size()) / game_ms : 0;
  auto avg_cluster = [&](int c) { return dev.cpu[c].empty() ? std::string("n/d") : Fmt("%.0f", Mean(dev.cpu[c])); };
  size_t over50 = 0, over100 = 0, over400 = 0;
  for (double x : iv) {
    over50 += x > 50;
    over100 += x > 100;
    over400 += x > 400;
  }
  char b[640];
  std::snprintf(b, sizeof b,
                "summary: %.1f fps no jogo (%zu quadros; %llu fora de jogo ignorados); intervalo p50 %.1f p95 %.1f p99 %.1f "
                "máx %.0f ms; >50 ms %zu, >100 ms %zu, >400 ms %zu; GPU %.1f ms/quadro (p95 %.1f); "
                "MHz prime %s perf %s eficiência %s, GPU %s; temp %s; térmico máx %d; headroom máx %s",
                fps, frames.size(), (unsigned long long)nongame, Percentile(iv, 50), Percentile(iv, 95),
                Percentile(iv, 99), Percentile(iv, 100), over50, over100, over400, Mean(gpu), Percentile(gpu, 95),
                avg_cluster(2).c_str(), avg_cluster(1).c_str(), avg_cluster(0).c_str(),
                dev.gpu_mhz.empty() ? "n/d" : Fmt("%.0f", Mean(dev.gpu_mhz)).c_str(),
                dev.temp.empty() ? "n/d"
                                 : (Fmt("%.0f", Percentile(dev.temp, 0)) + "-" + Fmt("%.0f °C", Percentile(dev.temp, 100))).c_str(),
                dev.thermal_max, dev.headroom.empty() ? "n/d" : Fmt("%.2f", Percentile(dev.headroom, 100)).c_str());
  line(b);
  const double iv_mean = Mean(iv);
  std::snprintf(b, sizeof b,
                "renderer por quadro: gravando %.1f ms, esperando o jogo %.1f ms (%.0f%% do quadro), esperando a GPU "
                "%.1f ms; GPU %.1f ms. Gargalo: %s",
                Mean(rec), Mean(idle), iv_mean > 0 ? 100.0 * Mean(idle) / iv_mean : 0, Mean(gw), Mean(gpu),
                Mean(idle) > 0.4 * iv_mean ? "o JOGO (CPU das threads do jogo): renderer e GPU esperam por ele"
                : Mean(gw) > 0.3 * iv_mean ? "a GPU"
                : Mean(rec) > 0.5 * iv_mean ? "a thread de render"
                                            : "misto / limite de fps");
  line(b);
  if (!iv_on.empty() && !iv_off.empty()) {
    std::snprintf(b, sizeof b, "custo do amostrador: %.1f fps com ele (%zu quadros) e %.1f fps sem (%zu quadros)",
                  1000.0 / Mean(iv_on), iv_on.size(), 1000.0 / Mean(iv_off), iv_off.size());
    line(b);
  }

  {
    std::vector<std::string> tl;
    std::vector<std::pair<std::string, double>> best, worst;
    double bt, wt;
    {
      std::lock_guard lock(g_mutex);
      tl.swap(g_timeline);
      best.swap(g_pass_best);
      worst.swap(g_pass_worst);
      bt = g_pass_best_total;
      wt = g_pass_worst_total;
    }
    line("linha do tempo (a cada ~2 s; tempo de jogo medido):");
    for (const std::string& t : tl) line(t);
    if (!worst.empty()) {
      std::snprintf(b, sizeof b, "GPU por tipo de passe, ms por quadro: janela mais rápida (total %.1f) x mais lenta (total %.1f):", bt, wt);
      line(b);
      std::map<std::string, double> fast(best.begin(), best.end());
      for (const auto& [k, v] : worst) {
        auto it = fast.find(k);
        std::snprintf(b, sizeof b, "  %6.2f -> %6.2f ms  %s", it == fast.end() ? 0.0 : it->second, v, k.c_str());
        line(b);
      }
    }
  }

  if (g_ab) {
    std::vector<std::pair<int, double>> gv;
    {
      std::lock_guard lock(g_mutex);
      gv.swap(g_gpu_variant);
    }
    line(g_hdr ? "passe HDR, um pixel shader desligado por vez (10 s cada; a imagem perde o que ele desenha): "
                 "fps | p95 do quadro | GPU média e p95 | quadros > 100 ms"
               : "comparar (A/B, 10 s cada, em rodízio): fps | p95 do quadro | GPU média e p95 | quadros > 100 ms");
    for (int v = 0; v < int(g_vars.size()); v++) {
      std::vector<double> ivv, gg;
      size_t slow = 0;
      double tot = 0;
      for (const FrameRec& f : frames)
        if (f.variant == v) {
          ivv.push_back(f.interval);
          tot += f.interval;
          slow += f.interval > 100;
        }
      for (const auto& [vv, ms] : gv)
        if (vv == v) gg.push_back(ms);
      if (ivv.empty() && gg.empty()) continue;  // modo hdr: candidato que o tempo não alcançou
      std::snprintf(b, sizeof b, "  %5.1f fps | p95 %6.1f ms | GPU %5.1f / %6.1f ms | %4zu  %s",
                    tot > 0 ? 1000.0 * double(ivv.size()) / tot : 0, Percentile(ivv, 95), Mean(gg), Percentile(gg, 95), slow,
                    g_vars[v].name.c_str());
      line(b);
    }
  }

  if (g_hdr) {
    const auto census = CensusSorted();
    const double frames = double(std::max<uint64_t>(1, g_census_frames.load()));
    std::snprintf(b, sizeof b, "censo do passe HDR: %zu pixel shaders em %.0f quadros (draws por quadro; os %zu primeiros "
                  "entraram no rodízio)", census.size(), frames, kHdrCandidates);
    line(b);
    for (size_t i = 0; i < census.size() && i < 64; i++) {
      std::snprintf(b, sizeof b, "  %016llX  %8.2f", static_cast<unsigned long long>(census[i].first),
                    double(census[i].second) / frames);
      line(b);
    }
  }

  // Threads
  if (have_snaps) {
    const long hz = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
    for (auto& [tid, u] : use) {
      auto a = first.threads.find(tid);
      auto z = last.threads.find(tid);
      if (z == last.threads.end()) continue;
      uint64_t t0 = a == first.threads.end() ? 0 : a->second.ticks;
      u.cpu_ms = double(z->second.ticks - std::min(z->second.ticks, t0)) * 1000.0 / double(hz);
    }
    double total = 0;
    for (auto& [tid, u] : use) total += u.cpu_ms;
    std::snprintf(b, sizeof b, "processo: %.0f%% de um núcleo em média (%.0f ms de CPU em %.0f ms); lidos %.1f MB; %llu faltas de página maiores",
                  100.0 * total / window_ms, total, window_ms,
                  double(last.read_bytes - std::min(last.read_bytes, first.read_bytes)) / 1048576.0,
                  (unsigned long long)(last.majflt - std::min(last.majflt, first.majflt)));
    line(b);
    line("threads (CPU própria | R rodando ou na fila | S dormindo/esperando | D disco | fila sem núcleo | cluster quando em R: efic/perf/prime):");
    for (const auto& [tid, u] : TopThreads(use, 14)) {
      const double n = double(std::max<uint64_t>(u.samples, 1)), rr = double(std::max<uint64_t>(u.r, 1));
      std::snprintf(b, sizeof b, "  %-16s %6d  CPU %5.1f%% | R %3.0f%% S %3.0f%% D %3.0f%% | fila %3.0f%% | %3.0f/%3.0f/%3.0f%%",
                    u.name.c_str(), tid, 100.0 * u.cpu_ms / window_ms, 100.0 * double(u.r) / n, 100.0 * double(u.s) / n,
                    100.0 * double(u.d) / n, 100.0 * QueuedFraction(u, window_ms), 100.0 * double(u.cluster_r[0]) / rr,
                    100.0 * double(u.cluster_r[1]) / rr, 100.0 * double(u.cluster_r[2]) / rr);
      line(b);
    }
  }

  // Funções
  const uint32_t n = std::min(g_pc_n.load(), kPcRing);
  if (n > 0) {
    std::map<std::string, uint64_t> all, callers;
    std::map<int, std::map<std::string, uint64_t>> per_thread;
    std::unordered_map<uint64_t, std::string> cache;
    auto name_of = [&](uint64_t a) -> const std::string& {
      auto it = cache.find(a);
      if (it != cache.end()) return it->second;
      return cache.emplace(a, Symbolize(a)).first->second;
    };
    std::map<int, std::map<std::string, uint64_t>> stacks;  // por thread: "função < chamador < ..."
    for (uint32_t i = 0; i < n; i++) {
      const PcSample& s = g_pc[i];
      const std::string& f = name_of(s.pc);
      all[f]++;
      per_thread[s.tid][f]++;
      if (s.lr) callers[name_of(s.lr)]++;
      std::string st = f;
      for (int k = 0; k < kFrames && s.frames[k]; k++) st += "  <  " + name_of(s.frames[k]);
      stacks[s.tid][st]++;
    }
    std::snprintf(b, sizeof b, "funções: %u amostras de PC (%u perdidas) nas threads amostradas; %% das amostras:", n,
                  g_pc_lost.load());
    line(b);
    for (const auto& [f, c] : TopNames(all, 25)) {
      std::snprintf(b, sizeof b, "  %5.1f%%  %s", 100.0 * double(c) / double(n), f.c_str());
      line(b);
    }
    line("chamadores mais frequentes (registrador de retorno, só o último nível):");
    for (const auto& [f, c] : TopNames(callers, 10)) {
      std::snprintf(b, sizeof b, "  %5.1f%%  %s", 100.0 * double(c) / double(n), f.c_str());
      line(b);
    }
    for (const auto& [tid, m] : per_thread) {
      uint64_t tot = 0;
      for (const auto& [f, c] : m) tot += c;
      auto u = use.find(tid);
      std::snprintf(b, sizeof b, "thread %s (%d), %llu amostras:", u == use.end() ? "?" : u->second.name.c_str(), tid,
                    (unsigned long long)tot);
      line(b);
      for (const auto& [f, c] : TopNames(m, 10)) {
        std::snprintf(b, sizeof b, "    %5.1f%%  %s", 100.0 * double(c) / double(tot), f.c_str());
        line(b);
      }
      line("  pilhas mais frequentes (função  <  quem chamou  <  ...):");
      for (const auto& [st, c] : TopNames(stacks[tid], 8)) {
        std::snprintf(b, sizeof b, "    %5.1f%%  ", 100.0 * double(c) / double(tot));
        line(std::string(b) + st);
      }
    }
  } else {
    line("funções: nenhuma amostra de PC (as threads amostradas não estavam rodando ou o sinal falhou)");
  }

  // Na pasta logs/ (a que já se exporta com o gtav.log) e uma cópia em logs/perf (tela Logs de desempenho).
  std::error_code ec;
  const std::string name = "perf-diag-" + Timestamp() + ".txt";
  const std::string text = "# Gtavport — Medir desempenho (odin2/perf/odin2_diag.cpp)\n" + out.str();
  const std::filesystem::path logs = ExeDir() / "logs", file = logs / name;
  std::filesystem::create_directories(logs / "perf", ec);
  std::ofstream(file) << text;
  std::ofstream(logs / "perf" / name) << text;
  REXLOG_INFO("[diag] relatório: {}", file.string());
  SaveLogcat(logs / "perf" / ("logcat-" + name));
  if (g_from_file && !g_always) std::filesystem::remove(ExeDir() / "diag_request.txt", ec);
  std::lock_guard lock(g_mutex);
  g_last_report = file.string();
}

// logcat do próprio processo (o Android deixa um app ler os seus logs sem root), só as linhas do driver e do
// carregamento: Turnip/Mesa/ir3 (TU_DEBUG=perf escreve aí), o loader Vulkan, o adrenotools e crashes nativos.
// Vai para logs/perf/logcat-<relatório>.txt, que o "Exportar" da tela Logs de desempenho inclui no zip.
void SaveLogcat(const std::filesystem::path& out) {
#ifdef __ANDROID__
  char cmd[96];
  std::snprintf(cmd, sizeof cmd, "logcat -d -v threadtime --pid=%d 2>&1", int(getpid()));
  FILE* p = popen(cmd, "r");
  if (!p) return;
  std::ofstream f(out);
  f << "# logcat do processo do jogo (pid " << getpid() << "), filtrado: TU MESA ir3 freedreno vulkan adrenotools "
       "Odin2Driver DEBUG libc\n";
  static const char* const kKeys[] = {" TU",      "TU_",   "MESA",        "Mesa",   "ir3",  "freedreno", "turnip",
                                      "vulkan",   "Vulkan", "adrenotools", "Odin2Driver", " DEBUG", " libc ", "kgsl"};
  char line[2048];
  size_t kept = 0;
  while (std::fgets(line, sizeof line, p) && kept < 20000) {
    for (const char* k : kKeys)
      if (std::strstr(line, k)) {
        f << line;
        kept++;
        break;
      }
  }
  pclose(p);
  REXLOG_INFO("[diag] logcat: {} linhas em {}", kept, out.string());
#else
  (void)out;
#endif
}

// Thread da medição: espera o jogo, amostra /proc a cada 50 ms, o aparelho a cada 2 s e conduz o amostrador.
// Modo hdr: os pixel shaders do censo, do que mais desenha no alvo HDR ao que menos, cada um num bloco de
// 10 s desligado (--gtav_native_skip_ps). Um shader que custa caro some do GPU quando desligado.
std::vector<std::pair<uint64_t, uint64_t>> CensusSorted() {
  std::vector<std::pair<uint64_t, uint64_t>> v;
  {
    std::lock_guard census(g_census_mutex);
    v.assign(g_census.begin(), g_census.end());
  }
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
  return v;
}

void BuildVariants() {
  if (g_hdr) {
    g_vars = {{"como está", "", ""}};
    const auto census = CensusSorted();
    const double frames = double(std::max<uint64_t>(1, g_census_frames.load()));
    char b[96];
    for (size_t i = 0; i < census.size() && i < kHdrCandidates; i++) {
      char hx[20];
      std::snprintf(hx, sizeof hx, "%016llX", static_cast<unsigned long long>(census[i].first));
      const std::string hex = hx;
      std::snprintf(b, sizeof b, "sem ps %s (%.1f draws/quadro)", hex.c_str(), double(census[i].second) / frames);
      g_vars.push_back({b, "gtav_native_skip_ps", hex});
    }
  }
  g_ab_saved.assign(g_vars.size(), std::string());
  g_schedule.clear();
  for (int v = 0; v < int(g_vars.size()); v++) {
    if (g_hdr && v > 0 && (v - 1) % 4 == 0) g_schedule.push_back(0);  // "como está" de novo a cada 4
    if (!g_hdr || v > 0) g_schedule.push_back(v);
  }
  g_slot = 0;
  g_variant = g_schedule.empty() ? 0 : g_schedule[0];
}

void Run() {
  setpriority(PRIO_PROCESS, 0, -2);  // um pouco acima do normal: amostras a tempo (custa quase nada)
  FindClusters();
  InstallHandler();
  const int self = int(syscall(SYS_gettid));
  std::vector<ProcSnapshot> warm;
  std::map<int, ThreadUse> acc;  // estados por thread, acumulados a cada leitura
  ProcSnapshot first, last;
  size_t nsnaps = 0;
  Device dev;
  auto last_dev = Clock::now() - std::chrono::seconds(10);
  auto block_start = Clock::now();
  for (;;) {
    const State st = g_state.load();
    if (st == State::kIdle) return;
    const auto now = Clock::now();
    if (st == State::kWaiting) {
      // As últimas amostras da espera escolhem as threads a amostrar por PC.
      warm.push_back(ProcTake(false));
      while (warm.size() > 40) warm.erase(warm.begin());  // ~2 s
      if (now - g_armed_at > std::chrono::minutes(30)) {
        REXLOG_INFO("[diag] medição cancelada: 30 min sem 5 s seguidos de jogo");
        g_state = State::kIdle;
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    if (st == State::kRecording && nsnaps == 0) {
      // Começou: as kTargets threads (fora esta) que mais usaram CPU nos últimos ~2 s.
      std::map<int, ThreadUse> u;
      if (warm.size() >= 2)
        for (const auto& [tid, ts] : warm.back().threads) {
          auto a = warm.front().threads.find(tid);
          if (tid == self || a == warm.front().threads.end()) continue;
          u[tid].name = ts.comm;
          u[tid].cpu_ms = double(ts.ticks - std::min(ts.ticks, a->second.ticks));
        }
      int k = 0;
      std::string list;
      for (const auto& [tid, tu] : TopThreads(u, kTargets)) {
        g_targets[k++] = tid;
        list += " " + tu.name + "(" + std::to_string(tid) + ")";
      }
      g_target_count = k;
      g_pc_n = 0;
      g_pc_lost = 0;
      REXLOG_INFO("[diag] medindo {} s de jogo; amostrando o PC de:{}", g_seconds, list.empty() ? " nenhuma" : list);
      if (g_ab) BuildVariants();
      g_pass_flag_before = rex::cvar::GetFlagByName("gtav_native_pass_gpu_times");
      rex::cvar::SetFlagByName("gtav_native_pass_gpu_times", "true");  // GPU por passe durante a medição
      block_start = now;
      g_sampler_on = true;
    }
    if (st == State::kRecording) {
      ProcSnapshot s = ProcTake(false);
      // Amostrador em blocos de 5 s ligado / 5 s desligado; só nas threads em R (rodando ou na fila).
      if (now - block_start >= std::chrono::seconds(5)) {
        g_sampler_on = !g_sampler_on.load();
        block_start = now;
        // A/B: a cada 10 s (dois blocos) a próxima variante; volta a opção da anterior antes.
        static int blocks = 0;
        if (g_ab && (++blocks % 2) == 0 && !g_schedule.empty()) {
          const int cur = g_variant.load();
          g_slot = (g_slot + 1) % g_schedule.size();
          const int next = g_schedule[g_slot];
          if (!g_vars[cur].cvar.empty()) rex::cvar::SetFlagByName(g_vars[cur].cvar, g_ab_saved[cur]);
          if (!g_vars[next].cvar.empty()) {
            g_ab_saved[next] = rex::cvar::GetFlagByName(g_vars[next].cvar);
            rex::cvar::SetFlagByName(g_vars[next].cvar, g_vars[next].value);
          }
          g_variant = next;
        }
      }
      if (now - last_dev >= std::chrono::seconds(2)) {
        last_dev = now;
        DeviceSample d = LatestDevice();
        for (int c = 0; c < 3; c++)
          if (d.cpu_mhz[c] > 0) dev.cpu[2 - c].push_back(d.cpu_mhz[c]);  // perf: [0] prime .. [2] eficiência
        if (d.gpu_mhz > 0) dev.gpu_mhz.push_back(d.gpu_mhz);
        if (d.gpu_temp > 0) dev.temp.push_back(d.gpu_temp);
        if (d.headroom >= 0) dev.headroom.push_back(d.headroom);
        dev.thermal_max = std::max(dev.thermal_max, d.thermal_status);
        // Linha do tempo: os quadros e tempos de GPU que chegaram desde a última linha.
        std::lock_guard lock(g_mutex);
        double sum = 0, gw = 0, sub = 0, gpu = 0, worst = 0;
        size_t nf = g_frames.size() - std::min(g_tl_frame, g_frames.size());
        for (size_t i = g_tl_frame; i < g_frames.size(); i++) {
          sum += g_frames[i].interval;
          gw += g_frames[i].gpu_wait;
          sub += g_frames[i].submit;
          worst = std::max(worst, g_frames[i].interval);
        }
        size_t ng = g_gpu.size() - std::min(g_tl_gpu, g_gpu.size());
        for (size_t i = g_tl_gpu; i < g_gpu.size(); i++) gpu += g_gpu[i];
        g_tl_frame = g_frames.size();
        g_tl_gpu = g_gpu.size();
        char b[256];
        std::snprintf(b, sizeof b,
                      "  %5.0f s  %5.1f fps  pior %4.0f ms | GPU %5.1f ms | renderer espera GPU %5.1f submit %5.1f ms | "
                      "GPU %4.0f MHz uso %3.0f%% %4.0f °C | CPU prime %4.0f perf %4.0f MHz",
                      g_game_ms / 1000.0, sum > 0 ? 1000.0 * double(nf) / sum : 0, worst, ng ? gpu / double(ng) : 0,
                      nf ? gw / double(nf) : 0, nf ? sub / double(nf) : 0, d.gpu_mhz, d.gpu_busy, d.gpu_temp,
                      d.cpu_mhz[0], d.cpu_mhz[1]);
        g_timeline.push_back(b);
      }
      const bool sample = g_sampler_on.load();
      for (const auto& [tid, ts] : s.threads) {
        ThreadUse& u = acc[tid];
        u.name = ts.comm;
        u.samples++;
        if (ts.state == 'R') {
          u.r++;
          if (ts.cpu >= 0 && ts.cpu < int(g_cluster.size()) && g_cluster[ts.cpu] >= 0) u.cluster_r[g_cluster[ts.cpu]]++;
        } else if (ts.state == 'D') {
          u.d++;
        } else {
          u.s++;
        }
      }
      if (nsnaps++ == 0) first = s;
      last = std::move(s);
      // Entre duas leituras de /proc (50 ms): um sinal a cada ~4 ms para cada thread alvo em R.
      for (int i = 0; i < 12; i++) {
        if (sample) {
          for (int t = 0; t < g_target_count.load(); t++) {
            auto it = last.threads.find(g_targets[t]);
            if (it != last.threads.end() && it->second.state == 'R')
              syscall(SYS_tgkill, getpid(), g_targets[t], SIGPROF);
          }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(4000));
      }
      continue;
    }
    if (st == State::kReporting) {
      g_sampler_on = false;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));  // sinais em voo
      const double window = nsnaps >= 2 ? std::chrono::duration<double, std::milli>(last.t - first.t).count() : 1;
      rex::cvar::SetFlagByName("gtav_native_pass_gpu_times", g_pass_flag_before.empty() ? "false" : g_pass_flag_before);
      if (g_ab) {
        const int cur = g_variant.load();
        if (!g_vars[cur].cvar.empty()) rex::cvar::SetFlagByName(g_vars[cur].cvar, g_ab_saved[cur]);
        g_variant = 0;
      }
      if (g_hdr) {
        rex::cvar::SetFlagByName("gtav_native_hdr_census", "false");
        g_census_on = false;
      }
      Report(std::move(acc), first, last, nsnaps >= 2, dev, window);
      g_state = State::kIdle;
      return;
    }
  }
}

void Arm(int seconds, const char* why, bool from_file, bool always = false) {
  State expected = State::kIdle;
  if (!g_state.compare_exchange_strong(expected, State::kWaiting)) return;
  {
    std::lock_guard lock(g_mutex);
    g_from_file = from_file;
    g_always = always;
    g_seconds = seconds;
    g_warm_ms = 0;
    g_game_ms = 0;
    g_frames.clear();
    g_gpu.clear();
    g_timeline.clear();
    g_tl_frame = g_tl_gpu = 0;
    g_pass_best.clear();
    g_pass_worst.clear();
    g_pass_best_total = g_pass_worst_total = 0;
    g_ab = false;
    g_hdr = false;
    g_variant = 0;
    g_slot = 0;
    g_vars.assign(std::begin(kVariants), std::end(kVariants));
    g_schedule.clear();
    g_gpu_variant.clear();
    g_nongame_frames = 0;
    g_armed_at = Clock::now();
  }
  REXLOG_INFO("[diag] medir desempenho: {} s de jogo ({}); começa após 5 s seguidos de jogo", seconds, why);
  std::thread(Run).detach();
}

}  // namespace

void DiagRequest(int seconds) { Arm(seconds, "menu do jogo", false); }

void DiagCheckRequestFile() {
  std::error_code ec;
  const std::filesystem::path f = ExeDir() / "diag_request.txt";
  if (!std::filesystem::exists(f, ec)) return;
  // "<segundos>" (só no próximo jogo) ou "<segundos> always" (em todo início). O arquivo fica até sair um
  // relatório: um início do jogo que fecha antes não gasta o pedido.
  int seconds = 0;
  std::string mode;
  std::ifstream(f) >> seconds >> mode;
  if (seconds > 0) {
    Arm(std::min(seconds, 3600), mode == "always" ? "logs de desempenho em todo início (app)"
                                 : mode == "ab"   ? "comparar opções (A/B) no app"
                                 : mode == "hdr"  ? "isolar o passe HDR (um pixel shader por vez) no app"
                                                  : "pedido no app",
        true, mode == "always");
    std::lock_guard lock(g_mutex);
    g_ab = mode == "ab" || mode == "hdr";
    g_hdr = mode == "hdr";
    if (g_hdr) {  // o censo começa já na espera (5 s de jogo), para o rodízio ter candidatos
      {
        std::lock_guard census(g_census_mutex);
        g_census.clear();
      }
      g_census_frames = 0;
      g_census_on = true;
      rex::cvar::SetFlagByName("gtav_native_hdr_census", "true");
    }
  }
  else std::filesystem::remove(f, ec);
}

void DiagOnFrame(const FrameSample& s, bool menu_open) {
  if (g_census_on.load(std::memory_order_relaxed) && !menu_open && s.draws >= kGameplayDraws) g_census_frames++;
  const State st = g_state.load(std::memory_order_relaxed);
  if (st != State::kWaiting && st != State::kRecording) return;
  const bool game = !menu_open && s.draws >= kGameplayDraws && s.interval_ms < 5000;  // um quadro de 5 s+ é loading
  std::lock_guard lock(g_mutex);
  if (st == State::kWaiting) {
    // Jogo acumulado: o menu aberto zera; quadros leves (loading, um corte) só não contam. A ~13 fps um
    // único quadro leve zerava a espera e a medição nunca começava.
    if (menu_open) g_warm_ms = 0;
    else if (game) g_warm_ms += s.interval_ms;
    if (g_warm_ms >= kWarmupMs) {
      g_rec_start = Clock::now();
      g_state = State::kRecording;
    }
    return;
  }
  if (!game) {  // só jogo: menus (o nosso e telas com poucos draws), loading e vídeos ficam fora
    g_nongame_frames++;
    return;
  }
  g_frames.push_back({s.interval_ms, s.record_ms, s.idle_ms, s.gpu_wait_ms, s.submit_ms, s.draws, g_sampler_on.load(),
                      g_variant.load()});
  g_game_ms += s.interval_ms;
  if (g_game_ms >= g_seconds * 1000.0 || Clock::now() - g_rec_start > std::chrono::seconds(g_seconds * 3 + 30))
    g_state = State::kReporting;
}

void DiagNoteHdrDraw(uint64_t ps_hash) {
  if (!g_census_on.load(std::memory_order_relaxed)) return;
  std::lock_guard census(g_census_mutex);
  g_census[ps_hash]++;
}

void DiagOnGpu(double gpu_ms) {
  if (g_state.load(std::memory_order_relaxed) != State::kRecording || gpu_ms <= 0) return;
  std::lock_guard lock(g_mutex);
  g_gpu.push_back(gpu_ms);
  if (g_ab) g_gpu_variant.push_back({g_variant.load(), gpu_ms});
}

void DiagOnPasses(const std::vector<std::pair<std::string, double>>& passes, double total_ms) {
  if (g_state.load(std::memory_order_relaxed) != State::kRecording || passes.empty()) return;
  std::lock_guard lock(g_mutex);
  if (g_pass_best.empty() || total_ms < g_pass_best_total) {
    g_pass_best = passes;
    g_pass_best_total = total_ms;
  }
  if (g_pass_worst.empty() || total_ms > g_pass_worst_total) {
    g_pass_worst = passes;
    g_pass_worst_total = total_ms;
  }
}

std::string DiagStatus() {
  const State st = g_state.load(std::memory_order_relaxed);
  std::lock_guard lock(g_mutex);
  char b[160];
  switch (st) {
    case State::kWaiting:
      std::snprintf(b, sizeof b, "Medir desempenho: esperando 5 s seguidos de jogo (%.0f s)", g_warm_ms / 1000.0);
      return b;
    case State::kRecording:
      std::snprintf(b, sizeof b, "Medindo desempenho: %.0f de %d s", g_game_ms / 1000.0, g_seconds);
      return b;
    case State::kReporting: return "Medir desempenho: gravando o relatório...";
    default: return "";
  }
}

std::string DiagLastReport() {
  std::lock_guard lock(g_mutex);
  return g_last_report;
}

}  // namespace gtav::native::perf
