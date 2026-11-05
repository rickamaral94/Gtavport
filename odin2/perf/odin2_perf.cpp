// Gtavport — seção "Performance" do menu e gravação de sessão. Ver odin2_perf.h.
//
// Classificação do gargalo (por quadro; a mesma regra está em tools/bench/perf_report.py):
//   cap      fps limitado (gtav_fps_limit / vsync) e o intervalo já está no alvo (±5 %)
//   gpu      tempo de GPU >= 85 % do intervalo, ou espera pela fence >= 25 % do intervalo
//   render   gravação de comandos (CPU da thread de render) >= 75 % do intervalo
//   jogo     thread de render ociosa esperando o jogo >= 40 % do intervalo (CPU das threads do jogo)
//   misto    nenhum dominante
// Sinais à parte: stutter (quadro > 50 ms com shader/pipeline/textura criado nele) e térmico
// (margem térmica >= 0,85 ou clock da GPU < 70 % do maior visto na sessão).
#include "odin2_perf.h"

#include "paths.h"

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include <dlfcn.h>
#include <unistd.h>

namespace gtav::native::perf {

namespace {

constexpr int kRing = 512;     // quadros guardados (gráfico e janela de análise)
constexpr int kWindow = 120;   // janela da análise ao vivo (~4 s a 30 fps)
constexpr int kGpuLag = 6;     // quadros até o tempo de GPU chegar (2 em voo + folga)

enum Bottleneck { kCap, kGpu, kRender, kGame, kMixed, kClassCount };
const char* const kClassNames[kClassCount] = {"limite de fps/vsync", "GPU", "CPU (thread de render)",
                                              "CPU (threads do jogo)", "misto"};

struct Frame {
  uint64_t seq = 0;
  double t_ms = 0;  // desde o início da sessão de gravação (ou do processo)
  FrameSample s;
  double gpu_ms = -1;  // -1: ainda não chegou / indisponível
};

struct Sensors {
  double gpu_mhz = -1, gpu_busy = -1, gpu_temp = -1;
  double cpu_mhz[3] = {-1, -1, -1};  // prime, performance, eficiência (por política de cpufreq)
  double headroom = -1;               // AThermal_getThermalHeadroom (0 = frio, 1 = limite)
  int thermal_status = -1;            // AThermal_getCurrentThermalStatus
  double rss_mb = -1;
};

std::mutex g_mutex;
Frame g_ring[kRing];
uint64_t g_seq = 0;
Sensors g_sensors;
double g_gpu_mhz_max = 0;
std::vector<std::pair<std::string, double>> g_passes;
double g_passes_total = 0;
std::string g_device = "?";
std::string g_driver_json;  // odin2_vulkan_describe (thread de sensores)
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();

// Gravação
bool g_recording = false;
uint64_t g_rec_first = 0, g_rec_written = 0;
std::string g_rec_base, g_last_summary;
std::deque<std::string> g_csv_pending;  // linhas prontas para a thread de E/S
std::chrono::steady_clock::time_point g_rec_start;
double g_head_start = -1, g_head_max = -1, g_mhz_start = -1, g_mhz_min = 1e9;
double g_cap_target_ms = 0;

std::thread g_thread;
std::once_flag g_thread_once;

double Now() {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_start).count();
}

// ---------- sensores (thread própria) ----------
double ReadNumber(const char* path) {
  FILE* f = std::fopen(path, "r");
  if (!f) return -1;
  double v = -1;
  if (std::fscanf(f, "%lf", &v) != 1) v = -1;
  std::fclose(f);
  return v;
}

struct ThermalApi {
  void* manager = nullptr;
  float (*headroom)(void*, int) = nullptr;
  int (*status)(void*) = nullptr;
  bool tried = false;
  void Init() {
    if (tried) return;
    tried = true;
#ifdef __ANDROID__
    void* lib = dlopen("libandroid.so", RTLD_NOW);
    if (!lib) return;
    auto acquire = reinterpret_cast<void* (*)()>(dlsym(lib, "AThermal_acquireManager"));  // API 30
    headroom = reinterpret_cast<float (*)(void*, int)>(dlsym(lib, "AThermal_getThermalHeadroom"));  // API 31
    status = reinterpret_cast<int (*)(void*)>(dlsym(lib, "AThermal_getCurrentThermalStatus"));
    if (acquire) manager = acquire();
#endif
  }
} g_thermal;

// Políticas de cpufreq agrupadas por frequência máxima: maior = prime, depois performance, eficiência.
std::vector<std::pair<long, int>> g_policies;  // (freq máx, cpu representante)
void FindPolicies() {
  std::vector<std::pair<long, int>> seen;
  for (int c = 0; c < 16; c++) {
    char p[96];
    std::snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", c);
    double f = ReadNumber(p);
    if (f <= 0) continue;
    bool dup = false;
    for (auto& s : seen) dup |= s.first == long(f);
    if (!dup) seen.push_back({long(f), c});
  }
  std::sort(seen.rbegin(), seen.rend());
  g_policies = seen;
}

Sensors ReadSensors() {
  Sensors s;
  // GPU (kgsl, Adreno). Cada arquivo pode estar bloqueado pelo SELinux: fica -1 = indisponível.
  double mhz = ReadNumber("/sys/class/kgsl/kgsl-3d0/clock_mhz");
  if (mhz <= 0) {
    double hz = ReadNumber("/sys/class/kgsl/kgsl-3d0/gpuclk");
    if (hz > 0) mhz = hz / 1e6;
  }
  s.gpu_mhz = mhz;
  s.gpu_busy = ReadNumber("/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage");
  double t = ReadNumber("/sys/class/kgsl/kgsl-3d0/temp");
  s.gpu_temp = t > 1000 ? t / 1000.0 : t;
  for (size_t i = 0; i < g_policies.size() && i < 3; i++) {
    char p[96];
    std::snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", g_policies[i].second);
    double f = ReadNumber(p);
    s.cpu_mhz[i] = f > 0 ? f / 1000.0 : -1;
  }
  g_thermal.Init();
  if (g_thermal.manager && g_thermal.headroom) {
    float h = g_thermal.headroom(g_thermal.manager, 0);
    s.headroom = std::isnan(h) ? -1 : h;
  }
  if (g_thermal.manager && g_thermal.status) s.thermal_status = g_thermal.status(g_thermal.manager);
  FILE* f = std::fopen("/proc/self/statm", "r");
  if (f) {
    long size, rss;
    if (std::fscanf(f, "%ld %ld", &size, &rss) == 2) s.rss_mb = rss * double(sysconf(_SC_PAGESIZE)) / 1048576.0;
    std::fclose(f);
  }
  return s;
}

std::string DriverJson() {
#ifdef __ANDROID__
  if (void* lib = dlopen("libodin2driver.so", RTLD_NOW)) {
    auto describe = reinterpret_cast<const char* (*)()>(dlsym(lib, "odin2_vulkan_describe"));
    auto name = reinterpret_cast<const char* (*)()>(dlsym(lib, "odin2_vulkan_driver_name"));
    std::string j = describe ? describe() : "{}";
    if (name && j.size() > 1 && j.back() == '}') j = j.substr(0, j.size() - 1) + ",\"selected\":\"" + name() + "\"}";
    return j;
  }
#endif
  return "{\"ok\":false,\"error\":\"sem libodin2driver.so (APK do upstream)\"}";
}

std::string JsonField(const std::string& j, const char* key) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = j.find(k);
  if (p == std::string::npos) return "";
  p += k.size();
  if (p < j.size() && j[p] == '"') {
    size_t e = j.find('"', p + 1);
    return e == std::string::npos ? "" : j.substr(p + 1, e - p - 1);
  }
  size_t e = j.find_first_of(",}", p);
  return j.substr(p, e - p);
}

void IoLoop();

void EnsureThread() {
  std::call_once(g_thread_once, [] {
    g_thread = std::thread(IoLoop);
    g_thread.detach();
  });
}

// ---------- análise ----------
double CapTargetMs() {
  int limit = std::atoi(rex::cvar::GetFlagByName("gtav_fps_limit").c_str());
  if (limit > 0) return 1000.0 / limit;
  return 0;  // sem limite explícito (vsync sozinho depende da tela: não conta como alvo)
}

int Classify(const Frame& f, double cap_ms) {
  const double T = std::max(1e-3, f.s.interval_ms);
  if (cap_ms > 0 && std::abs(T - cap_ms) <= 0.05 * cap_ms) return kCap;
  if ((f.gpu_ms >= 0 && f.gpu_ms >= 0.85 * T) || f.s.gpu_wait_ms >= 0.25 * T) return kGpu;
  if (f.s.record_ms >= 0.75 * T) return kRender;
  if (f.s.idle_ms >= 0.40 * T) return kGame;
  return kMixed;
}

struct Stats {
  int n = 0;
  double fps = 0, low1 = 0, p50 = 0, p99 = 0, record = 0, gpu = 0, gpu_wait = 0, idle = 0;
  int hitches = 0, stutter = 0;
  int classes[kClassCount] = {};
};

Stats Analyze(const std::vector<Frame>& v, double cap_ms) {
  Stats st;
  std::vector<double> ft;
  double gpu_sum = 0;
  int gpu_n = 0;
  for (const auto& f : v) {
    if (f.s.interval_ms <= 0 || f.s.interval_ms > 1000) continue;
    ft.push_back(f.s.interval_ms);
    st.record += f.s.record_ms;
    st.gpu_wait += f.s.gpu_wait_ms;
    st.idle += f.s.idle_ms;
    if (f.gpu_ms >= 0) gpu_sum += f.gpu_ms, gpu_n++;
    if (f.s.interval_ms > 50) {
      st.hitches++;
      if (f.s.new_pipelines || f.s.miss_ms > 5) st.stutter++;
    }
    st.classes[Classify(f, cap_ms)]++;
  }
  st.n = int(ft.size());
  if (!st.n) return st;
  double total = 0;
  for (double x : ft) total += x;
  st.fps = 1000.0 * st.n / total;
  std::vector<double> s = ft;
  std::sort(s.begin(), s.end());
  auto pct = [&](double p) {
    double k = (s.size() - 1) * p / 100.0;
    size_t lo = size_t(std::floor(k)), hi = size_t(std::ceil(k));
    return s[lo] + (s[hi] - s[lo]) * (k - lo);
  };
  st.p50 = pct(50);
  st.p99 = pct(99);
  int nl = std::max(1, st.n / 100);
  double worst = 0;
  for (int i = 0; i < nl; i++) worst += s[s.size() - 1 - i];
  st.low1 = 1000.0 / (worst / nl);
  st.record /= st.n;
  st.gpu_wait /= st.n;
  st.idle /= st.n;
  st.gpu = gpu_n ? gpu_sum / gpu_n : -1;
  return st;
}

std::vector<Frame> LastFrames(int n, bool need_gpu) {  // com g_mutex
  std::vector<Frame> v;
  uint64_t end = g_seq > (need_gpu ? kGpuLag : 0) ? g_seq - (need_gpu ? kGpuLag : 0) : 0;
  uint64_t begin = end > uint64_t(n) ? end - n : 0;
  for (uint64_t q = begin + 1; q <= end; q++) {
    const Frame& f = g_ring[q % kRing];
    if (f.seq == q) v.push_back(f);
  }
  return v;
}

std::string Fmt(double v, const char* unit, int dec = 1) {
  if (v < 0) return "indisp.";
  char b[48];
  std::snprintf(b, sizeof b, "%.*f%s", dec, v, unit);
  return b;
}

// ---------- gravação ----------
std::string Timestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char b[32];
  std::strftime(b, sizeof b, "%Y%m%d_%H%M%S", &tm);
  return b;
}

constexpr const char* kCsvHeader =
    "seq,t_ms,interval_ms,record_ms,gpu_ms,gpu_wait_ms,idle_ms,acquire_ms,submit_ms,draws,new_pipelines,textures,"
    "miss_ms,class,gpu_mhz,gpu_busy,gpu_temp_c,cpu_prime_mhz,cpu_perf_mhz,cpu_eff_mhz,thermal_headroom,"
    "thermal_status,rss_mb";

std::string CsvRow(const Frame& f, const Sensors& s, double cap_ms) {
  char b[512];
  std::snprintf(b, sizeof b,
                "%llu,%.1f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%.2f,%s,%.0f,%.0f,%.1f,%.0f,%.0f,%.0f,%.3f,%d,%.0f",
                (unsigned long long)f.seq, f.t_ms, f.s.interval_ms, f.s.record_ms, f.gpu_ms, f.s.gpu_wait_ms, f.s.idle_ms,
                f.s.acquire_ms, f.s.submit_ms, f.s.draws, f.s.new_pipelines, f.s.textures, f.s.miss_ms,
                kClassNames[Classify(f, cap_ms)], s.gpu_mhz, s.gpu_busy, s.gpu_temp, s.cpu_mhz[0], s.cpu_mhz[1],
                s.cpu_mhz[2], s.headroom, s.thermal_status, s.rss_mb);
  return b;
}

std::filesystem::path PerfDir() { return ExeDir() / "logs" / "perf"; }

std::string Settings() {
  auto g = [](const char* n) { return rex::cvar::GetFlagByName(n); };
  std::string s;
  s += "fps_limit=" + g("gtav_fps_limit") + " vsync=" + g("gtav_native_vsync");
  s += " render_scale=" + g("gtav_native_render_scale");
  s += " antialias=" + g("odin2_antialias") + " upscaler=" + g("odin2_upscaler");
  s += " async_pipelines=" + g("gtav_native_async_pipelines") + " gpu_lag_ms=" + g("gpu_skip_rendering_lag_ms");
  return s;
}

std::string EnvLine() {
  std::string s;
  for (const char* k : {"ODIN2_VK_DRIVER", "TU_DEBUG", "MESA_VK_ABORT_ON_DEVICE_LOSS", "FD_DEV_FEATURES"})
    if (const char* v = std::getenv(k)) s += std::string(k) + "=" + v + " ";
  return s.empty() ? "(nenhuma)" : s;
}

std::string Summary(const std::vector<Frame>& v, double secs, double cap_ms) {
  Stats st = Analyze(v, cap_ms);
  std::string o;
  char b[512];
  o += "# Gtavport — sessão de performance\n";
  o += "aparelho: " + g_device + "\n";
  o += "driver: " + JsonField(g_driver_json, "selected") + " | " + JsonField(g_driver_json, "driverName") + " " +
       JsonField(g_driver_json, "driverInfo") + " | Vulkan " + JsonField(g_driver_json, "api") + "\n";
  o += "variáveis do driver: " + EnvLine() + "\n";
  o += "opções: " + Settings() + "\n";
  std::snprintf(b, sizeof b, "duração: %.1f s, %d quadros\n\n", secs, st.n);
  o += b;
  std::snprintf(b, sizeof b,
                "fps médio %.2f | 1%% low %.2f | frametime p50 %.2f ms, p99 %.2f ms | engasgos > 50 ms: %d (%d com "
                "shader/pipeline novo)\n",
                st.fps, st.low1, st.p50, st.p99, st.hitches, st.stutter);
  o += b;
  std::snprintf(b, sizeof b, "por quadro: gravação CPU %.2f ms | GPU %s | espera da GPU %.2f ms | esperando o jogo %.2f ms\n\n",
                st.record, Fmt(st.gpu, " ms", 2).c_str(), st.gpu_wait, st.idle);
  o += b;
  o += "gargalo provável (fração dos quadros):\n";
  int best = 0;
  for (int c = 0; c < kClassCount; c++) {
    if (st.classes[c] > st.classes[best]) best = c;
    std::snprintf(b, sizeof b, "  %-24s %5.1f %%\n", kClassNames[c], st.n ? 100.0 * st.classes[c] / st.n : 0.0);
    o += b;
  }
  o += std::string("=> dominante: ") + kClassNames[best] + "\n\n";
  std::snprintf(b, sizeof b, "térmico: margem início %s, máx %s | clock GPU início %s, mín %s, máx da sessão %s\n",
                Fmt(g_head_start, "", 2).c_str(), Fmt(g_head_max, "", 2).c_str(), Fmt(g_mhz_start, " MHz", 0).c_str(),
                Fmt(g_mhz_min < 1e9 ? g_mhz_min : -1, " MHz", 0).c_str(), Fmt(g_gpu_mhz_max > 0 ? g_gpu_mhz_max : -1, " MHz", 0).c_str());
  o += b;
  if (g_mhz_min < 1e9 && g_gpu_mhz_max > 0 && g_mhz_min < 0.7 * g_gpu_mhz_max)
    o += "  ATENÇÃO: clock da GPU caiu abaixo de 70 % do máximo: throttling térmico provável.\n";
  if (g_head_max >= 0.85) o += "  ATENÇÃO: margem térmica chegou a >= 0,85 (perto do limite do sistema).\n";
  if (!g_passes.empty()) {
    std::snprintf(b, sizeof b, "\nGPU por tipo de passe (últimos 300 quadros, total %.2f ms):\n", g_passes_total);
    o += b;
    for (size_t i = 0; i < g_passes.size() && i < 15; i++) {
      std::snprintf(b, sizeof b, "  %6.2f ms  %s\n", g_passes[i].second, g_passes[i].first.c_str());
      o += b;
    }
  }
  o += "\nindisponível: ";
  bool any = false;
  auto un = [&](bool cond, const char* name) {
    if (cond) o += std::string(any ? ", " : "") + name, any = true;
  };
  un(g_sensors.gpu_mhz < 0, "clock da GPU");
  un(g_sensors.gpu_busy < 0, "uso da GPU");
  un(g_sensors.gpu_temp < 0, "temperatura da GPU");
  un(g_sensors.cpu_mhz[0] < 0, "clock das CPUs");
  un(g_sensors.headroom < 0, "margem térmica");
  un(st.gpu < 0, "tempo de GPU");
  if (!any) o += "nada";
  o += "\n";
  return o;
}

void StartRecording() {  // com g_mutex
  std::error_code ec;
  std::filesystem::create_directories(PerfDir(), ec);
  g_rec_base = (PerfDir() / ("perf_" + Timestamp())).string();
  g_recording = true;
  g_rec_first = g_seq + 1;
  g_rec_written = g_seq;
  g_rec_start = std::chrono::steady_clock::now();
  g_head_start = g_sensors.headroom;
  g_head_max = g_sensors.headroom;
  g_mhz_start = g_sensors.gpu_mhz;
  g_mhz_min = g_sensors.gpu_mhz > 0 ? g_sensors.gpu_mhz : 1e9;
  g_cap_target_ms = CapTargetMs();
  g_csv_pending.clear();
  g_csv_pending.push_back(kCsvHeader);
  REXLOG_INFO("[odin2] gravação de performance iniciada: {}.csv", g_rec_base);
}

void StopRecording() {  // com g_mutex
  if (!g_recording) return;
  g_recording = false;
  // Quadros ainda não escritos (os últimos podem estar sem o tempo de GPU: ficam -1). O resumo da
  // sessão inteira vem do CSV, que a thread de E/S termina de escrever e então resume (IoLoop).
  for (uint64_t q = std::max(g_rec_written + 1, g_rec_first); q <= g_seq; q++) {
    const Frame& f = g_ring[q % kRing];
    if (f.seq == q) g_csv_pending.push_back(CsvRow(f, g_sensors, g_cap_target_ms));
    g_rec_written = q;
  }
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_rec_start).count();
  g_csv_pending.push_back("#END " + std::to_string(secs));
}

// Lê o CSV gravado e escreve o resumo da sessão inteira (thread de E/S).
void WriteSummaryFromCsv(const std::string& base, double secs, double cap_ms) {
  std::ifstream in(base + ".csv");
  std::string line;
  std::vector<Frame> v;
  std::getline(in, line);  // cabeçalho
  while (std::getline(in, line)) {
    Frame f;
    double gpu;
    unsigned long long seq;
    if (std::sscanf(line.c_str(), "%llu,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%u,%u,%u,%lf", &seq, &f.t_ms, &f.s.interval_ms,
                    &f.s.record_ms, &gpu, &f.s.gpu_wait_ms, &f.s.idle_ms, &f.s.acquire_ms, &f.s.submit_ms, &f.s.draws,
                    &f.s.new_pipelines, &f.s.textures, &f.s.miss_ms) == 13) {
      f.seq = seq;
      f.gpu_ms = gpu;
      v.push_back(f);
    }
  }
  std::string text;
  {
    std::lock_guard lock(g_mutex);
    text = Summary(v, secs, cap_ms);
    g_last_summary = base + ".txt";
  }
  std::ofstream(base + ".txt") << text;
  REXLOG_INFO("[odin2] gravação de performance: {}.txt ({} quadros)", base, v.size());
}

void IoLoop() {
  FindPolicies();
  {
    std::string j = DriverJson();
    std::lock_guard lock(g_mutex);
    g_driver_json = j;
  }
  std::ofstream csv;
  std::string open_base;
  for (;;) {
    Sensors s = ReadSensors();
    std::vector<std::string> lines;
    std::string base;
    double cap_ms;
    {
      std::lock_guard lock(g_mutex);
      g_sensors = s;
      if (s.gpu_mhz > g_gpu_mhz_max) g_gpu_mhz_max = s.gpu_mhz;
      if (g_recording) {
        if (s.headroom > g_head_max) g_head_max = s.headroom;
        if (s.gpu_mhz > 0 && s.gpu_mhz < g_mhz_min) g_mhz_min = s.gpu_mhz;
        // Quadros cujo tempo de GPU já chegou viram linhas do CSV.
        uint64_t ready = g_seq > kGpuLag ? g_seq - kGpuLag : 0;
        for (uint64_t q = std::max(g_rec_written + 1, g_rec_first); q <= ready; q++) {
          const Frame& f = g_ring[q % kRing];
          if (f.seq == q) g_csv_pending.push_back(CsvRow(f, s, g_cap_target_ms));
          g_rec_written = q;
        }
      }
      lines.assign(g_csv_pending.begin(), g_csv_pending.end());
      g_csv_pending.clear();
      base = g_rec_base;
      cap_ms = g_cap_target_ms;
    }
    for (const auto& l : lines) {
      if (l == kCsvHeader) {
        csv.close();
        csv.open(base + ".csv");
        open_base = base;
      }
      if (l.rfind("#END ", 0) == 0) {
        csv.close();
        WriteSummaryFromCsv(open_base, std::atof(l.c_str() + 5), cap_ms);
        continue;
      }
      if (csv.is_open()) csv << l << '\n';
    }
    if (csv.is_open()) csv.flush();
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

}  // namespace

uint64_t OnFrame(const FrameSample& s) {
  EnsureThread();
  std::lock_guard lock(g_mutex);
  uint64_t seq = ++g_seq;
  Frame& f = g_ring[seq % kRing];
  f.seq = seq;
  f.s = s;
  f.gpu_ms = -1;
  f.t_ms = g_recording ? std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - g_rec_start).count()
                       : Now();
  return seq;
}

void OnGpuFrame(uint64_t seq, double gpu_ms) {
  std::lock_guard lock(g_mutex);
  Frame& f = g_ring[seq % kRing];
  if (f.seq == seq) f.gpu_ms = gpu_ms;
}

void SetPassBreakdown(std::vector<std::pair<std::string, double>> passes, double total_ms) {
  std::lock_guard lock(g_mutex);
  g_passes = std::move(passes);
  g_passes_total = total_ms;
}

void SetDeviceName(const std::string& name) {
  std::lock_guard lock(g_mutex);
  g_device = name;
}

void BuildMenu(float ui) {
  EnsureThread();
  if (!ImGui::CollapsingHeader("Performance (gargalos, sensores, driver, gravação)")) return;
  std::lock_guard lock(g_mutex);
  const double cap_ms = CapTargetMs();
  std::vector<Frame> win = LastFrames(kWindow, true);
  Stats st = Analyze(win, cap_ms);

  ImGui::Text("Driver: %s  |  %s %s  |  Vulkan %s", JsonField(g_driver_json, "selected").c_str(),
              JsonField(g_driver_json, "driverName").c_str(), JsonField(g_driver_json, "driverInfo").c_str(),
              JsonField(g_driver_json, "api").c_str());
  ImGui::TextDisabled("GPU: %s  |  variáveis: %s", g_device.c_str(), EnvLine().c_str());
  ImGui::Separator();
  ImGui::Text("fps %.1f   1%% low %.1f   p99 %.1f ms   engasgos >50 ms: %d (%d de shader)", st.fps, st.low1, st.p99,
              st.hitches, st.stutter);
  ImGui::Text("Por quadro: CPU render %.1f ms | GPU %s | espera GPU %.1f ms | espera jogo %.1f ms", st.record,
              Fmt(st.gpu, " ms").c_str(), st.gpu_wait, st.idle);

  // Gráfico: frametime (barras) dos últimos quadros; linha de referência no alvo.
  std::vector<Frame> all = LastFrames(240, false);
  std::vector<float> ft(all.size()), gpu(all.size());
  for (size_t i = 0; i < all.size(); i++) {
    ft[i] = float(all[i].s.interval_ms);
    gpu[i] = float(std::max(0.0, all[i].gpu_ms));
  }
  char overlay[64];
  std::snprintf(overlay, sizeof overlay, "frametime (alvo %s)", cap_ms > 0 ? Fmt(cap_ms, " ms").c_str() : "livre");
  if (!ft.empty())
    ImGui::PlotLines("##ft", ft.data(), int(ft.size()), 0, overlay, 0.0f, 70.0f, ImVec2(480.0f * ui, 70.0f * ui));
  if (!gpu.empty())
    ImGui::PlotLines("##gpu", gpu.data(), int(gpu.size()), 0, "tempo de GPU (ms)", 0.0f, 70.0f, ImVec2(480.0f * ui, 50.0f * ui));

  int best = 0;
  for (int c = 0; c < kClassCount; c++)
    if (st.classes[c] > st.classes[best]) best = c;
  ImGui::Text("Gargalo provável: %s", st.n ? kClassNames[best] : "—");
  for (int c = 0; c < kClassCount; c++) {
    if (!st.n) break;
    ImGui::SameLine();
    ImGui::TextDisabled("%s %.0f%%", c == kCap ? "cap" : c == kGpu ? "GPU" : c == kRender ? "render" : c == kGame ? "jogo" : "misto",
                        100.0 * st.classes[c] / st.n);
  }
  const Sensors& s = g_sensors;
  bool thermal = (s.headroom >= 0.85) || (s.gpu_mhz > 0 && g_gpu_mhz_max > 0 && s.gpu_mhz < 0.7 * g_gpu_mhz_max);
  if (thermal) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Térmico: clock da GPU caindo / margem térmica alta");
  if (st.stutter) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Stutter de shader/pipeline na janela atual");
  ImGui::Separator();
  ImGui::Text("GPU %s (máx visto %s)  uso %s  %s", Fmt(s.gpu_mhz, " MHz", 0).c_str(),
              Fmt(g_gpu_mhz_max > 0 ? g_gpu_mhz_max : -1, " MHz", 0).c_str(), Fmt(s.gpu_busy, " %", 0).c_str(),
              Fmt(s.gpu_temp, " °C").c_str());
  ImGui::Text("CPU prime %s  perf %s  efic %s", Fmt(s.cpu_mhz[0], " MHz", 0).c_str(), Fmt(s.cpu_mhz[1], " MHz", 0).c_str(),
              Fmt(s.cpu_mhz[2], " MHz", 0).c_str());
  ImGui::Text("Margem térmica %s (1,0 = limite)  status %d  RAM do jogo %s", Fmt(s.headroom, "", 2).c_str(),
              s.thermal_status, Fmt(s.rss_mb, " MB", 0).c_str());

  bool pass_times = rex::cvar::GetFlagByName("gtav_native_pass_gpu_times") == "true";
  if (ImGui::Checkbox("GPU por tipo de passe (custo pequeno; atualiza a cada 300 quadros)", &pass_times))
    rex::cvar::SetFlagByName("gtav_native_pass_gpu_times", pass_times ? "true" : "false");
  if (pass_times && !g_passes.empty()) {
    ImGui::TextDisabled("total %.2f ms por quadro:", g_passes_total);
    for (size_t i = 0; i < g_passes.size() && i < 8; i++)
      ImGui::TextDisabled("  %6.2f ms  %s", g_passes[i].second, g_passes[i].first.c_str());
  }
  ImGui::Separator();
  if (!g_recording) {
    if (ImGui::Button("Gravar sessão")) StartRecording();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("CSV por quadro + resumo em logs/perf/ (exportável pela tela \"Logs de desempenho\" do app).\n"
                        "Para comparar: mesma cena, mesma temperatura inicial, 3 gravações (docs/baseline.md).");
  } else {
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - g_rec_start).count();
    if (ImGui::Button("Parar gravação")) StopRecording();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "● gravando %.0f s", secs);
  }
  if (!g_last_summary.empty()) ImGui::TextDisabled("Último resumo: %s", g_last_summary.c_str());
}

}  // namespace gtav::native::perf
