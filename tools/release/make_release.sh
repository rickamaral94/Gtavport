#!/bin/bash
# Monta os arquivos de uma release do Gtavport em dist/<TAG>/ (o workflow .github/workflows/release.yml
# roda isto depois das builds; dá para rodar localmente também).
#   tools/release/make_release.sh TAG
# Pré-requisitos (já compilados): tools/prepare.sh, SDK Android (work/gtav/tools/android/build_sdk.sh),
# tools/build_android_shell.sh, odin2/driver/build.sh e, para o APK de prova, android-ondevice/probe/build.sh.
# Assinatura: ODIN2_KEYSTORE/ODIN2_KS_PASS/ODIN2_KEY_ALIAS/ODIN2_KEY_PASS (chave fixa) ou chave de debug.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TAG=${1:?uso: make_release.sh TAG}
G=$ROOT/work/gtav
D=$ROOT/dist/$TAG
rm -rf "$D" && mkdir -p "$D"

# 1) pasta android/ do pacote do PC, com o APK do Gtavport (make_shell.sh do upstream).
export ODIN2_JAVA=$ROOT/odin2/android/java ODIN2_PACKAGE=com.gtavport.app
# Ícone do app (arte original "GV"): res/ gerado de odin2/android/icon.
rm -rf "$ROOT/build/icon-res" && python3 -I "$ROOT/odin2/android/icon/make_icons.py" "$ROOT/build/icon-res"
export ODIN2_RES=$ROOT/build/icon-res
# Fase 1 (compilar no aparelho): libs de build + toolroot (android-ondevice/device/make_device_bundle.sh);
# sem o pacote, sai o APK só com o fluxo pelo PC.
if [ -d "$ROOT/build/device/libs" ]; then
  export ODIN2_LIBS=$ROOT/build/device/libs ODIN2_ASSETS=$ROOT/build/device/assets
else
  export ODIN2_LIBS=$ROOT/build/odin2-driver/out
fi
mkdir -p "$G/build/android-dll"
SHELL_OUT=$ROOT/build/release-android
rm -rf "$SHELL_OUT" && mkdir -p "$SHELL_OUT"
"$G/tools/android/make_shell.sh" "$SHELL_OUT"
cat > "$SHELL_OUT/LEIA-ME_GTAVPORT.txt" <<TXT
Gtavport $TAG — pasta android/ para o launcher do gtav-recomp no PC.
Substitua a pasta android/ do seu launcher por esta e rode "Build for Android": o pack gerado
(gtav-android-pack.zip) fica casado com o GTAV.apk desta pasta (Gtavport, pacote com.gtavport.app).
Nenhum código, shader ou dado do jogo está aqui: o pack é feito no seu PC, a partir dos seus discos.
TXT
# O app instala e compila o jogo no próprio aparelho: a pasta android/ para o PC não vai na release
# (WITH_PC_PACK=1 a inclui, para quem quiser o fluxo antigo).
[ "${WITH_PC_PACK:-0}" = 1 ] && (cd "$SHELL_OUT" && zip -q -r -X "$D/gtavport-android-$TAG.zip" .)
cp "$SHELL_OUT/GTAV.apk" "$D/gtavport-$TAG.apk"
# O build_apk.sh do upstream ignora erros do javac: confere que toda classe do Gtavport está no dex.
python3 -I - "$D/gtavport-$TAG.apk" "$ROOT/odin2/android/java/com/gtavrecomp/app" <<'PY'
import sys, zipfile, pathlib
dex = zipfile.ZipFile(sys.argv[1]).read("classes.dex")
missing = [p.stem for p in pathlib.Path(sys.argv[2]).glob("*.java") if f"Lcom/gtavrecomp/app/{p.stem};".encode() not in dex]
if missing: sys.exit("classes do Gtavport fora do APK (erro de compilação Java?): " + ", ".join(missing))
print("dex: todas as classes do Gtavport presentes")
PY

# 2) APK de prova (Etapa P), se compilado.
[ -f "$ROOT/build/probe/gtavport-probe.apk" ] && cp "$ROOT/build/probe/gtavport-probe.apk" "$D/gtavport-probe-$TAG.apk"

# 3) licenças do que vai nos binários.
L=$D/LICENSES
mkdir -p "$L"
cp "$ROOT/upstream/LICENSE" "$L/Gtavport-e-gtav-recomp-GPL-3.0.txt"
cp "$ROOT/odin2/post/third_party/smaa/LICENSE.txt" "$L/SMAA-MIT.txt"
cp "$ROOT/work/deps/libadrenotools/LICENSE" "$L/libadrenotools-BSD-2.txt"
cp -r "$SHELL_OUT/LICENSES/." "$L/" 2>/dev/null || true
[ -f "$ROOT/work/llvm/build/android-llvm/llvm-project-19.1.5.src/llvm/LICENSE.TXT" ] &&
  cp "$ROOT/work/llvm/build/android-llvm/llvm-project-19.1.5.src/llvm/LICENSE.TXT" "$L/LLVM-Apache-2.0-with-LLVM-exception.txt"
[ -f "$ROOT/work/dxc/DirectXShaderCompiler/LICENSE.TXT" ] && cp "$ROOT/work/dxc/DirectXShaderCompiler/LICENSE.TXT" "$L/DirectXShaderCompiler.txt"
for extra in "${THIRD_PARTY_LICENSES:-}"; do [ -n "$extra" ] && [ -d "$extra" ] && cp -r "$extra"/. "$L/"; done
cat > "$L/CREDITOS.txt" <<TXT
Gtavport é um fork de andrewnakas/gtav-recomp (GPL-3.0) para o AYN Odin 2 Portal Pro.
Passes de pós-processamento portados do BOTWRecompAndroid (MIT): FXAA; AMD FidelityFX Super Resolution 1
(EASU/RCAS, MIT); Snapdragon Game Super Resolution 1 (Qualcomm Innovation Center, BSD-3-Clause);
SMAA (Jimenez et al., MIT). Drivers via libadrenotools (Billy Laws, BSD-2). APK de prova: LLVM 19.1.5
(Apache-2.0 WITH LLVM-exception), DirectXShaderCompiler (University of Illinois/NCSA), XenosRecomp, SPIRV-Tools.
TXT
(cd "$D" && zip -q -r -X "LICENSES-$TAG.zip" LICENSES && rm -rf LICENSES)

# 4) fonte correspondente da parte GPL: este repositório (patches e código do Gtavport) + o gtav-recomp
# no commit fixado. As demais dependências (MIT/BSD/zlib/Apache) são públicas e estão fixadas por
# commit em versions.lock; tools/prepare.sh reconstrói a árvore exata usada nos binários.
SRC=$ROOT/build/release-src/gtavport-$TAG-source
rm -rf "$ROOT/build/release-src" && mkdir -p "$SRC/upstream"
git -C "$ROOT" archive --format=tar HEAD | tar -C "$SRC" -x
git -C "$ROOT/upstream" archive --format=tar HEAD | tar -C "$SRC/upstream" -x
{ echo "Gtavport $TAG — commit $(git -C "$ROOT" rev-parse HEAD)"; echo "gtav-recomp $(git -C "$ROOT/upstream" rev-parse HEAD)";
  echo "Dependências (versions.lock):"; grep -v '^#' "$ROOT/versions.lock"; } > "$SRC/FONTE.txt"
python3 -I "$ROOT/tools/check_purity.py" "$SRC" >/dev/null || { python3 -I "$ROOT/tools/check_purity.py" "$SRC"; exit 1; }
(cd "$ROOT/build/release-src" && tar -c "gtavport-$TAG-source" | xz -T0 -6 > "$D/gtavport-$TAG-source.tar.xz")

# 5) pureza de tudo e somas.
python3 -I "$ROOT/tools/check_purity.py" "$D"/*.apk "$D"/*.zip
(cd "$D" && sha256sum * > SHA256SUMS)
ls -la "$D"
