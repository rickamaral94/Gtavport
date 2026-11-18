// The frame generation's shaders from the user's own Lossless.dll (docs/FRAMEGEN.md § B1): read through the
// descriptor the picker gave, its compute shaders (RT_RCDATA resources) translated from DXBC to SPIR-V, checked, and
// installed whole into <root>/lsfg/spirv (filesDir: private to the app) - never the DLL itself.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#include <functional>
#include <string>
#include <vector>

namespace lsfg {

enum Result {
    OK = 0,
    READ,         // the file couldn't be read (or is larger than any Lossless.dll)
    NOT_PE,       // not a Windows DLL
    MISSING,      // a shader the framegen needs isn't there: another version of Lossless Scaling
    TRANSLATE,    // a shader didn't translate
    INVALID,      // the SPIR-V failed validation
    BINDINGS,     // a shader's bindings aren't the ones the framegen gives it
    WRITE,        // the shaders couldn't be written
};

// DXBC -> SPIR-V (lsfg_translate.cpp: DXVK's dxbc, the bindings numbered as the framegen expects). false: why in err.
using Translator = std::function<bool(const std::vector<uint8_t> &dxbc, std::vector<uint32_t> &spirv, std::string &err)>;
// A SPIR-V module's validation (SPIRV-Tools; empty: none). false: why in err.
using Validator = std::function<bool(const std::vector<uint32_t> &spirv, std::string &err)>;

struct Report {
    std::string detail;        // what failed, or what was installed
    std::string dll_sha256;
    int shaders = 0;
    std::vector<std::string> capabilities;  // the union of the shaders' OpCapability (the device's required features)
};

// The whole extraction from a DLL in memory (tests) or from a descriptor (the app: owned from here on).
Result extract(const uint8_t *dll, size_t size, const std::string &root, const Translator &tr, const Validator &val, Report &rep);
Result extract_fd(int fd, const std::string &root, const Translator &tr, const Validator &val, Report &rep);

// At startup: an interrupted install's leftovers (lsfg/spirv.importing, lsfg/spirv.old) go, the last good set stays.
void recover(const std::string &root);
// The installed set: true when complete, from this extractor's version, with every file there. summary: its line.
bool installed(const std::string &root, std::string *summary, std::vector<std::string> *capabilities = nullptr);
// "Remove shaders": lsfg/ goes.
void remove_all(const std::string &root);

// (the names the framegen loads, 3.1 and 3.1P, and their resource ids in the DLL)
struct Shader { const char *name; uint32_t id; };
const std::vector<Shader> &shader_table();
// (tests) a SPIR-V module's descriptor bindings by kind: uniform buffers, samplers, sampled images, storage images
bool spirv_bindings(const std::vector<uint32_t> &spirv, int counts[4], std::vector<std::string> &caps, std::string &err);

}  // namespace lsfg

extern "C" {
#endif

// (the app) the default translator and validator, the state for the menus
int lsfg_import_fd(int fd, char *detail, size_t n);  // lsfg::Result
int lsfg_state(char *line, size_t n);                // 0 none, 1 installed, 2 importing
void lsfg_remove(void);
void lsfg_startup(void);
// (the frame generation, vulkan/framegen.c) the installed shaders' folder and their capabilities, space-separated:
// 1 when a complete set is installed
int lsfg_installed_set(char *dir, size_t ndir, char *caps, size_t ncaps);

#ifdef __cplusplus
}
#endif
