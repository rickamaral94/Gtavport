> [!WARNING]
> **Pré-release de teste: v0.4.2.** Ganhos de CPU trazidos de outros ports, ainda por medir no Odin:
> - **Do upstream (branch perf-linux):** a thread de render para menos e reaproveita constantes que não mudaram.
> - **Do port do Switch 2 (gtav-recomp-nx2):**
>   - conversor de vértices sem trava (lá: -2,4 ms por quadro na thread de gravação);
>   - tabela de objetos do kernel sem trava nas buscas (lá a thread principal passava ~18% do tempo esperando nela);
>   - leituras de constantes indexadas limitadas à tabela (índice negativo lia fora do buffer).
>
> **Recompile o jogo uma vez** quando o app pedir (a base do kernel mudou). Na primeira abertura, alguns shaders
> são traduzidos de novo. Instala por cima sem perder saves.

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
