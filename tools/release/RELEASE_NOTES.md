> [!WARNING]
> **Pré-release, ainda não testada no aparelho.** Tudo foi compilado e verificado no CI (build a partir do código,
> verificação de pureza, testes), mas nenhum destes APKs rodou num Odin 2 até agora. É justamente para isso que
> esta release existe: para as primeiras medições (`docs/baseline.md`, `docs/etapa-p.md`).

**Nenhum jogo está incluído, nem aqui nem em lugar nenhum.** Você precisa dos seus próprios Disc 1, Disc 2 e
Title Update 27 do GTA V de Xbox 360. Código, shaders e dados do jogo são gerados no seu PC a partir dos seus
discos e nunca são distribuídos.

Gtavport é um fork de [andrewnakas/gtav-recomp](https://github.com/andrewnakas/gtav-recomp) (GPL-3.0) focado no
AYN Odin 2 Portal Pro (Snapdragon 8 Gen 2 / Adreno 740). Commit: `@SHA@`.

## Arquivos

| Arquivo | O que é |
|---|---|
| `gtavport-@TAG@.apk` | O app (pacote `com.gtavport.app`, convive com o app do upstream). Sem código do jogo. |
| `gtavport-android-@TAG@.zip` | Pasta `android/` para o launcher do gtav-recomp **no PC**: gera o pack casado com este APK. |
| `gtavport-probe-@TAG@.apk` | APK de prova da Etapa P: clang/lld/DXC rodando no aparelho (P1–P4). Não é o jogo. |
| `LICENSES-@TAG@.zip` | Licenças e créditos de tudo que vai nos binários. |
| `gtavport-@TAG@-source.tar.xz` | Fonte correspondente: este repositório, os patches e o gtav-recomp no commit fixado (demais dependências fixadas em `versions.lock`). |

## Como usar o app
1. No PC, instale o gtav-recomp do upstream e faça o setup com os seus discos, como no README dele.
2. Substitua a pasta `android/` do launcher pelo conteúdo de `gtavport-android-@TAG@.zip` e rode **Build for Android**.
   O pack gerado fica casado com este APK. Packs feitos com o `android/` original do upstream são recusados (outra
   `libgtav_fast.so`).
3. No Odin 2, instale `gtavport-@TAG@.apk` e siga o setup (pack + discos + TU), como no app do upstream.

## O que há de novo em relação ao upstream
- **Drivers Turnip:** tela "Driver gráfico" (importar zip no formato adrenotools, testar em processo separado,
  variáveis como `TU_DEBUG`, status do driver que carregou de fato).
- **Menu Video:** anti-aliasing (FXAA / SMAA 1x), upscaler (AMD FSR 1 / Snapdragon GSR 1) e resolução interna.
  O padrão é o caminho original do upstream.
- **Menu Performance:** fps, 1 % low, gargalo provável (GPU / CPU de render / CPU do jogo / cap), clocks,
  margem térmica e driver, além da gravação de sessão. A tela "Logs de desempenho" exporta tudo para `Download/GtavPort`.

## Limites conhecidos
- Nada foi executado num Odin 2 ainda; custo em ms das opções novas e quais sensores o Android libera estão por medir.
- AA e upscaler passam também pelo HUD (HUD nativo é trabalho da Fase 3).
- Assinatura desta build: **@SIGNING@** (chave fixa = atualizações instalam por cima; certificado em `docs/assinatura.md`).
