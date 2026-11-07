// Testes no host do "Medir desempenho" (odin2/perf): contas, leitura de /proc e símbolos ELF.
//   odin2/perf/test/run.sh
#include "odin2_diag_stats.h"
#include "odin2_elfsym.h"
#include "odin2_procstat.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <chrono>
#include <thread>

#include <dlfcn.h>
#include <unistd.h>

using namespace gtav::native::perf;
using namespace gtav::native::perf::diag;

static int g_fail = 0;
#define CHECK(c)                                                       \
  do {                                                                 \
    if (!(c)) {                                                        \
      std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #c); \
      g_fail++;                                                        \
    }                                                                  \
  } while (0)
static bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

extern "C" __attribute__((noinline)) void sub_82001234(volatile int* x) { *x += 1; }
extern "C" __attribute__((noinline)) void sub_8200ABCD(volatile int* x) { *x += 2; }

int main(int argc, char** argv) {
  // Percentis (como o numpy "linear").
  CHECK(Near(Percentile({}, 50), 0));
  CHECK(Near(Percentile({5}, 99), 5));
  CHECK(Near(Percentile({1, 2, 3, 4}, 50), 2.5));
  CHECK(Near(Percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 90), 9.1));
  CHECK(Near(Percentile({3, 1, 2}, 100), 3));
  CHECK(Near(Percentile({3, 1, 2}, 0), 1));
  CHECK(Near(Mean({2, 4}), 3));

  // Fila sem núcleo: R amostrado menos a CPU usada, nunca negativo.
  ThreadUse t;
  t.samples = 100;
  t.r = 80;
  t.cpu_ms = 500;  // 50% de 1000 ms
  CHECK(Near(QueuedFraction(t, 1000), 0.30));
  t.cpu_ms = 900;
  CHECK(Near(QueuedFraction(t, 1000), 0.0));

  // Clusters: Snapdragon 8 Gen 2 (3 eficiência, 2+2 performance em dois níveis, 1 prime).
  std::vector<int> c = ClustersFromMaxFreq({2016000, 2016000, 2016000, 2803200, 2803200, 2803200, 2803200, 3360000,
                                            -1, -1, -1, -1, -1, -1, -1, -1});
  CHECK(c[0] == 0 && c[2] == 0 && c[3] == 1 && c[6] == 1 && c[7] == 2 && c[8] == -1);
  c = ClustersFromMaxFreq({1800000, 2400000, 2700000, 3200000});  // quatro níveis: os do meio são performance
  CHECK(c[0] == 0 && c[1] == 1 && c[2] == 1 && c[3] == 2);
  c = ClustersFromMaxFreq({2000000, 2000000});
  CHECK(c[0] == 1 && c[1] == 1);

  // Ordenação e nomes.
  std::map<std::string, uint64_t> names = {{"b", 5}, {"a", 5}, {"c", 9}};
  auto top = TopNames(names, 2);
  CHECK(top.size() == 2 && top[0].first == "c" && top[1].first == "a");
  CHECK(BaseName("/data/app/x/lib/arm64/libgtav_fast.so") == "libgtav_fast.so");
  CHECK(ModuleOffset("/a/libx.so", 0x1234) == "libx.so+0x1200");

  // /proc: esta thread aparece, com CPU e estado.
  // Gira até o /proc contar pelo menos um tick de CPU (10 ms): numa CPU rápida um laço fixo acabava antes.
  const int self = getpid();
  volatile int spin = 0;
  ProcSnapshot s;
  const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  do {
    for (int i = 0; i < 5000000; i++) spin = spin + 1;
    s = ProcTake();
  } while (s.threads[self].ticks == 0 && std::chrono::steady_clock::now() < until);
  CHECK(s.threads.count(self) == 1);
  CHECK(s.threads[self].ticks > 0);
  CHECK(s.threads[self].state == 'R');
  CHECK(s.threads[self].cpu >= 0);

  // Símbolos: as funções deste executável pela .symtab, a partir do endereço.
  ElfSymbols syms;
  CHECK(syms.Load("/proc/self/exe"));
  Dl_info info{};
  dladdr(reinterpret_cast<void*>(&main), &info);
  const uint64_t base = uint64_t(reinterpret_cast<uintptr_t>(info.dli_fbase));
  auto rel = [&](void* f) { return uint64_t(reinterpret_cast<uintptr_t>(f)) - base; };
  const std::string* n1 = syms.Find(rel(reinterpret_cast<void*>(&sub_82001234)) + 4);
  const std::string* n2 = syms.Find(rel(reinterpret_cast<void*>(&sub_8200ABCD)));
  CHECK(n1 && *n1 == "sub_82001234");
  CHECK(n2 && *n2 == "sub_8200ABCD");
  CHECK(syms.Find(0) == nullptr);
  CHECK(!syms.Load("/nonexistent"));

  volatile int x = 0;
  sub_82001234(&x);
  sub_8200ABCD(&x);
  if (g_fail) {
    std::fprintf(stderr, "%d falhas\n", g_fail);
    return 1;
  }
  std::printf("diag_test: ok (%zu símbolos, %zu threads)\n", syms.Count(), s.threads.size());
  return 0;
}
