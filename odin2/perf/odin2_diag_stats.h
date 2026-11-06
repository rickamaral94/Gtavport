// Gtavport (odin2/perf) — contas do diagnóstico "Medir desempenho", sem dependência do Android (testadas no
// host: odin2/perf/test/diag_test.cpp). Percentis do intervalo entre quadros, uso por thread a partir das
// amostras de /proc e agregação das amostras de PC por função.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace gtav::native::perf::diag {

// Percentil p (0..100) com interpolação linear entre vizinhos (o "linear" do numpy); 0 sem dados.
inline double Percentile(std::vector<double> v, double p) {
  if (v.empty()) return 0;
  std::sort(v.begin(), v.end());
  const double idx = std::clamp(p, 0.0, 100.0) / 100.0 * double(v.size() - 1);
  const size_t lo = size_t(std::floor(idx)), hi = std::min(lo + 1, v.size() - 1);
  return v[lo] + (v[hi] - v[lo]) * (idx - double(lo));
}

inline double Mean(const std::vector<double>& v) {
  if (v.empty()) return 0;
  double s = 0;
  for (double x : v) s += x;
  return s / double(v.size());
}

// Uso de uma thread na janela: amostras de estado a cada 50 ms (R rodando ou na fila, S dormindo/esperando,
// D esperando disco) e o núcleo em que estava (por cluster) quando em R; CPU própria do início ao fim.
struct ThreadUse {
  std::string name;
  uint64_t samples = 0, r = 0, s = 0, d = 0;
  uint64_t cluster_r[3] = {0, 0, 0};  // eficiência, performance, prime
  double cpu_ms = 0;
};

// Fração (0..1) do tempo em que a thread estava pronta mas sem núcleo: R amostrado menos a CPU que usou.
inline double QueuedFraction(const ThreadUse& t, double window_ms) {
  if (t.samples == 0 || window_ms <= 0) return 0;
  const double r = double(t.r) / double(t.samples), cpu = t.cpu_ms / window_ms;
  return std::max(0.0, r - cpu);
}

// Ordena por CPU usada, maior primeiro.
inline std::vector<std::pair<int, ThreadUse>> TopThreads(const std::map<int, ThreadUse>& m, size_t n) {
  std::vector<std::pair<int, ThreadUse>> v(m.begin(), m.end());
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.cpu_ms > b.second.cpu_ms; });
  if (v.size() > n) v.resize(n);
  return v;
}

// Contagem por nome (função ou módulo+offset), maior primeiro.
inline std::vector<std::pair<std::string, uint64_t>> TopNames(const std::map<std::string, uint64_t>& m, size_t n) {
  std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
  std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second > b.second || (a.second == b.second && a.first < b.first); });
  if (v.size() > n) v.resize(n);
  return v;
}

// Nome de um módulo sem a pasta ("/data/app/.../lib/arm64/libgtav_fast.so" -> "libgtav_fast.so").
inline std::string BaseName(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Endereço sem símbolo: módulo+offset em blocos de 64 bytes (amostras do mesmo trecho somam juntas).
inline std::string ModuleOffset(const std::string& module, uint64_t rel) {
  char b[32];
  std::snprintf(b, sizeof b, "+0x%llx", (unsigned long long)(rel & ~uint64_t(63)));
  return BaseName(module) + b;
}

// Cluster (0 eficiência, 1 performance, 2 prime) de cada núcleo a partir da frequência máxima de cada um.
inline std::vector<int> ClustersFromMaxFreq(const std::vector<long>& max_khz) {
  std::vector<long> distinct;
  for (long f : max_khz)
    if (f > 0 && std::find(distinct.begin(), distinct.end(), f) == distinct.end()) distinct.push_back(f);
  std::sort(distinct.begin(), distinct.end());
  std::vector<int> out;
  for (long f : max_khz) {
    if (f <= 0) {
      out.push_back(-1);
      continue;
    }
    int idx = int(std::find(distinct.begin(), distinct.end(), f) - distinct.begin());
    // Mais de três níveis: os do meio contam como performance.
    out.push_back(distinct.size() <= 1 ? 1 : idx == 0 ? 0 : idx == int(distinct.size()) - 1 ? 2 : 1);
  }
  return out;
}

}  // namespace gtav::native::perf::diag
