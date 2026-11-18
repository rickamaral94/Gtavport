# Geração de quadros (odin2/framegen)

2x: o jogo continua em 30 quadros reais e a tela mostra 60. Entre cada dois quadros reais entra um gerado pelo LSFG 3.1
(ou 3.1P, mais leve), com os shaders do **Lossless.dll do próprio usuário** (Lossless Scaling, Steam, app 993090).

**Nada do Lossless Scaling vem no APK nem neste repositório.** O usuário escolhe o próprio DLL em Configurações >
Geração de quadros; os shaders são traduzidos no aparelho para `files/lsfg/spirv` e o DLL não é guardado.

## Peças

| Arquivo | O que faz | Origem |
|---|---|---|
| `lsfg_framegen_c.{h,cpp}`, `lsfg_framegen.map` | interface C do framegen, em `liblsfg_framegen.so` com o volk dele escondido | TOTKRecompAndroid (MIT, Peiikai), commit `2ee651b` |
| `lsfg_extract.{h,cpp}`, `lsfg_translate.cpp`, `lsfg_expect.inc` | extração e tradução DXBC → SPIR-V, conferência dos bindings, instalação atômica | TOTKRecompAndroid (MIT); SHA-256 trocado pelo `odin2/driver/odin2_sha256.h` |
| `lsfg_jni.cpp` | a ponte com o app (`Odin2Framegen.java`) | Gtavport |
| `odin2_framegen.{h,cpp}` | a integração no renderer do GTA (plume, threads de gravação e de submissão) | Gtavport, desenho do `framegen.c` do TOTK |

Dependências (fixadas em `versions.lock`, baixadas por `tools/prepare.sh` em `work/deps/lsfg-vk-android`):

| Projeto | Licença |
|---|---|
| [GameNative/lsfg-vk-android](https://github.com/GameNative/lsfg-vk-android) `d33caceb5e26` (`framegen/`, LSFG 3.1/3.1P) | MIT |
| dxbc do DXVK, cópia [GameNative/dxbc](https://github.com/GameNative/dxbc) `78ab59a8aaeb` (o submódulo original, PancakeTAS/dxbc, saiu do ar) | zlib |
| pe-parse (trailofbits) | MIT |
| volk | MIT |

O lsfg-vk atual mudou para CC BY-NC-ND 4.0; o ramo usado deriva da 1.0.0, MIT.

## Como funciona no renderer

Por quadro real B (`renderer.cpp`, patch 0063):
1. a imagem final (pós-processamento + barras pretas) é desenhada na entrada da vez (RGBA8 storage, `count % 2`);
2. na thread de submissão, depois dessa lista, o framegen gera I(A, B) na saída, na nossa fila, com o mutex da fila
   do plume (o framegen faz o próprio `vkQueueSubmit`). Barreiras de memória completas antes e depois;
3. a janela recebe I e depois B, cada um com o menu e o contador por cima, em FIFO com a janela pedida a 60 Hz
   (`ANativeWindow_setFrameRate`).

Exige: VSync ligado; `vulkanMemoryModel` e imagens storage sem formato no driver (patch 0062 do plume liga quando há).
Um intervalo de mais de 100 ms entre quadros reais (carregamento, pausa) recomeça sem quadro gerado.

## Testes

`test/run.sh` (no PC): a extração recusa entradas inválidas sem travar.
