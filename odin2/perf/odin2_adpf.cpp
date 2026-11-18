// Gtavport — dica de desempenho do Android (ADPF, APerformanceHint, API 33; docs/validacao-18-11.md). Uma sessão com a
// thread principal do jogo e a thread de render, com a meta do quadro (33,3 ms a 30 fps): a cada quadro real informa o
// trabalho de verdade (o maior entre a CPU da thread principal no quadro e a gravação da thread de render). O sistema
// ajusta o clock dentro dos limites normais do aparelho (não é overclock): sobe quando o quadro estoura a meta.
// As funções são procuradas em tempo de execução (libandroid.so de API 33+): sem elas, nada acontece.
#include "odin2_perf.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

REXCVAR_DEFINE_BOOL(odin2_adpf, true, "Gtavport",
                    "Dica de desempenho do Android (ADPF): informa a meta do quadro e o trabalho da thread principal e da de "
                    "render, para o sistema ajustar o clock dentro dos limites normais");
REXCVAR_DECLARE(int32_t, gtav_fps_limit);

namespace gtav::native::perf {
namespace {

using GetManagerFn = void* (*)();
using CreateSessionFn = void* (*)(void*, const int32_t*, size_t, int64_t);
using UpdateTargetFn = int (*)(void*, int64_t);
using ReportFn = int (*)(void*, int64_t);

struct Adpf {
  bool tried = false;
  void* session = nullptr;
  UpdateTargetFn update = nullptr;
  ReportFn report = nullptr;
  int main_tid = 0;
  int64_t target_ns = 0;
  uint64_t last_main_runtime = 0;
  // estatística do log
  uint32_t n = 0, over = 0;
  double sum_ms = 0;
  std::chrono::steady_clock::time_point since = std::chrono::steady_clock::now();
} g;

int FindMainThread() {
  DIR* dir = opendir("/proc/self/task");
  if (!dir) return 0;
  int found = 0;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] == '.') continue;
    char path[64], name[32] = {};
    std::snprintf(path, sizeof path, "/proc/self/task/%s/comm", e->d_name);
    if (FILE* f = std::fopen(path, "re")) {
      size_t n = std::fread(name, 1, sizeof name - 1, f);
      name[n] = 0;
      std::fclose(f);
    }
    if (!std::strncmp(name, "Main XThread", 12)) {
      found = std::atoi(e->d_name);
      break;
    }
  }
  closedir(dir);
  return found;
}

// tempo de CPU da thread (ns), primeiro campo de schedstat
uint64_t Runtime(int tid) {
  char path[64];
  std::snprintf(path, sizeof path, "/proc/self/task/%d/schedstat", tid);
  FILE* f = std::fopen(path, "re");
  if (!f) return 0;
  unsigned long long ns = 0;
  if (std::fscanf(f, "%llu", &ns) != 1) ns = 0;
  std::fclose(f);
  return ns;
}

int64_t TargetNs() {
  const int32_t fps = REXCVAR_GET(gtav_fps_limit);
  return 1000000000LL / (fps > 0 ? fps : 60);
}

}  // namespace

void AdpfFrame(double record_ms) {
  if (!REXCVAR_GET(odin2_adpf)) return;
  auto& s = g;
  if (!s.session) {
    if (s.tried) return;
    s.main_tid = FindMainThread();
    if (!s.main_tid) return;  // (o jogo ainda não criou a thread principal: tenta no próximo quadro)
    s.tried = true;
    void* lib = dlopen("libandroid.so", RTLD_NOW | RTLD_NOLOAD);
    if (!lib) lib = dlopen("libandroid.so", RTLD_NOW);
    auto get = lib ? reinterpret_cast<GetManagerFn>(dlsym(lib, "APerformanceHint_getManager")) : nullptr;
    auto create = lib ? reinterpret_cast<CreateSessionFn>(dlsym(lib, "APerformanceHint_createSession")) : nullptr;
    s.update = lib ? reinterpret_cast<UpdateTargetFn>(dlsym(lib, "APerformanceHint_updateTargetWorkDuration")) : nullptr;
    s.report = lib ? reinterpret_cast<ReportFn>(dlsym(lib, "APerformanceHint_reportActualWorkDuration")) : nullptr;
    void* mgr = get ? get() : nullptr;
    const int32_t tids[2] = {s.main_tid, int32_t(gettid())};
    s.target_ns = TargetNs();
    s.session = mgr && create && s.report ? create(mgr, tids, 2, s.target_ns) : nullptr;
    REXLOG_INFO("[adpf] {} (thread principal {}, render {}, meta {:.1f} ms)",
                s.session ? "sessão criada" : "indisponível neste Android (sem APerformanceHint)", s.main_tid, gettid(),
                s.target_ns / 1e6);
    if (!s.session) return;
    s.last_main_runtime = Runtime(s.main_tid);
    return;
  }
  const int64_t target = TargetNs();
  if (target != s.target_ns && s.update) s.update(s.session, target), s.target_ns = target;
  const uint64_t rt = Runtime(s.main_tid);
  const double main_ms = rt > s.last_main_runtime ? (rt - s.last_main_runtime) / 1e6 : 0.0;
  s.last_main_runtime = rt;
  double work = main_ms > record_ms ? main_ms : record_ms;
  if (work <= 0) return;
  if (work > 1000.0) work = 1000.0;  // (pausa, troca de janela)
  s.report(s.session, int64_t(work * 1e6));
  s.n++;
  s.sum_ms += work;
  s.over += work * 1e6 > double(s.target_ns);
  auto now = std::chrono::steady_clock::now();
  if (now - s.since >= std::chrono::seconds(30)) {
    REXLOG_INFO("[adpf] 30 s: trabalho médio {:.1f} ms de {:.1f} ms; {} de {} quadros acima da meta", s.n ? s.sum_ms / s.n : 0.0,
                s.target_ns / 1e6, s.over, s.n);
    s.n = s.over = 0, s.sum_ms = 0, s.since = now;
  }
}

}  // namespace gtav::native::perf
