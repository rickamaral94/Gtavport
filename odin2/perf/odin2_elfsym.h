// Gtavport (odin2/perf) — nomes de função a partir da tabela de símbolos (.symtab) de um ELF64 no disco.
// A libgtav_game.so compilada no aparelho não é "stripada": cada função recompilada do jogo tem o nome
// sub_<endereço no Xbox 360> na .symtab (só as da interface ficam na .dynsym, que o dladdr vê).
// O diagnóstico usa isto para dizer em que funções do jogo o tempo de CPU vai.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <elf.h>

namespace gtav::native::perf {

class ElfSymbols {
 public:
  // Lê a .symtab (ou, sem ela, a .dynsym) de path. false: arquivo ilegível ou sem símbolos.
  bool Load(const std::string& path) {
    syms_.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size < long(sizeof(Elf64_Ehdr))) {
      std::fclose(f);
      return false;
    }
    Elf64_Ehdr eh;
    bool ok = std::fread(&eh, sizeof eh, 1, f) == 1 && std::memcmp(eh.e_ident, ELFMAG, SELFMAG) == 0 &&
              eh.e_ident[EI_CLASS] == ELFCLASS64 && eh.e_shentsize == sizeof(Elf64_Shdr) && eh.e_shnum > 0 &&
              eh.e_shoff + uint64_t(eh.e_shnum) * sizeof(Elf64_Shdr) <= uint64_t(size);
    std::vector<Elf64_Shdr> sh;
    if (ok) {
      sh.resize(eh.e_shnum);
      std::fseek(f, long(eh.e_shoff), SEEK_SET);
      ok = std::fread(sh.data(), sizeof(Elf64_Shdr), sh.size(), f) == sh.size();
    }
    for (int pass = 0; ok && pass < 2 && syms_.empty(); pass++) {
      const uint32_t want = pass == 0 ? SHT_SYMTAB : SHT_DYNSYM;
      for (const Elf64_Shdr& s : sh) {
        if (s.sh_type != want || s.sh_link >= sh.size() || s.sh_entsize != sizeof(Elf64_Sym)) continue;
        const Elf64_Shdr& str = sh[s.sh_link];
        if (s.sh_offset + s.sh_size > uint64_t(size) || str.sh_offset + str.sh_size > uint64_t(size)) continue;
        std::vector<char> names(str.sh_size + 1, 0);
        std::vector<Elf64_Sym> table(s.sh_size / sizeof(Elf64_Sym));
        std::fseek(f, long(str.sh_offset), SEEK_SET);
        if (std::fread(names.data(), 1, str.sh_size, f) != str.sh_size) continue;
        std::fseek(f, long(s.sh_offset), SEEK_SET);
        if (std::fread(table.data(), sizeof(Elf64_Sym), table.size(), f) != table.size()) continue;
        for (const Elf64_Sym& y : table) {
          if (ELF64_ST_TYPE(y.st_info) != STT_FUNC || y.st_value == 0 || y.st_name >= str.sh_size) continue;
          syms_.push_back({y.st_value, y.st_size, std::string(names.data() + y.st_name)});
        }
      }
    }
    std::fclose(f);
    std::sort(syms_.begin(), syms_.end(), [](const Sym& a, const Sym& b) { return a.addr < b.addr; });
    return !syms_.empty();
  }

  // Função que contém o endereço (relativo à base do módulo); nullptr fora de qualquer uma.
  const std::string* Find(uint64_t rel) const {
    auto it = std::upper_bound(syms_.begin(), syms_.end(), rel, [](uint64_t v, const Sym& s) { return v < s.addr; });
    if (it == syms_.begin()) return nullptr;
    --it;
    if (it->size && rel >= it->addr + it->size) return nullptr;
    return &it->name;
  }

  size_t Count() const { return syms_.size(); }

 private:
  struct Sym {
    uint64_t addr, size;
    std::string name;
  };
  std::vector<Sym> syms_;
};

}  // namespace gtav::native::perf
