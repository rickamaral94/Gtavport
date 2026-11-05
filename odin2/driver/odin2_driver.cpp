// Gtavport — seleção do driver Vulkan via libadrenotools. Ver odin2_driver.h.
#include "odin2_driver.h"

#include <adrenotools/driver.h>
#include <android/log.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

constexpr const char* kTag = "Odin2Driver";
std::once_flag g_once;
void* g_handle = nullptr;
std::string g_name = "system";

const char* Env(const char* name) {
  const char* v = std::getenv(name);
  return (v && *v) ? v : nullptr;
}

// Resultado para a tela "Driver gráfico" (ODIN2_VK_STATUS_FILE): pedido, efetivo e motivo.
void WriteStatus(const char* requested, const char* effective, const char* detail) {
  const char* path = Env("ODIN2_VK_STATUS_FILE");
  if (!path) return;
  if (FILE* f = std::fopen(path, "w")) {
    std::fprintf(f, "requested=%s\neffective=%s\ndetail=%s\n", requested, effective, detail ? detail : "");
    std::fclose(f);
  }
}

std::string WithSlash(const char* dir) {
  std::string s(dir);
  if (!s.empty() && s.back() != '/') s += '/';
  return s;
}

void Open() {
  const char* name = Env("ODIN2_VK_DRIVER");
  if (!name || std::string(name) == "system") {
    __android_log_print(ANDROID_LOG_INFO, kTag, "driver: system (sem driver customizado)");
    WriteStatus("system", "system", "");
    return;
  }
  const char* dir = Env("ODIN2_VK_DRIVER_DIR");
  const char* lib = Env("ODIN2_VK_DRIVER_LIB");
  const char* hooks = Env("ODIN2_VK_HOOK_DIR");
  const char* tmp = Env("ODIN2_VK_TMP_DIR");
  if (!dir || !lib || !hooks) {
    __android_log_print(ANDROID_LOG_ERROR, kTag,
                        "driver '%s' pedido mas ODIN2_VK_DRIVER_DIR/LIB/HOOK_DIR incompletos: usando system",
                        name);
    WriteStatus(name, "system", "variáveis ODIN2_VK_* incompletas");
    return;
  }
  std::string tmp_dir = tmp ? WithSlash(tmp) : std::string();
  if (!tmp_dir.empty()) mkdir(tmp_dir.c_str(), 0700);
  std::string drv_dir = WithSlash(dir), hook_dir = WithSlash(hooks);
  void* h = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM,
                                       tmp_dir.empty() ? nullptr : tmp_dir.c_str(), hook_dir.c_str(),
                                       drv_dir.c_str(), lib, nullptr, nullptr);
  if (!h) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "adrenotools falhou para '%s' (%s%s): usando system: %s",
                        name, drv_dir.c_str(), lib, dlerror());
    WriteStatus(name, "system", "adrenotools não conseguiu abrir o driver");
    return;
  }
  if (!dlsym(h, "vkGetInstanceProcAddr")) {
    __android_log_print(ANDROID_LOG_ERROR, kTag, "'%s' sem vkGetInstanceProcAddr: usando system", name);
    WriteStatus(name, "system", "driver sem vkGetInstanceProcAddr");
    return;
  }
  g_handle = h;
  g_name = name;
  __android_log_print(ANDROID_LOG_INFO, kTag, "driver: %s (%s%s)", name, drv_dir.c_str(), lib);
  WriteStatus(name, name, lib);
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void* odin2_vulkan_open(void) {
  std::call_once(g_once, Open);
  return g_handle;
}

extern "C" __attribute__((visibility("default"))) const char* odin2_vulkan_driver_name(void) {
  std::call_once(g_once, Open);
  return g_name.c_str();
}
