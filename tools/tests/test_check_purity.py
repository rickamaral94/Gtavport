import io
import sys
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_purity as C  # noqa: E402


def run(name, data):
    f = []
    C.check_blob(name, data, f)
    return f


def zipped(entries):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        for n, d in entries.items():
            z.writestr(n, d)
    return b.getvalue()


class Purity(unittest.TestCase):
    def test_clean_source(self):
        self.assertEqual(run("odin2/driver/odin2_driver.cpp", b"#include <x>\nint main(){}\n"), [])

    def test_names(self):
        for n in ["Disc 1.iso", "a/default.xexp", "update.rpf", "tu00000009_00000000", "lib/arm64-v8a/libgtav_game.so",
                  "gtav-android-pack.zip", "x/shader_cache/a.bin", "vk_pipelines.turnip.bin", "ps_0001.spv"]:
            self.assertTrue(run(n, b"x"), n)

    def test_magics(self):
        self.assertTrue(any("XEX2" in f for f in run("blob", b"XEX2" + bytes(100))))
        self.assertTrue(any("RPF" in f for f in run("blob", b"RPF7" + bytes(100))))
        self.assertTrue(any("SPIR-V" in f for f in run("blob", b"\x03\x02\x23\x07" + bytes(100))))
        self.assertTrue(any("STFS" in f for f in run("blob", b"LIVE" + bytes(0x300))))
        iso = bytearray(0x10100)
        iso[0x10000:0x10000 + len(C.XDVDFS)] = C.XDVDFS
        self.assertTrue(any("XDVDFS" in f for f in run("blob", bytes(iso))))
        # "LIVE" no começo de um texto curto não é STFS
        self.assertEqual(run("notes.txt", b"LIVE notes"), [])

    def test_generated_code(self):
        gen = b"".join(b"DEFINE_REX_FUNC(sub_8%07X) {\n REX_FUNC_PROLOGUE();\n}\n" % i for i in range(30))
        self.assertTrue(any("codegen" in f for f in run("x/sub.cpp", gen)))
        hooks = b"#define REX_FUNC_PROLOGUE() ((void)0)\n" + b"REX_FUNC_PROLOGUE();\n" * 3
        self.assertEqual(run("hooks.h", hooks), [])

    def test_inside_apk(self):
        apk = zipped({"lib/arm64-v8a/libodin2driver.so": b"\x7fELF", "assets/x/ps_1.spv": b"\x03\x02\x23\x07"})
        f = run("game.apk", apk)
        self.assertEqual(len([x for x in f if "ps_1.spv" in x]), 2)  # nome + assinatura
        nested = zipped({"inner.zip": zipped({"default.xex": b"XEX2"})})
        self.assertTrue(any("inner.zip!default.xex" in x for x in run("outer.zip", nested)))

    def test_large_allowlist(self):
        big = b"\0" * (C.MAX_BYTES + 1)
        self.assertEqual(run("lib/arm64-v8a/libclang.so", big), [])
        self.assertTrue(run("blob.bin", big))

    def test_large_archive_checked_by_entries(self):
        apk = zipped({"lib/arm64-v8a/libclang.so": b"\0" * (C.MAX_BYTES + 1)})
        self.assertEqual(run("probe.apk", apk), [])
        apk = zipped({"assets/big.bin": b"\0" * (C.MAX_BYTES + 1)})
        self.assertTrue(any("big.bin" in f for f in run("probe.apk", apk)))


    def test_allowlist_by_hash(self):
        import hashlib
        spv = b"\x03\x02\x23\x07" + b"nosso shader"
        old = C.ALLOW
        try:
            C.ALLOW = {(hashlib.sha256(spv).hexdigest(), "assets/probe_ps.ref.spv")}
            self.assertEqual(run("x.apk!assets/probe_ps.ref.spv", spv), [])
            # mesmo caminho, conteúdo diferente: volta a ser barrado
            self.assertTrue(run("x.apk!assets/probe_ps.ref.spv", spv + b"!"))
            # mesmo conteúdo em outro caminho: barrado
            self.assertTrue(run("x.apk!assets/outro.spv", spv))
        finally:
            C.ALLOW = old


if __name__ == "__main__":
    unittest.main()
