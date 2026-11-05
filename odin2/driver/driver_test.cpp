// Gtavport — "Testar" da tela Driver gráfico: carrega um driver (ou o do sistema), cria uma
// instância Vulkan e devolve o que o jogo vai encontrar. Roda num processo separado
// (Odin2DriverTestActivity, android:process=":drivertest"): o adrenotools só pode trocar o driver
// uma vez por processo, e um driver quebrado não pode derrubar o jogo.
#include <adrenotools/driver.h>
#include <dlfcn.h>
#include <jni.h>
#include <vulkan/vulkan.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string Str(JNIEnv* env, jstring s) {
  if (!s) return {};
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r(c);
  env->ReleaseStringUTFChars(s, c);
  return r;
}

std::string Esc(const char* s) {
  std::string o;
  for (; s && *s; ++s) {
    if (*s == '"' || *s == '\\') o += '\\';
    if (static_cast<unsigned char>(*s) >= 0x20) o += *s;
  }
  return o;
}

std::string Ver(uint32_t v) {
  char b[32];
  std::snprintf(b, sizeof b, "%u.%u.%u", VK_API_VERSION_MAJOR(v), VK_API_VERSION_MINOR(v), VK_API_VERSION_PATCH(v));
  return b;
}

std::string Error(const std::string& msg) { return "{\"ok\":false,\"error\":\"" + Esc(msg.c_str()) + "\"}"; }

std::string Describe(void* h);

std::string Test(const std::string& dir, const std::string& lib, const std::string& hooks, const std::string& tmp) {
  void* h = nullptr;
  if (dir.empty()) {
    h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
  } else {
    h = adrenotools_open_libvulkan(RTLD_NOW | RTLD_LOCAL, ADRENOTOOLS_DRIVER_CUSTOM, tmp.empty() ? nullptr : tmp.c_str(),
                                   hooks.c_str(), dir.c_str(), lib.c_str(), nullptr, nullptr);
  }
  if (!h) return Error(std::string("não carregou: ") + (dlerror() ? dlerror() : "adrenotools"));
  return Describe(h);
}

// Instância Vulkan pelo handle `h` (driver já carregado) e o que o jogo vai encontrar.
std::string Describe(void* h) {
  auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(h, "vkGetInstanceProcAddr"));
  if (!gipa) return Error("sem vkGetInstanceProcAddr");
  auto create = reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr, "vkCreateInstance"));
  auto enumVer = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(gipa(nullptr, "vkEnumerateInstanceVersion"));
  uint32_t instVer = VK_API_VERSION_1_0;
  if (enumVer) enumVer(&instVer);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "gtavport-driver-test";
  app.apiVersion = instVer >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
  VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ci.pApplicationInfo = &app;
  VkInstance inst = VK_NULL_HANDLE;
  if (!create || create(&ci, nullptr, &inst) != VK_SUCCESS) return Error("vkCreateInstance falhou");
#define GET(name) auto name = reinterpret_cast<PFN_##name>(gipa(inst, #name))
  GET(vkEnumeratePhysicalDevices);
  GET(vkGetPhysicalDeviceProperties);
  GET(vkGetPhysicalDeviceProperties2);
  GET(vkGetPhysicalDeviceFeatures);
  GET(vkEnumerateDeviceExtensionProperties);
  GET(vkDestroyInstance);
#undef GET
  uint32_t n = 0;
  vkEnumeratePhysicalDevices(inst, &n, nullptr);
  if (n == 0) {
    vkDestroyInstance(inst, nullptr);
    return Error("nenhuma GPU enumerada (hook do adrenotools falhou?)");
  }
  std::vector<VkPhysicalDevice> devs(n);
  vkEnumeratePhysicalDevices(inst, &n, devs.data());
  VkPhysicalDeviceProperties p{};
  vkGetPhysicalDeviceProperties(devs[0], &p);
  std::string driverName, driverInfo;
  if (vkGetPhysicalDeviceProperties2 && p.apiVersion >= VK_API_VERSION_1_2) {
    VkPhysicalDeviceDriverProperties dp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &dp};
    vkGetPhysicalDeviceProperties2(devs[0], &p2);
    driverName = dp.driverName;
    driverInfo = dp.driverInfo;
  }
  VkPhysicalDeviceFeatures f{};
  vkGetPhysicalDeviceFeatures(devs[0], &f);
  uint32_t ne = 0;
  vkEnumerateDeviceExtensionProperties(devs[0], nullptr, &ne, nullptr);
  std::vector<VkExtensionProperties> exts(ne);
  vkEnumerateDeviceExtensionProperties(devs[0], nullptr, &ne, exts.data());
  auto has = [&](const char* e) {
    for (auto& x : exts) if (std::string(x.extensionName) == e) return true;
    return false;
  };
  std::string out = "{\"ok\":true";
  out += ",\"device\":\"" + Esc(p.deviceName) + "\"";
  out += ",\"api\":\"" + Ver(p.apiVersion) + "\"";
  out += ",\"driverVersionRaw\":" + std::to_string(p.driverVersion);
  out += ",\"driverName\":\"" + Esc(driverName.c_str()) + "\"";
  out += ",\"driverInfo\":\"" + Esc(driverInfo.c_str()) + "\"";
  out += ",\"extensions\":" + std::to_string(ne);
  // O que o renderer do upstream contorna no driver proprietário (docs/ANDROID_STATUS.md).
  out += std::string(",\"shaderInt64\":") + (f.shaderInt64 ? "true" : "false");
  out += std::string(",\"geometryShader\":") + (f.geometryShader ? "true" : "false");
  out += std::string(",\"graphicsPipelineLibrary\":") + (has("VK_EXT_graphics_pipeline_library") ? "true" : "false");
  out += std::string(",\"fragmentShadingRate\":") + (has("VK_KHR_fragment_shading_rate") ? "true" : "false");
  out += "}";
  vkDestroyInstance(inst, nullptr);
  return out;
}

}  // namespace

// Para o jogo (odin2/perf): o driver em uso neste processo (o mesmo handle de odin2_vulkan_open, ou o
// carregador do sistema), em JSON. Calculado uma vez; chamar fora da thread de render.
extern "C" void* odin2_vulkan_open(void);
extern "C" __attribute__((visibility("default"))) const char* odin2_vulkan_describe(void) {
  static std::string cached;
  static bool done = false;
  if (!done) {
    void* h = odin2_vulkan_open();
    if (!h) h = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    cached = h ? Describe(h) : Error("libvulkan.so não carregou");
    done = true;
  }
  return cached.c_str();
}

extern "C" JNIEXPORT jstring JNICALL Java_com_gtavrecomp_app_Odin2DriverTestActivity_nativeTest(
    JNIEnv* env, jclass, jstring dir, jstring lib, jstring hooks, jstring tmp) {
  return env->NewStringUTF(Test(Str(env, dir), Str(env, lib), Str(env, hooks), Str(env, tmp)).c_str());
}
