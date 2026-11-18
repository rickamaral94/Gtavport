// A C interface to lsfg-vk's framegen (third_party/lsfg-vk-android/framegen, LSFG 3.1), built into its own shared
// library (liblsfg_framegen.so) with its own volk, nothing exported but these: the framegen's adopted device nulls volk's
// vkCmdPipelineBarrier2 pointer, which must not be ours (docs/FRAMEGEN.md). No exception crosses it: each call returns
// 0, or LSFGFG_DEVICE_LOST, or another failure, with the reason in err.
#pragma once
#include <stddef.h>
#include <stdint.h>

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { LSFGFG_OK = 0, LSFGFG_FAILED = 1, LSFGFG_DEVICE_LOST = 2 };

// Once, on the caller's device and queue (all framegen work is submitted there, by the caller's thread). spirv_dir: the
// shaders lsfg_extract installed (<name>.spv). flow_scale: lsfg-vk's (0.25..1, 1 the motion at the frame's size);
// generations: frames made per real one; performance: LSFG 3.1P (lighter). lsfgfg_finalize before another init.
int lsfgfg_init(PFN_vkGetInstanceProcAddr gipa, VkInstance instance, VkPhysicalDevice phys, VkDevice device, uint32_t queue_family,
                VkQueue queue, float flow_scale, int generations, int performance, const char *spirv_dir, char *err, size_t n);
// A context over the caller's images (GENERAL layout, kept by the caller): two inputs (the real frame of number k in
// in[k % 2]) and one output per generated frame. format: the views' (storage-capable).
int lsfgfg_create(VkImage in0, VkImage in1, const VkImage *out, int nout, uint32_t width, uint32_t height, VkFormat format,
                  int32_t *id, char *err, size_t n);
// The next real frame is in its input: the generated frames between it and the one before, submitted to the queue
// (no semaphores: ordered by the queue; the caller's barriers order its own reads and writes).
int lsfgfg_present(int32_t id, char *err, size_t n);
int lsfgfg_delete(int32_t id, char *err, size_t n);
void lsfgfg_finalize(void);

#ifdef __cplusplus
}
#endif
