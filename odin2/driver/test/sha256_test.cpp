// Testes no host do SHA-256 do relatório de driver: vetores do FIPS 180-4 e arquivos contra o sha256sum.
#include "../odin2_sha256.h"

#include <cstdio>
#include <cstdlib>
#include <string>

static int g_fail = 0;
static void Check(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "FALHOU %s: %s != %s\n", what, got.c_str(), want.c_str());
    g_fail++;
  }
}

int main(int argc, char** argv) {
  auto hex = [](const std::string& s) {
    odin2::Sha256 h;
    h.Update(s.data(), s.size());
    return h.HexDigest();
  };
  Check(hex(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "vazio");
  Check(hex("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "abc");
  Check(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "448 bits");
  Check(hex(std::string(1000000, 'a')), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "1e6 x a");
  // argv: pares arquivo esperado (do sha256sum, no run.sh)
  for (int i = 1; i + 1 < argc; i += 2) Check(odin2::Sha256File(argv[i]), argv[i + 1], argv[i]);
  if (g_fail) return 1;
  std::puts("sha256_test: ok");
}
