> [!WARNING]
> **Pré-release de teste: v0.5.4.**
> - **Caçador de piscadas sem travar o jogo:** os quadros salvos (2,7 MB cada) agora são gravados noutra thread. Na
>   sessão de 18/11 eles causaram 34 das 45 travadas por "espera do slot".
> - **Detector de quadro gerado escuro** (sempre ligado com a geração de quadros, ~18 KB por quadro): compara o brilho do
>   quadro gerado com o real; linhas `[fg] quadro gerado escuro` e a contagem a cada 10 s.
> - **Teste do chão** em Caçar shader: "marcas do chão (viés de profundidade x8)". O GTA usa profundidade float24 e o
>   port emula com float32; com o caçador ligado, passe pelo mesmo trecho com e sem e compare as piscadas.
>
> Da v0.5.3:
> - **Caçar piscadas:** Select + R1 > Performance > Caçar shader > "Caçar piscadas". Compara cada quadro real do jogo com
>   os vizinhos; os que destoam vão para o `gtav.log` (`flicker #`) e os grandes para `logs/flicker` (antes, durante e
>   depois), incluídos na exportação de logs. Com a geração de quadros ligada, um preto que ele não pega veio de um
>   quadro gerado.
>
> Da v0.5.2 (medição):
> - **Por que cada efeito novo compila** (linhas `[pipe]` no `gtav.log`): PS novo, PS já visto com outro VS, mesmo par só
>   com o bit de escala de render, mesmo par com outra constante, ou só estado — com o tempo de cada grupo. Decide o
>   próximo passo contra as travadas ao entrar em área nova (docs/validacao-18-11.md, validado com o agente do Turnip).
> - **Dica de desempenho do Android (ADPF):** a thread principal e a de render informam ao sistema a meta do quadro
>   (33,3 ms a 30 fps) e o trabalho real; o sistema ajusta o clock dentro dos limites normais (não é overclock). Linhas
>   `[adpf]` no log; `odin2_adpf=false` desliga.
> - Da v0.5.1: geração de quadros 2x (LSFG 3.1, o seu Lossless.dll), testada no Odin: 60 na tela, p99 35 ms.
>
> Instala por cima sem perder saves; não precisa recompilar o jogo.

**Nenhum jogo está incluído, nem aqui nem em lugar nenhum.** Você precisa dos seus próprios Disc 1, Disc 2 e
Title Update 27 do GTA V de Xbox 360. Código, shaders e dados do jogo são gerados **no seu aparelho** a partir dos seus discos e nunca são distribuídos.

Gtavport é um fork de [andrewnakas/gtav-recomp](https://github.com/andrewnakas/gtav-recomp) (GPL-3.0) focado no
AYN Odin 2 Portal Pro (Snapdragon 8 Gen 2 / Adreno 740). Commit: `@SHA@`.

## Arquivos

| Arquivo | O que é |
|---|---|
| `gtavport-@TAG@.apk` | O app (pacote `com.gtavport.app`, convive com o app do upstream). Sem código do jogo; traz o codegen, o clang/lld e o DXC para compilar o jogo no próprio aparelho. |
| `gtavport-probe-@TAG@.apk` | APK de prova da Etapa P: clang/lld/DXC rodando no aparelho (P1–P4). Não é o jogo. |
| `LICENSES-@TAG@.zip` | Licenças e créditos de tudo que vai nos binários. |
| `gtavport-@TAG@-source.tar.xz` | Fonte correspondente: este repositório, os patches e o gtav-recomp no commit fixado (demais dependências fixadas em `versions.lock`). |

## Como usar: tudo no Odin 2, sem PC (como os ports de BOTW/TOTK)
1. Copie para o aparelho os seus **Disc 1**, **Disc 2** (ISO ou pasta) e o **Title Update 27** (`tu00000009_00000000`).
2. Instale `gtavport-@TAG@.apk` e abra: ele já abre na tela **instalar e compilar neste aparelho**.
3. Escolha os três arquivos e toque em **Instalar e compilar**. O app copia e confere ~16 GB, recompila o código
   do jogo no aparelho (codegen + clang nos núcleos grandes) e deixa tudo pronto. Precisa de ~25 GB livres
   durante a instalação. Pode sair da tela: a compilação continua (notificação). Se for interrompida, continua de
   onde parou.
4. Toque em **Jogar**. Os shaders são traduzidos no aparelho enquanto você joga: nos primeiros minutos alguns
   objetos aparecem com atraso, e nas próximas vezes eles já estão prontos.

## O que há de novo em relação ao upstream
- **Drivers Turnip:** tela "Driver gráfico" (importar zip no formato adrenotools, testar em processo separado,
  variáveis como `TU_DEBUG`, status do driver que carregou de fato).
- **Menu Video:** anti-aliasing (FXAA / SMAA 1x), upscaler (AMD FSR 1 / Snapdragon GSR 1) e resolução interna.
  O padrão é o caminho original do upstream.
- **Menu Performance:** fps, 1 % low, gargalo provável (GPU / CPU de render / CPU do jogo / cap), clocks,
  margem térmica e driver, além da gravação de sessão. A tela "Logs de desempenho" exporta tudo para `Download/GtavPort`.

## Limites conhecidos
- Nada foi executado num Odin 2 ainda: tempo da compilação no aparelho, RAM do clang por job, se o SELinux do
  aparelho deixa o app executar as ferramentas, custo em ms das opções novas e quais sensores o Android libera
  estão por medir. O `gtavport-probe` testa as ferramentas antes (P1–P4); vale rodá-lo primeiro.
- O código gerado no aparelho é conferido arquivo a arquivo contra a referência do upstream (811 hashes); se o
  codegen no Android gerar algo diferente do PC, a instalação para com a mensagem e nada é usado.
- AA e upscaler passam também pelo HUD (HUD nativo é trabalho da Fase 3).
- Assinatura desta build: **@SIGNING@** (chave fixa = atualizações instalam por cima; certificado em `docs/assinatura.md`).
