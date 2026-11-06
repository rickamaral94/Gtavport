// Gtavport — seção "Performance" do menu: números ao vivo, gargalo provável, sensores, driver em uso
// (Turnip ou sistema) e gravação de sessão (CSV por quadro + resumo .txt) em logs/perf/.
//
// O renderer chama OnFrame / OnGpuFrame (thread de render); uma thread própria lê os sensores uma
// vez por segundo e grava os arquivos, para não pôr E/S de disco na thread de render.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gtav::native::perf {

struct FrameSample {
  double interval_ms = 0;   // entre esta apresentação e a anterior
  double record_ms = 0;     // gravação de comandos na thread de render (CPU)
  double gpu_wait_ms = 0;   // espera pela fence do quadro anterior no mesmo slot (GPU ocupada)
  double idle_ms = 0;       // thread de render esperando o próximo quadro do jogo (CPU do jogo)
  double acquire_ms = 0, submit_ms = 0;
  uint32_t draws = 0, new_pipelines = 0, textures = 0;
  double miss_ms = 0;       // shaders + pipelines + texturas criados no quadro (stutter)
};

// Devolve o número do quadro, para OnGpuFrame casar o tempo de GPU que chega dois quadros depois.
uint64_t OnFrame(const FrameSample& s);
void OnGpuFrame(uint64_t seq, double gpu_ms);
// GPU por tipo de passe (gtav_native_pass_gpu_times), ms por quadro, maior primeiro.
void SetPassBreakdown(std::vector<std::pair<std::string, double>> passes, double total_ms);
void SetDeviceName(const std::string& name);

// Sonda de travadas (odin2_stall.cpp): amostra /proc/self/task a cada 50 ms; num quadro >= 150 ms
// loga quais threads usaram CPU, quais esperaram disco, MB lidos e as frequências dos núcleos.
void StartStallProbe();
void OnHitch(double ms, uint64_t guest_frame);

// Seção no menu (dentro da janela de Settings, thread de render).
void BuildMenu(float ui_scale);

}  // namespace gtav::native::perf
