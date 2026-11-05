#!/usr/bin/env python3
"""Verificação estrutural de pureza: nada do jogo no repositório nem nos pacotes.

    python3 -I tools/check_purity.py                 # arquivos versionados do repositório
    python3 -I tools/check_purity.py X.apk pasta/    # + pacotes/pastas (zips/APKs abertos por dentro)

Não substitui a taint scan do upstream (tools/release/scan_release*.sh), que compara byte a byte com
o código gerado e os shaders do jogo e por isso só roda onde estão os discos. Esta verificação roda
em qualquer lugar (CI) e pega o que dá para reconhecer sem o jogo:
  - nomes: imagens de disco, xex/xexp, rpf, title updates, biblioteca do jogo compilada, pack Android,
    caches de shader/pipeline;
  - assinaturas de formato: XEX2, RPF, STFS (CON/LIVE/PIRS), XDVDFS (ISO de Xbox), SPIR-V;
  - código gerado pelo codegen do ReXGlue (muitas funções REX_FUNC_PROLOGUE);
  - arquivos grandes demais para código-fonte.
Saída != 0 se houver qualquer achado.
"""
from __future__ import annotations

import io
import re
import subprocess
import sys
import zipfile
from pathlib import Path

MAX_BYTES = 40 * 1024 * 1024  # acima disso, num repositório de código, é suspeito
# O APK de prova leva clang/lld e o APK do jogo leva o runtime: binários grandes legítimos.
LARGE_OK = re.compile(r"(^|/)lib(clang|ld\.lld|dxcompiler|rexruntime|rexgpu-xenos|gtav_fast|hook_impl|c\+\+_shared)\.so$")

NAME_RULES = [
    (re.compile(r"\.(iso|xex|xexp|rpf|xiso|god)$", re.I), "imagem/executável/arquivo do jogo"),
    (re.compile(r"(^|/)tu0000[0-9a-f]{4}_[0-9a-f]{8}$", re.I), "title update"),
    (re.compile(r"(^|/)(lib)?gtav_game\.(so|dll|dylib)$", re.I), "biblioteca do jogo compilada"),
    (re.compile(r"(^|/)gtav-android-pack[^/]*\.zip$", re.I), "pack Android (código + shaders do jogo)"),
    (re.compile(r"(^|/)GTAV\.ipa$"), "ipa gerado (contém o jogo)"),
    (re.compile(r"(^|/)(shader_cache|spirv|generated)/", re.I), "pasta de shaders/código gerado"),
    (re.compile(r"(^|/)vk_pipelines[^/]*\.bin$|(^|/)pipelines\.bin$", re.I), "cache de pipeline"),
    (re.compile(r"\.(spv|fxc|xma|ytd|ydr|yft|ycd|ysc)$", re.I), "shader/asset do jogo"),
]

XDVDFS = b"MICROSOFT*XBOX*MEDIA"
GENERATED_MIN = 20  # REX_FUNC_PROLOGUE() por arquivo: o template define a macro, código gerado a usa por função


def magic_findings(name: str, head: bytes, data: bytes | None) -> list[str]:
    out = []
    h4 = head[:4]
    if h4 == b"XEX2":
        out.append("assinatura XEX2 (executável de Xbox 360)")
    if h4 in (b"RPF7", b"7FPR", b"RPF2", b"2FPR", b"RPF6", b"6FPR"):
        out.append("assinatura RPF (arquivo de dados da Rockstar)")
    if h4 in (b"CON ", b"LIVE", b"PIRS") and len(head) >= 0x22C:
        out.append("assinatura STFS (pacote de Xbox 360 / title update)")
    if h4 in (b"\x03\x02\x23\x07", b"\x07\x23\x02\x03"):
        out.append("assinatura SPIR-V (shader compilado)")
    if data is not None and len(data) >= 0x10000 + len(XDVDFS) and data[0x10000:0x10000 + len(XDVDFS)] == XDVDFS:
        out.append("sistema de arquivos XDVDFS (ISO de Xbox)")
    if data is not None and name.endswith((".cpp", ".cc", ".h", ".inc", ".c")):
        n = data.count(b"REX_FUNC_PROLOGUE()")
        if n >= GENERATED_MIN:
            out.append(f"código gerado pelo codegen ({n} funções recompiladas)")
    return out


def check_blob(name: str, data: bytes, findings: list[str], depth: int = 0) -> None:
    for rx, why in NAME_RULES:
        if rx.search(name):
            findings.append(f"{name}: {why}")
    if len(data) > MAX_BYTES and not LARGE_OK.search(name):
        findings.append(f"{name}: {len(data) >> 20} MB (grande demais para fonte; explicar ou remover)")
    for why in magic_findings(name, data[:0x400], data):
        findings.append(f"{name}: {why}")
    if depth < 3 and name.lower().endswith((".zip", ".apk", ".jar", ".aab")) and data[:2] == b"PK":
        try:
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                for info in z.infolist():
                    if not info.is_dir():
                        check_blob(f"{name}!{info.filename}", z.read(info), findings, depth + 1)
        except zipfile.BadZipFile:
            findings.append(f"{name}: zip inválido")


def check_path(p: Path, label: str, findings: list[str]) -> None:
    if p.is_dir():
        for f in sorted(p.rglob("*")):
            if f.is_file():
                check_path(f, f"{label}/{f.relative_to(p)}", findings)
        return
    try:
        data = p.read_bytes()
    except OSError as e:
        findings.append(f"{label}: ilegível ({e})")
        return
    check_blob(label, data, findings)


def repo_files(root: Path) -> list[str]:
    out = subprocess.run(["git", "-C", str(root), "ls-files", "-z"], capture_output=True, check=True).stdout
    return [f for f in out.decode().split("\0") if f]


def main(argv: list[str]) -> int:
    root = Path(__file__).resolve().parents[1]
    findings: list[str] = []
    count = 0
    for rel in repo_files(root):
        p = root / rel
        if p.is_file():  # o submódulo upstream/ aparece como diretório: tem as próprias regras
            check_path(p, rel, findings)
            count += 1
    for a in argv[1:]:
        check_path(Path(a), a, findings)
        count += 1
    if findings:
        print("PUREZA: FALHOU")
        for f in findings:
            print("  " + f)
        return 1
    print(f"PUREZA: ok ({count} entradas verificadas)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
