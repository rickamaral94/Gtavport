// Gtavport — geração de quadros 2x (30 reais -> 60 na tela) com o framegen do lsfg-vk-android (LSFG 3.1/3.1P) e os
// shaders do Lossless.dll do próprio usuário (odin2/framegen/README.md). Desenho vindo do TOTKRecompAndroid
// (runtime/nvn/vulkan/framegen.c, MIT), adaptado ao renderer do GTA (plume, uma thread que grava e outra que submete).
//
// Por quadro real B (o renderer chama, na thread de render):
//   1. Begin(): pronto para usar? (opção ligada, shaders instalados, device com as features, sem falha anterior)
//   2. o renderer desenha a imagem final (com o pós-processamento e as barras pretas) em Input() e chama EndInput(),
//      que deixa a imagem em GENERAL com uma barreira de memória completa;
//   3. QueueWork() vai para a thread de submissão logo depois da lista da entrada: inicializa a biblioteca e o contexto
//      na primeira vez e gera I(A, B) na saída, tudo na nossa fila com o mutex dela (o vkQueueSubmit do framegen);
//   4. com ShowGenerated() verdadeiro, o renderer apresenta Output() (I) e depois Input() (B); senão só B.
// O ritmo vem da FIFO: com a janela pedida a 60 Hz (WantedFrameRate) cada imagem fica um vsync; o jogo em 30.
// O menu e o contador são desenhados por cima das duas imagens, nunca interpolados.
#pragma once

#include <plume_render_interface.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace gtav::native {

struct FramegenHost {
  plume::RenderDevice* device = nullptr;
  plume::RenderCommandQueue* queue = nullptr;
  // descritor da textura no heap do backend, para o blit (layout GENERAL), e a devolução dele
  std::function<uint32_t(plume::RenderTexture*, plume::RenderTextureView*)> register_texture;
  std::function<void(uint32_t)> free_descriptor;
};

class Framegen {
 public:
  explicit Framegen(FramegenHost host);
  ~Framegen();

  // A opção (odin2_framegen) está ligada: o renderer pede 60 Hz à janela.
  static bool Enabled();
  // Pronto para este quadro de w x h (o tamanho da janela)? Cria ou recria as imagens quando precisa.
  bool Begin(uint32_t w, uint32_t h);
  plume::RenderTexture* Input() const;
  plume::RenderFramebuffer* InputFramebuffer() const;
  uint32_t InputDescriptor() const;
  uint32_t OutputDescriptor() const;
  // Depois de desenhar na entrada: GENERAL + barreira completa (o framegen lê no compute).
  void EndInput(plume::RenderCommandList* list);
  // No começo de cada lista que lê a saída ou reescreve a entrada: barreira de memória completa.
  static void FullBarrier(plume::RenderCommandList* list);
  // O que a thread de submissão precisa deste quadro (retrato tirado na thread de render: as imagens podem ser
  // recriadas no quadro seguinte enquanto este ainda está na fila).
  struct Work {
    uint32_t gen = 0, w = 0, h = 0;
    uint64_t in0 = 0, in1 = 0, out = 0;  // VkImage
    float flow = 0.5f;
    bool perf = false;
  };
  Work PrepareWork() const;
  // Na thread de submissão, logo depois da lista da entrada.
  void QueueWork(const Work& work);
  // O quadro gerado deste quadro real pode ir para a tela (já houve um quadro real antes e nada falhou).
  bool ShowGenerated() const;
  // Fim do quadro real (thread de render): o próximo usa a outra entrada.
  void EndFrame();
  // Sem quadro anterior utilizável (pausa longa, troca de janela): o próximo vai sozinho.
  void Reset();
  // Detector de quadro gerado escuro (sempre ligado; ~18 KB por quadro): o gerado e o real reduzidos a 64x36 lado a
  // lado (blit pelo backend), copiados para a CPU; depois da fence do slot, compara o brilho médio dos dois.
  // CaptureCheck grava na lista (fora de passe de render); CheckSlot roda quando a fence do slot passou.
  void CaptureCheck(plume::RenderCommandList* list, uint32_t slot,
                    const std::function<void(plume::RenderCommandList*, uint32_t)>& blit);
  void CheckSlot(uint32_t slot);
  // Uma linha para o log e o menu.
  std::string Status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gtav::native
