// Gtavport — a importação do Lossless.dll recusa entradas inválidas sem travar (sem DLL nenhum: só lixo).
#include "lsfg_extract.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace lsfg { bool dxbc_translate(const std::vector<uint8_t>&, std::vector<uint32_t>&, std::string&);
                 bool spirv_validate(const std::vector<uint32_t>&, std::string&); }
int main(int, char** argv) {
  std::string root = argv[1];
  int fails = 0;
  auto check = [&](const char* what, bool ok) { std::printf("%s: %s\n", ok ? "ok" : "FALHOU", what); fails += !ok; };
  lsfg::Report rep;
  std::vector<uint8_t> junk(4096, 0x41);
  check("lixo -> NOT_PE", lsfg::extract(junk.data(), junk.size(), root, lsfg::dxbc_translate, lsfg::spirv_validate, rep) == lsfg::NOT_PE);
  std::vector<uint8_t> mz(4096, 0); mz[0]='M'; mz[1]='Z';
  lsfg::Result r = lsfg::extract(mz.data(), mz.size(), root, lsfg::dxbc_translate, lsfg::spirv_validate, rep);
  check("MZ sem PE -> recusado", r != lsfg::OK);
  std::vector<uint32_t> out; std::string err;
  check("dxbc com lixo falha limpo", !lsfg::dxbc_translate(junk, out, err));
  check("nada instalado", !lsfg::installed(root, nullptr));
  check("tabela com 52 shaders", lsfg::shader_table().size() == 52);
  return fails;
}
