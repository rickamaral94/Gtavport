// Gtavport — pós-processamento antes da apresentação: anti-aliasing (FXAA / SMAA 1x) na imagem do
// jogo e upscaler (bilinear / AMD FSR 1 / Snapdragon GSR 1) até o tamanho dela na tela. Opções no menu
// "Video" (menu.cpp), cvars odin2_antialias e odin2_upscaler, que valem na hora.
//
// Os padrões (off / bilinear) reproduzem exatamente o caminho do upstream: Run() devolve o próprio
// front buffer e o renderer faz o mesmo blit bilinear de antes. Assim o baseline da Etapa 0.5 continua
// comparável.
#pragma once

#include <plume_render_interface.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace gtav::native {

// O que o pós-processamento precisa do backend (backend.cpp monta isto).
struct PostHost {
  plume::RenderDevice* device = nullptr;
  plume::RenderDescriptorSet* textures = nullptr;  // heap de texturas do backend (bindless)
  plume::RenderDescriptorSet* samplers = nullptr;  // heap de samplers do backend
  uint32_t texture_heap_size = 0, sampler_heap_size = 0;
  std::function<uint32_t()> alloc_descriptor;
  std::function<uint32_t(const plume::RenderSamplerDesc&)> sampler;
  std::function<void()> invalidate_draw_state;  // o backend guarda estado de draw; os passes o mudam
};

class Post {
 public:
  explicit Post(PostHost host);
  ~Post();
  // Roda os passes escolhidos sobre a imagem `descriptor` (src_w x src_h) para o tamanho `out_w` x
  // `out_h` que ela ocupa na tela e devolve o descritor a apresentar com o blit do upstream. Sem nada
  // escolhido (ou upscaler sem ampliação) devolve o próprio `descriptor`. Deixa o framebuffer
  // desligado: o chamador liga o da swap chain depois.
  uint32_t Run(plume::RenderCommandList* list, uint32_t descriptor, uint32_t src_w, uint32_t src_h, uint32_t out_w,
               uint32_t out_h);
  // Texto curto do que rodou no último quadro (menu, log).
  const char* LastChain() const { return last_chain_; }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  const char* last_chain_ = "off";
};

}  // namespace gtav::native
