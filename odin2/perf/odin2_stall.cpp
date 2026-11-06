// Gtavport — sonda de travadas (odin2/perf). Mede antes de otimizar: quando um quadro demora, o que o
// processo fazia nesse intervalo?
//
// Uma thread própria (núcleos pequenos, prioridade baixa) lê /proc/self/task/*/stat a cada 50 ms e
// guarda ~4 s de amostras: tempo de CPU e estado (R rodando, S dormindo, D esperando disco) de cada
// thread, mais /proc/self/io (bytes lidos), faltas de página maiores e a frequência dos núcleos.
//   - Travada (OnHitch, quadro >= 150 ms): uma linha "[stall]" com as threads que mais usaram CPU no
//     intervalo, as que ficaram em D, MB lidos, faltas de página e as frequências.
//   - A cada 10 s: "[stall] cpu por thread" com o uso médio das que mais consomem.
// Só lê arquivos do próprio processo e do sysfs da CPU; nada de root.
#include "odin2_perf.h"
#include "odin2_procstat.h"

#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <dirent.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

namespace gtav::native::perf {

namespace {

using Snapshot = ProcSnapshot;

std::mutex g_mutex;
std::condition_variable g_cv;
std::deque<Snapshot> g_ring;  // ~4 s
struct Request {
  double ms;
  uint64_t frame;
  std::chrono::steady_clock::time_point at;
};
std::deque<Request> g_requests;
bool g_started = false;
long g_tick_hz = 100;

Snapshot Take() { return ProcTake(); }

// Uso de CPU por thread entre duas amostras: "nome(tid) N ms" das maiores, e quem passou por D.
std::string Describe(const Snapshot& a, const Snapshot& b, size_t top, bool with_d) {
  struct Use {
    int tid;
    const ThreadStat* ts;
    double ms;
  };
  std::vector<Use> uses;
  double total = 0;
  for (const auto& [tid, ts] : b.threads) {
    auto it = a.threads.find(tid);
    uint64_t before = it == a.threads.end() ? 0 : it->second.ticks;
    double ms = double(ts.ticks - std::min(ts.ticks, before)) * 1000.0 / double(g_tick_hz);
    total += ms;
    uses.push_back({tid, &ts, ms});
  }
  std::sort(uses.begin(), uses.end(), [](const Use& x, const Use& y) { return x.ms > y.ms; });
  const double span = std::chrono::duration<double, std::milli>(b.t - a.t).count();
  std::string out = "processo " + std::to_string(int(total)) + " ms de CPU em " + std::to_string(int(span)) + " ms;";
  for (size_t i = 0; i < uses.size() && i < top; i++) {
    if (uses[i].ms <= 0) break;
    char line[96];
    std::snprintf(line, sizeof(line), " %s(%d) %.0f", uses[i].ts->comm.c_str(), uses[i].tid, uses[i].ms);
    out += line;
  }
  (void)with_d;
  return out;
}

void Worker() {
  // Fora do caminho do jogo: núcleos pequenos, prioridade baixa.
  cpu_set_t set;
  CPU_ZERO(&set);
  for (int c = 0; c < 3; c++) CPU_SET(c, &set);
  sched_setaffinity(0, sizeof(set), &set);
  setpriority(PRIO_PROCESS, 0, 10);
  g_tick_hz = sysconf(_SC_CLK_TCK) > 0 ? sysconf(_SC_CLK_TCK) : 100;
  auto last_summary = std::chrono::steady_clock::now();
  Snapshot summary_base = Take();
  auto last_stall_log = std::chrono::steady_clock::time_point{};
  uint32_t stall_logs = 0;
  for (;;) {
    Snapshot s = Take();
    std::deque<Request> reqs;
    {
      std::unique_lock lock(g_mutex);
      g_ring.push_back(s);
      while (!g_ring.empty() && s.t - g_ring.front().t > std::chrono::seconds(4)) g_ring.pop_front();
      reqs.swap(g_requests);
    }
    for (const Request& r : reqs) {
      // No máximo uma linha por segundo (e as 200 primeiras sempre que espaçadas): o log não pode
      // virar o gargalo.
      if (r.at - last_stall_log < std::chrono::seconds(1)) continue;
      last_stall_log = r.at;
      stall_logs++;
      auto start = r.at - std::chrono::microseconds(int64_t(r.ms * 1000.0));
      const Snapshot* a = nullptr;
      std::vector<const Snapshot*> window;
      {
        std::lock_guard lock(g_mutex);
        for (const Snapshot& x : g_ring) {
          if (x.t <= start) a = &x;
          else if (x.t <= r.at + std::chrono::milliseconds(60)) window.push_back(&x);
        }
        if (!a && !g_ring.empty()) a = &g_ring.front();
        const Snapshot& b = window.empty() ? g_ring.back() : *window.back();
        if (!a) continue;
        // Threads vistas em D (disco) ou R (querendo CPU) nas amostras do intervalo.
        std::map<int, std::pair<int, int>> dr;  // tid -> (D, R)
        int samples = 0, mhz_min[3] = {1 << 30, 1 << 30, 1 << 30}, mhz_max[3] = {-1, -1, -1};
        for (const Snapshot* x : window) {
          samples++;
          for (const auto& [tid, ts] : x->threads) {
            if (ts.state == 'D') dr[tid].first++;
            if (ts.state == 'R') dr[tid].second++;
          }
          for (int c = 0; c < 3; c++) {
            if (x->mhz[c] < 0) continue;
            mhz_min[c] = std::min(mhz_min[c], x->mhz[c]);
            mhz_max[c] = std::max(mhz_max[c], x->mhz[c]);
          }
        }
        std::string d_list;
        int runnable_max = 0;
        for (const Snapshot* x : window) {
          int n = 0;
          for (const auto& [tid, ts] : x->threads) n += ts.state == 'R';
          runnable_max = std::max(runnable_max, n);
        }
        for (const auto& [tid, c] : dr) {
          if (c.first == 0) continue;
          auto it = b.threads.find(tid);
          d_list += " " + (it == b.threads.end() ? std::string("?") : it->second.comm) + "(" + std::to_string(tid) +
                    ")x" + std::to_string(c.first);
        }
        char freq[160] = "";
        if (mhz_max[2] >= 0)
          std::snprintf(freq, sizeof(freq), "; MHz pequeno %d-%d médio %d-%d prime %d-%d", mhz_min[0], mhz_max[0],
                        mhz_min[1], mhz_max[1], mhz_min[2], mhz_max[2]);
        REXLOG_INFO("[stall] quadro de {:.0f} ms (guest frame {}): {}; em disco (D):{}; até {} threads rodando; "
                    "lidos {:.1f} MB, {} faltas de página{} ({} amostras)",
                    r.ms, r.frame, Describe(*a, b, 6, true), d_list.empty() ? " nenhuma" : d_list, runnable_max,
                    double(b.read_bytes - std::min(b.read_bytes, a->read_bytes)) / 1048576.0,
                    b.majflt - std::min(b.majflt, a->majflt), freq, samples);
      }
    }
    if (s.t - last_summary >= std::chrono::seconds(10)) {
      REXLOG_INFO("[stall] cpu por thread nos últimos 10 s: {}; lidos {:.1f} MB, {} faltas de página; MHz {} / {} / {}",
                  Describe(summary_base, s, 10, false),
                  double(s.read_bytes - std::min(s.read_bytes, summary_base.read_bytes)) / 1048576.0,
                  s.majflt - std::min(s.majflt, summary_base.majflt), s.mhz[0], s.mhz[1], s.mhz[2]);
      summary_base = s;
      last_summary = s.t;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

}  // namespace

void StartStallProbe() {
  DiagCheckRequestFile();  // pedido de medição deixado pela tela Configurações do app
  std::lock_guard lock(g_mutex);
  if (g_started) return;
  g_started = true;
  std::thread(Worker).detach();
}

void OnHitch(double ms, uint64_t guest_frame) {
  if (ms < 150) return;
  std::lock_guard lock(g_mutex);
  if (!g_started) return;
  if (g_requests.size() < 8) g_requests.push_back({ms, guest_frame, std::chrono::steady_clock::now()});
}

}  // namespace gtav::native::perf
