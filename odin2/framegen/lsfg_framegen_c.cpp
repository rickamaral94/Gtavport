// lsfg_framegen_c.h: the framegen's C++ API (lsfg_3_1.hpp) behind a C one, exceptions caught here.
#include "lsfg_framegen_c.h"

#include <stdio.h>
#include <string.h>

#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "common/exception.hpp"
#include "lsfg_3_1.hpp"
#include "lsfg_3_1p.hpp"

#define EXPORT extern "C" __attribute__((visibility("default")))

namespace {

std::string g_dir;
bool g_perf;  // (LSFG 3.1P: the performance mode's lighter shaders, p_*)

int fail(const char *what, char *err, size_t n) {
    try {
        throw;
    } catch (const LSFG::vulkan_error &e) {
        snprintf(err, n, "%s: %s (VkResult %d)", what, e.what(), int(e.error()));
        return e.error() == VK_ERROR_DEVICE_LOST ? LSFGFG_DEVICE_LOST : LSFGFG_FAILED;
    } catch (const std::exception &e) {
        snprintf(err, n, "%s: %s", what, e.what());
    } catch (...) {
        snprintf(err, n, "%s: failed", what);
    }
    return LSFGFG_FAILED;
}

// The loader the framegen asks for each shader by name: <dir>/<name, [ and ] as _>.spv (lsfg_extract.cpp)
std::vector<uint8_t> load(const std::string &name) {
    std::string f = name;
    for (auto &c : f) if (c == '[' || c == ']') c = '_';
    std::ifstream in(g_dir + "/" + f + ".spv", std::ios::binary);
    std::vector<uint8_t> v((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return v;  // (empty: the framegen reports the shader as missing)
}

}  // namespace

EXPORT int lsfgfg_init(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, VkPhysicalDevice phys, VkDevice device,
                       uint32_t queue_family, VkQueue queue, float flow_scale, int generations, int performance,
                       const char *spirv_dir, char *err, size_t n) {
    try {
        g_dir = spirv_dir;
        g_perf = performance != 0;
        // (the framegen takes the inverse: its motion images are the frame's size divided by it - lsfg-vk's layer
        // passes 1 / flow_scale too)
        float inv = 1.0f / (flow_scale < 0.25f ? 0.25f : flow_scale > 1.0f ? 1.0f : flow_scale);
        if (g_perf)
            LSFG_3_1P::initializeExternal(gipa, instance, phys, device, queue_family, queue, false, inv, uint64_t(generations), load);
        else
            LSFG_3_1::initializeExternal(gipa, instance, phys, device, queue_family, queue, false, inv, uint64_t(generations), load);
        return LSFGFG_OK;
    } catch (...) {
        return fail("initialize", err, n);
    }
}

EXPORT int lsfgfg_create(VkImage in0, VkImage in1, const VkImage *out, int nout, uint32_t width, uint32_t height, VkFormat format,
                         int32_t *id, char *err, size_t n) {
    try {
        std::vector<VkImage> o(out, out + nout);
        *id = g_perf ? LSFG_3_1P::createContextFromImages(in0, in1, o, VkExtent2D{width, height}, format)
                     : LSFG_3_1::createContextFromImages(in0, in1, o, VkExtent2D{width, height}, format);
        return LSFGFG_OK;
    } catch (...) {
        return fail("create context", err, n);
    }
}

EXPORT int lsfgfg_present(int32_t id, char *err, size_t n) {
    try {
        if (g_perf) LSFG_3_1P::presentContext(id, -1, {});
        else LSFG_3_1::presentContext(id, -1, {});
        return LSFGFG_OK;
    } catch (...) {
        return fail("present", err, n);
    }
}

EXPORT int lsfgfg_delete(int32_t id, char *err, size_t n) {
    try {
        if (g_perf) LSFG_3_1P::deleteContext(id);
        else LSFG_3_1::deleteContext(id);
        return LSFGFG_OK;
    } catch (...) {
        return fail("delete context", err, n);
    }
}

EXPORT void lsfgfg_finalize(void) {
    try {
        if (g_perf) LSFG_3_1P::finalize();
        else LSFG_3_1::finalize();
    } catch (...) {
    }
}
