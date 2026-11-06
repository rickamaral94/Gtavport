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

#include <atomic>
#include <cerrno>
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
#include <ucontext.h>
#include <unistd.h>

namespace gtav::native::perf {

namespace {

using Clock = std::chrono::steady_clock;
using namespace diag;

constexpr double kWarmupMs = 5000;       // jogo contínuo antes de começar
constexpr uint32_t kGameplayDraws = 100;  // menos que isso: tela de loading, menu, vídeo
constexpr int kTargets = 4;               // threads amostradas por PC

enum class State { kIdle, kWaiting, kRecording, kReporting };

std::mutex g_mutex;
std::atomic<State> g_state{State::kIdle};
int g_seconds = 0;
double g_warm_ms = 0;          // jogo contínuo acumulado na espera
double g_game_ms = 0;          // jogo medido
Clock::time_point g_rec_start, g_armed_at;
std::string g_last_report;     // caminho do último relatório (para o menu)
bool g_from_file = false;      // pedido da tela Configurações: o arquivo só sai quando houver relatório
double g_armed_frames_ms = 0;  // tempo de quadros desde o pedido (sem jogo confirmado em 120 s, mede assim mesmo)
bool g_unconfirmed = false;
bool g_menu_open_hint = false;

// Quadros da medição
struct FrameRec {
  double interval, record, idle, gpu_wait;
  uint32_t draws;
  bool sampler_on;
};
std::vector<FrameRec> g_frames;
std::vector<double> g_gpu;  // tempo de GPU por quadro (chega depois)
uint64_t g_nongame_frames = 0;

// Amostras de PC (SIGPROF): anel sem lock, escrito pelo handler.
struct PcSample {
  int32_t tid;
  uint64_t pc, lr;
};
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
    g_pc[i] = {int32_t(syscall(SYS_gettid)), uint64_t(uc->uc_mcontext.pc), uint64_t(uc->uc_mcontext.regs[30])};
#elif defined(__x86_64__)
    g_pc[i] = {int32_t(syscall(SYS_gettid)), uint64_t(uc->uc_mcontext.gregs[REG_RIP]), 0};
#else
    g_pc[i] = {int32_t(syscall(SYS_gettid)), 0, 0};
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
  if (g_unconfirmed)
    line("aviso: 120 s sem 5 s de jogo confirmados (cena com " + std::to_string(kGameplayDraws) +
         "+ draws, menu fechado); medido assim mesmo, pode incluir loading ou menus");

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
    for (uint32_t i = 0; i < n; i++) {
      const PcSample& s = g_pc[i];
      const std::string& f = name_of(s.pc);
      all[f]++;
      per_thread[s.tid][f]++;
      if (s.lr) callers[name_of(s.lr)]++;
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
    }
  } else {
    line("funções: nenhuma amostra de PC (as threads amostradas não estavam rodando ou o sinal falhou)");
  }

  std::error_code ec;
  const std::filesystem::path dir = ExeDir() / "logs" / "perf";
  std::filesystem::create_directories(dir, ec);
  const std::filesystem::path file = dir / ("perf-diag-" + Timestamp() + ".txt");
  std::ofstream(file) << "# Gtavport — Medir desempenho (odin2/perf/odin2_diag.cpp)\n" << out.str();
  REXLOG_INFO("[diag] relatório: {}", file.string());
  if (g_from_file) std::filesystem::remove(ExeDir() / "diag_request.txt", ec);
  std::lock_guard lock(g_mutex);
  g_last_report = file.string();
}

// Thread da medição: espera o jogo, amostra /proc a cada 50 ms, o aparelho a cada 2 s e conduz o amostrador.
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
      block_start = now;
      g_sampler_on = true;
    }
    if (st == State::kRecording) {
      ProcSnapshot s = ProcTake(false);
      // Amostrador em blocos de 5 s ligado / 5 s desligado; só nas threads em R (rodando ou na fila).
      if (now - block_start >= std::chrono::seconds(5)) {
        g_sampler_on = !g_sampler_on.load();
        block_start = now;
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
      Report(std::move(acc), first, last, nsnaps >= 2, dev, window);
      g_state = State::kIdle;
      return;
    }
  }
}

void Arm(int seconds, const char* why, bool from_file) {
  State expected = State::kIdle;
  if (!g_state.compare_exchange_strong(expected, State::kWaiting)) return;
  {
    std::lock_guard lock(g_mutex);
    g_from_file = from_file;
    g_armed_frames_ms = 0;
    g_unconfirmed = false;
    g_seconds = seconds;
    g_warm_ms = 0;
    g_game_ms = 0;
    g_frames.clear();
    g_gpu.clear();
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
  int seconds = 0;
  std::ifstream(f) >> seconds;
  // O arquivo fica até sair um relatório: um início do jogo que fecha antes não gasta o pedido.
  if (seconds > 0) Arm(std::min(seconds, 3600), "pedido na tela Configurações do app", true);
  else std::filesystem::remove(f, ec);
}

void DiagOnFrame(const FrameSample& s, bool menu_open) {
  const State st = g_state.load(std::memory_order_relaxed);
  if (st != State::kWaiting && st != State::kRecording) return;
  const bool game = !menu_open && s.draws >= kGameplayDraws && s.interval_ms < 5000;
  std::lock_guard lock(g_mutex);
  if (st == State::kWaiting) {
    // Jogo acumulado: o menu aberto zera; quadros leves (loading, um corte) só não contam. A ~13 fps um
    // único quadro leve zerava a espera e a medição nunca começava.
    if (menu_open) g_warm_ms = 0;
    else if (game) g_warm_ms += s.interval_ms;
    if (!menu_open) g_armed_frames_ms += std::min(s.interval_ms, 1000.0);
    const bool fallback = g_armed_frames_ms >= 120000;
    if (g_warm_ms >= kWarmupMs || fallback) {
      g_unconfirmed = g_warm_ms < kWarmupMs;
      g_rec_start = Clock::now();
      g_state = State::kRecording;
    }
    return;
  }
  if (menu_open || (!game && !g_unconfirmed)) {
    g_nongame_frames++;
    return;
  }
  g_frames.push_back({s.interval_ms, s.record_ms, s.idle_ms, s.gpu_wait_ms, s.draws, g_sampler_on.load()});
  g_game_ms += s.interval_ms;
  if (g_game_ms >= g_seconds * 1000.0 || Clock::now() - g_rec_start > std::chrono::seconds(g_seconds * 3 + 30))
    g_state = State::kReporting;
}

void DiagOnGpu(double gpu_ms) {
  if (g_state.load(std::memory_order_relaxed) != State::kRecording || gpu_ms <= 0) return;
  std::lock_guard lock(g_mutex);
  g_gpu.push_back(gpu_ms);
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
