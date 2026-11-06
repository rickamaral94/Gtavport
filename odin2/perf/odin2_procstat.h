// Gtavport (odin2/perf) — leitura de /proc/self para a sonda de travadas e o diagnóstico: por thread,
// tempo de CPU (utime + stime), estado (R rodando/na fila, S dormindo, D esperando disco) e o núcleo em
// que rodou por último; do processo, bytes lidos e faltas de página; a frequência atual de alguns núcleos.
// Só arquivos do próprio processo e do sysfs da CPU: nada de root.
#pragma once

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include <dirent.h>

namespace gtav::native::perf {

struct ThreadStat {
  std::string comm;
  char state = '?';
  uint64_t ticks = 0;  // utime + stime (em ticks de _SC_CLK_TCK)
  int cpu = -1;        // núcleo em que rodou por último (campo 39)
};

struct ProcSnapshot {
  std::chrono::steady_clock::time_point t;
  std::map<int, ThreadStat> threads;
  uint64_t read_bytes = 0, majflt = 0;
  int mhz[3] = {-1, -1, -1};  // cpu0 (pequeno), cpu4 (médio), cpu7 (prime) no Snapdragon 8 Gen 2
};

inline bool ProcReadFile(const char* path, char* buf, size_t size) {
  FILE* f = std::fopen(path, "re");
  if (!f) return false;
  size_t n = std::fread(buf, 1, size - 1, f);
  std::fclose(f);
  buf[n] = 0;
  return n > 0;
}

inline int ProcReadMhz(int cpu) {
  char path[96], buf[64];
  std::snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
  if (!ProcReadFile(path, buf, sizeof(buf))) return -1;
  return int(std::strtoll(buf, nullptr, 10) / 1000);
}

// Campo n (contando do 1, como no proc(5)) de uma linha de stat: depois do ")" do nome vem o campo 3.
inline const char* ProcStatField(const char* close_paren, int n) {
  const char* p = close_paren + 2;
  for (int field = 3; field < n && p; field++) {
    p = std::strchr(p, ' ');
    if (p) p++;
  }
  return p;
}

inline bool ProcParseThread(const char* buf, ThreadStat* ts) {
  const char* open = std::strchr(buf, '(');
  const char* close = std::strrchr(buf, ')');
  if (!open || !close || close[1] == 0) return false;
  ts->comm.assign(open + 1, close);
  ts->state = close[2];
  const char* ut = ProcStatField(close, 14);
  const char* st = ProcStatField(close, 15);
  const char* cpu = ProcStatField(close, 39);
  ts->ticks = (ut ? std::strtoull(ut, nullptr, 10) : 0) + (st ? std::strtoull(st, nullptr, 10) : 0);
  ts->cpu = cpu ? std::atoi(cpu) : -1;
  return true;
}

inline ProcSnapshot ProcTake(bool with_mhz = true) {
  ProcSnapshot s;
  s.t = std::chrono::steady_clock::now();
  if (DIR* d = opendir("/proc/self/task")) {
    char path[300], buf[1024];
    while (dirent* e = readdir(d)) {
      if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
      std::snprintf(path, sizeof(path), "/proc/self/task/%s/stat", e->d_name);
      if (!ProcReadFile(path, buf, sizeof(buf))) continue;
      ThreadStat ts;
      if (ProcParseThread(buf, &ts)) s.threads[std::atoi(e->d_name)] = std::move(ts);
    }
    closedir(d);
  }
  char buf[2048];
  if (ProcReadFile("/proc/self/io", buf, sizeof(buf))) {
    if (const char* r = std::strstr(buf, "\nread_bytes: ")) s.read_bytes = std::strtoull(r + 13, nullptr, 10);
  }
  if (ProcReadFile("/proc/self/stat", buf, sizeof(buf))) {
    if (const char* close = std::strrchr(buf, ')'))
      if (const char* p = ProcStatField(close, 12)) s.majflt = std::strtoull(p, nullptr, 10);
  }
  if (with_mhz) {
    s.mhz[0] = ProcReadMhz(0);
    s.mhz[1] = ProcReadMhz(4);
    s.mhz[2] = ProcReadMhz(7);
  }
  return s;
}

}  // namespace gtav::native::perf
