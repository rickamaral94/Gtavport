# Gtavport — GTA V (Xbox 360 TU27) no AYN Odin 2 Portal Pro

Fork fino de [andrewnakas/gtav-recomp](https://github.com/andrewnakas/gtav-recomp) (GPL-3.0) focado no
Snapdragon 8 Gen 2 / Adreno 740, com meta de compilar o jogo **no próprio aparelho** a partir dos discos do usuário.

**Nenhum dado do jogo neste repositório ou em qualquer APK** — ver as regras "Keeping it clean" do upstream.
Sem contorno de DRM. Sem modo online.

Estado: Caminho A aprovado; plano em avaliação. Leia [`docs/plano-caminho-A.md`](docs/plano-caminho-A.md), [`docs/decisao-arquitetura.md`](docs/decisao-arquitetura.md) e
[`docs/baseline.md`](docs/baseline.md).

Suporte a drivers **Turnip** (importar, escolher, testar) via adrenotools: [`odin2/driver/README.md`](odin2/driver/README.md).

Layout previsto (após aprovação): `upstream/` (submódulo pinado), `odin2/` (patches e tuning), `android-ondevice/` (compilação no aparelho).
