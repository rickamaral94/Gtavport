// Gtavport — teste dos passes de odin2_post.hlsl numa GPU Vulkan de software (lavapipe), com o mesmo
// contrato do jogo: texturas bindless no set 0, samplers no set 1, push constants de 64 bytes.
// Não substitui o teste no Adreno: verifica o port dos shaders (coordenadas, pesos, NaN, limites).
//   odin2/post/test/run.sh
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "../third_party/smaa/AreaTex.h"
#include "../third_party/smaa/SearchTex.h"

#define CK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { fprintf(stderr, "%s = %d (linha %d)\n", #x, r_, __LINE__); exit(2); } } while (0)

static VkInstance inst;
static VkPhysicalDevice pd;
static VkDevice dev;
static VkQueue q;
static uint32_t qf;
static VkCommandPool pool;
static VkDescriptorSetLayout tex_layout, smp_layout;
static VkPipelineLayout pl;
static VkDescriptorPool dpool;
static VkDescriptorSet tex_set, smp_set;
static VkSampler samplers[2];
static const char* spv_dir;
static int failures;

typedef struct {
  float a[4], b[4];
  uint32_t t0, t1, t2, lin, pt, pad[3];
} Push;

typedef struct {
  VkImage img;
  VkDeviceMemory mem;
  VkImageView view;
  uint32_t w, h;
  VkFormat fmt;
  int bpp;
} Img;

static uint32_t mem_type(uint32_t bits, VkMemoryPropertyFlags f) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(pd, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
    if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & f) == f) return i;
  exit(3);
}

static void init(void) {
  VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO, .apiVersion = VK_API_VERSION_1_3};
  VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
  CK(vkCreateInstance(&ici, NULL, &inst));
  uint32_t n = 1;
  vkEnumeratePhysicalDevices(inst, &n, &pd);
  if (!n) { fprintf(stderr, "sem GPU Vulkan\n"); exit(2); }
  VkPhysicalDeviceProperties pp;
  vkGetPhysicalDeviceProperties(pd, &pp);
  printf("GPU: %s\n", pp.deviceName);
  qf = 0;
  float prio = 1;
  VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = qf, .queueCount = 1, .pQueuePriorities = &prio};
  VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                          .runtimeDescriptorArray = VK_TRUE, .descriptorBindingPartiallyBound = VK_TRUE};
  VkPhysicalDeviceVulkan13Features f13 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &f12, .dynamicRendering = VK_TRUE};
  VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &f13, .queueCreateInfoCount = 1, .pQueueCreateInfos = &qci};
  CK(vkCreateDevice(pd, &dci, NULL, &dev));
  vkGetDeviceQueue(dev, qf, 0, &q);
  VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = qf};
  CK(vkCreateCommandPool(dev, &cpi, NULL, &pool));
  // Heaps como os do jogo: arrays grandes, parcialmente preenchidos.
  VkDescriptorBindingFlags bf = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
  VkDescriptorSetLayoutBindingFlagsCreateInfo bfi = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO, .bindingCount = 1, .pBindingFlags = &bf};
  VkDescriptorSetLayoutBinding tb = {0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64, VK_SHADER_STAGE_ALL, NULL};
  VkDescriptorSetLayoutCreateInfo tl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &bfi, .bindingCount = 1, .pBindings = &tb};
  CK(vkCreateDescriptorSetLayout(dev, &tl, NULL, &tex_layout));
  VkDescriptorSetLayoutBinding sb = {0, VK_DESCRIPTOR_TYPE_SAMPLER, 8, VK_SHADER_STAGE_ALL, NULL};
  VkDescriptorSetLayoutCreateInfo sl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .pNext = &bfi, .bindingCount = 1, .pBindings = &sb};
  CK(vkCreateDescriptorSetLayout(dev, &sl, NULL, &smp_layout));
  VkDescriptorSetLayout sets[2] = {tex_layout, smp_layout};
  VkPushConstantRange pcr = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 2, .pSetLayouts = sets, .pushConstantRangeCount = 1, .pPushConstantRanges = &pcr};
  CK(vkCreatePipelineLayout(dev, &pli, NULL, &pl));
  VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 64}, {VK_DESCRIPTOR_TYPE_SAMPLER, 8}};
  VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 2, .poolSizeCount = 2, .pPoolSizes = ps};
  CK(vkCreateDescriptorPool(dev, &dpi, NULL, &dpool));
  VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = dpool, .descriptorSetCount = 2, .pSetLayouts = sets};
  VkDescriptorSet out[2];
  CK(vkAllocateDescriptorSets(dev, &dai, out));
  tex_set = out[0];
  smp_set = out[1];
  for (int i = 0; i < 2; i++) {  // 0 = linear, 1 = point; clamp (como o jogo)
    VkSamplerCreateInfo si = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .magFilter = i ? VK_FILTER_NEAREST : VK_FILTER_LINEAR,
                              .minFilter = i ? VK_FILTER_NEAREST : VK_FILTER_LINEAR, .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                              .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE};
    CK(vkCreateSampler(dev, &si, NULL, &samplers[i]));
    VkDescriptorImageInfo ii = {.sampler = samplers[i]};
    VkWriteDescriptorSet w = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = smp_set, .dstArrayElement = (uint32_t)i, .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER, .pImageInfo = &ii};
    vkUpdateDescriptorSets(dev, 1, &w, 0, NULL);
  }
}

static VkCommandBuffer begin(void) {
  VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = pool, .commandBufferCount = 1};
  VkCommandBuffer cb;
  CK(vkAllocateCommandBuffers(dev, &ai, &cb));
  VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  vkBeginCommandBuffer(cb, &bi);
  return cb;
}
static void submit(VkCommandBuffer cb) {
  vkEndCommandBuffer(cb);
  VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
  CK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE));
  vkQueueWaitIdle(q);
  vkFreeCommandBuffers(dev, pool, 1, &cb);
}
static void barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to) {
  VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT, .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                            .oldLayout = from, .newLayout = to, .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .image = img,
                            .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, NULL, 0, NULL, 1, &b);
}

static VkBuffer staging(VkDeviceSize size, VkDeviceMemory* mem, void** map) {
  VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, .size = size, .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
  VkBuffer b;
  CK(vkCreateBuffer(dev, &bi, NULL, &b));
  VkMemoryRequirements r;
  vkGetBufferMemoryRequirements(dev, b, &r);
  VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = r.size,
                             .memoryTypeIndex = mem_type(r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
  CK(vkAllocateMemory(dev, &ai, NULL, mem));
  vkBindBufferMemory(dev, b, *mem, 0);
  vkMapMemory(dev, *mem, 0, size, 0, map);
  return b;
}

// slot: índice no heap de texturas (o "descritor" que o shader recebe).
static Img image(uint32_t w, uint32_t h, VkFormat fmt, int bpp, const void* data, uint32_t slot) {
  Img m = {.w = w, .h = h, .fmt = fmt, .bpp = bpp};
  VkImageCreateInfo ii = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .imageType = VK_IMAGE_TYPE_2D, .format = fmt, .extent = {w, h, 1}, .mipLevels = 1, .arrayLayers = 1,
                          .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                          .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
  CK(vkCreateImage(dev, &ii, NULL, &m.img));
  VkMemoryRequirements r;
  vkGetImageMemoryRequirements(dev, m.img, &r);
  VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .allocationSize = r.size, .memoryTypeIndex = mem_type(r.memoryTypeBits, 0)};
  CK(vkAllocateMemory(dev, &ai, NULL, &m.mem));
  vkBindImageMemory(dev, m.img, m.mem, 0);
  VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, .image = m.img, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = fmt,
                              .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
  CK(vkCreateImageView(dev, &vi, NULL, &m.view));
  VkCommandBuffer cb = begin();
  if (data) {
    VkDeviceMemory sm;
    void* p;
    VkBuffer sb = staging((VkDeviceSize)w * h * bpp, &sm, &p);
    memcpy(p, data, (size_t)w * h * bpp);
    barrier(cb, m.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy c = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {w, h, 1}};
    vkCmdCopyBufferToImage(cb, sb, m.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    barrier(cb, m.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    submit(cb);
  } else {
    barrier(cb, m.img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    submit(cb);
  }
  VkDescriptorImageInfo di = {.imageView = m.view, .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet wd = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = tex_set, .dstArrayElement = slot, .descriptorCount = 1,
                             .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .pImageInfo = &di};
  vkUpdateDescriptorSets(dev, 1, &wd, 0, NULL);
  return m;
}

static VkShaderModule module_path(const char* path);
static VkShaderModule module(const char* name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.spv", spv_dir, name);
  return module_path(path);
}
static VkShaderModule module_path(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "sem %s\n", path); exit(2); }
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  uint32_t* code = malloc((size_t)n);
  if (fread(code, 1, (size_t)n, f) != (size_t)n) exit(2);
  fclose(f);
  VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, .codeSize = (size_t)n, .pCode = code};
  VkShaderModule m;
  CK(vkCreateShaderModule(dev, &ci, NULL, &m));
  free(code);
  return m;
}

static void draw(VkShaderModule vs, const char* vs_entry, VkShaderModule ps, const char* ps_entry, VkPipelineLayout layout,
                 VkDescriptorSet* sets, uint32_t nsets, Img* out, const void* push, uint32_t push_size);
static void pass(const char* ps_name, Img* out, uint32_t t0, uint32_t t1, uint32_t t2, const float a[4], const float b[4]) {
  VkShaderModule vs = module("VSPost"), ps = module(ps_name);
  Push p = {.t0 = t0, .t1 = t1, .t2 = t2, .lin = 0, .pt = 1};
  memcpy(p.a, a, 16);
  memcpy(p.b, b, 16);
  VkDescriptorSet sets[2] = {tex_set, smp_set};
  draw(vs, "VSPost", ps, ps_name, pl, sets, 2, out, &p, sizeof p);
  vkDestroyShaderModule(dev, vs, NULL);
  vkDestroyShaderModule(dev, ps, NULL);
}
static void draw(VkShaderModule vs, const char* vs_entry, VkShaderModule ps, const char* ps_entry, VkPipelineLayout layout,
                 VkDescriptorSet* sets, uint32_t nsets, Img* out, const void* push, uint32_t push_size) {
  VkPipelineShaderStageCreateInfo st[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vs, .pName = vs_entry},
                                           {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = ps, .pName = ps_entry}};
  VkPipelineVertexInputStateCreateInfo vi = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  VkViewport vp = {0, 0, (float)out->w, (float)out->h, 0, 1};
  VkRect2D sc = {{0, 0}, {out->w, out->h}};
  VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .pViewports = &vp, .scissorCount = 1, .pScissors = &sc};
  VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO, .cullMode = VK_CULL_MODE_NONE, .lineWidth = 1};
  VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  VkPipelineColorBlendAttachmentState ba = {.colorWriteMask = 0xF};
  VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &ba};
  VkPipelineRenderingCreateInfo ri = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO, .colorAttachmentCount = 1, .pColorAttachmentFormats = &out->fmt};
  VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, .pNext = &ri, .stageCount = 2, .pStages = st, .pVertexInputState = &vi,
                                     .pInputAssemblyState = &ia, .pViewportState = &vps, .pRasterizationState = &rs, .pMultisampleState = &ms,
                                     .pColorBlendState = &cbs, .layout = layout};
  VkPipeline pipe;
  CK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipe));
  VkCommandBuffer cb = begin();
  barrier(cb, out->img, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  VkRenderingAttachmentInfo ca = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO, .imageView = out->view, .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE, .storeOp = VK_ATTACHMENT_STORE_OP_STORE};
  VkRenderingInfo rinfo = {VK_STRUCTURE_TYPE_RENDERING_INFO, .renderArea = sc, .layerCount = 1, .colorAttachmentCount = 1, .pColorAttachments = &ca};
  vkCmdBeginRendering(cb, &rinfo);
  vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
  vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, nsets, sets, 0, NULL);
  vkCmdPushConstants(cb, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, push_size, push);
  vkCmdDraw(cb, 3, 1, 0, 0);
  vkCmdEndRendering(cb);
  barrier(cb, out->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  submit(cb);
  vkDestroyPipeline(dev, pipe, NULL);
}

// Referência: o mesmo passe com o shader GLSL do BOTWRecompAndroid (sampler combinado no set 0,
// push {vec4 a; vec4 b}), rodando no mesmo lugar, para comparar o port.
static VkPipelineLayout botw_pl;
static VkDescriptorSetLayout botw_dsl;
static VkDescriptorPool botw_pool;
static void botw_init(void) {
  VkDescriptorSetLayoutBinding b[3];
  for (int i = 0; i < 3; i++) b[i] = (VkDescriptorSetLayoutBinding){(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_ALL, NULL};
  VkDescriptorSetLayoutCreateInfo li = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 3, .pBindings = b};
  CK(vkCreateDescriptorSetLayout(dev, &li, NULL, &botw_dsl));
  VkPushConstantRange r = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 32};
  VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, .setLayoutCount = 1, .pSetLayouts = &botw_dsl, .pushConstantRangeCount = 1, .pPushConstantRanges = &r};
  CK(vkCreatePipelineLayout(dev, &pli, NULL, &botw_pl));
  VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 300};
  VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, .maxSets = 100, .poolSizeCount = 1, .pPoolSizes = &ps};
  CK(vkCreateDescriptorPool(dev, &dpi, NULL, &botw_pool));
}
static void botw_pass(const char* dir, const char* frag, Img* out, Img* t0, const float a[4], const float b[4]) {
  char vp[600], fp[600];
  snprintf(vp, sizeof vp, "%s/post.vert.spv", dir);
  snprintf(fp, sizeof fp, "%s/%s.spv", dir, frag);
  VkShaderModule vs = module_path(vp), ps = module_path(fp);
  VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorPool = botw_pool, .descriptorSetCount = 1, .pSetLayouts = &botw_dsl};
  VkDescriptorSet set;
  CK(vkAllocateDescriptorSets(dev, &ai, &set));
  VkDescriptorImageInfo ii[3];
  VkWriteDescriptorSet w[3];
  for (int i = 0; i < 3; i++) {
    ii[i] = (VkDescriptorImageInfo){samplers[0], t0->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    w[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = (uint32_t)i, .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &ii[i]};
  }
  vkUpdateDescriptorSets(dev, 3, w, 0, NULL);
  float push[8];
  memcpy(push, a, 16);
  memcpy(push + 4, b, 16);
  draw(vs, "main", ps, "main", botw_pl, &set, 1, out, push, sizeof push);
  vkDestroyShaderModule(dev, vs, NULL);
  vkDestroyShaderModule(dev, ps, NULL);
}

static uint8_t* readback(Img* m) {
  VkDeviceMemory sm;
  void* p;
  size_t n = (size_t)m->w * m->h * m->bpp;
  VkBuffer sb = staging(n, &sm, &p);
  VkCommandBuffer cb = begin();
  barrier(cb, m->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  VkBufferImageCopy c = {.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .imageExtent = {m->w, m->h, 1}};
  vkCmdCopyImageToBuffer(cb, m->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sb, 1, &c);
  barrier(cb, m->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  submit(cb);
  uint8_t* out = malloc(n);
  memcpy(out, p, n);
  return out;
}

static void check(int ok, const char* what) {
  printf("  %s %s\n", ok ? "ok  " : "FALHA", what);
  if (!ok) failures++;
}

// ---------- imagens de teste (RGBA8, W x H) ----------
enum { W = 64, H = 48, OW = 96, OH = 72 };
static uint8_t in_flat[W * H * 4], in_grad[W * H * 4], in_step[W * H * 4], in_diag[W * H * 4];
static void make_inputs(void) {
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      uint8_t* f = in_flat + (y * W + x) * 4;
      f[0] = 77, f[1] = 153, f[2] = 51, f[3] = 255;
      uint8_t* g = in_grad + (y * W + x) * 4;  // gradiente suave em x e y
      g[0] = (uint8_t)(x * 255 / (W - 1)), g[1] = (uint8_t)(y * 255 / (H - 1)), g[2] = 128, g[3] = 255;
      uint8_t* s = in_step + (y * W + x) * 4;  // degrau vertical no meio
      uint8_t v = x < W / 2 ? 0 : 255;
      s[0] = s[1] = s[2] = v, s[3] = 255;
      uint8_t* d = in_diag + (y * W + x) * 4;  // borda inclinada (serrilhada) — o caso do AA
      uint8_t dv = (x * 3 > y * 4 + 20) ? 255 : 0;
      d[0] = d[1] = d[2] = dv, d[3] = 255;
    }
}

// Referência: o blit bilinear do upstream (uv do centro do pixel de saída, sampler linear, clamp).
static float bilinear_ref(const uint8_t* img, int c, float ox, float oy) {
  float x = (ox + 0.5f) * W / OW - 0.5f, y = (oy + 0.5f) * H / OH - 0.5f;
  int x0 = (int)floorf(x), y0 = (int)floorf(y);
  float fx = x - x0, fy = y - y0;
#define PX(xx, yy) (img[((((yy) < 0 ? 0 : (yy) >= H ? H - 1 : (yy)) * W) + ((xx) < 0 ? 0 : (xx) >= W ? W - 1 : (xx))) * 4 + c] / 255.0f)
  float v = (PX(x0, y0) * (1 - fx) + PX(x0 + 1, y0) * fx) * (1 - fy) + (PX(x0, y0 + 1) * (1 - fx) + PX(x0 + 1, y0 + 1) * fx) * fy;
#undef PX
  return v;
}

int main(int argc, char** argv) {
  spv_dir = argc > 1 ? argv[1] : ".";
  init();
  make_inputs();
  Img flat = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, in_flat, 1);
  Img grad = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, in_grad, 2);
  Img step = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, in_step, 3);
  Img diag = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, in_diag, 4);
  Img area = image(AREATEX_WIDTH, AREATEX_HEIGHT, VK_FORMAT_R8G8_UNORM, 2, areaTexBytes, 5);
  Img search = image(SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, VK_FORMAT_R8_UNORM, 1, searchTexBytes, 6);
  Img aa = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 10);
  Img edges = image(W, H, VK_FORMAT_R8G8_UNORM, 2, NULL, 11);
  Img weights = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 12);
  Img upA = image(OW, OH, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 13);
  Img upB = image(OW, OH, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 14);
  const float metrics[4] = {1.0f / W, 1.0f / H, W, H}, zero[4] = {0};
  const float sx = (float)W / OW, sy = (float)H / OH;
  const float easu_a[4] = {sx, sy, 0.5f * sx - 0.5f, 0.5f * sy - 0.5f}, easu_b[4] = {W, H, 0, 0};
  const float rcas_a[4] = {exp2f(-0.2f), 0, 0, 0}, rcas_b[4] = {OW, OH, 0, 0};
  const float sgsr_b[4] = {OW, OH, 0, 0};

  // ---- 1. imagem lisa: todos os passes devolvem a mesma cor ----
  printf("1. imagem lisa (todos os passes = identidade)\n");
  struct { const char* name; Img* out; int smaa; } aa_cases[] = {{"FXAA", &aa, 0}, {"SMAA", &aa, 1}};
  for (int k = 0; k < 2; k++) {
    if (aa_cases[k].smaa) {
      pass("PSSmaaEdge", &edges, 1, 0, 0, metrics, zero);
      pass("PSSmaaWeight", &weights, 11, 5, 6, metrics, zero);
      pass("PSSmaaBlend", &aa, 1, 12, 0, metrics, zero);
    } else {
      pass("PSFxaa", &aa, 1, 0, 0, metrics, zero);
    }
    uint8_t* o = readback(&aa);
    int maxd = 0;
    for (int i = 0; i < W * H; i++)
      for (int c = 0; c < 3; c++) maxd = fmax(maxd, abs(o[i * 4 + c] - in_flat[i * 4 + c]));
    char msg[96];
    snprintf(msg, sizeof msg, "%s: diferença máxima %d/255", aa_cases[k].name, maxd);
    check(maxd <= 1, msg);
    free(o);
  }
  {
    pass("PSEasu", &upA, 1, 0, 0, easu_a, easu_b);
    pass("PSRcas", &upB, 13, 0, 0, rcas_a, rcas_b);
    uint8_t* o = readback(&upB);
    pass("PSSgsr1", &upA, 1, 0, 0, metrics, sgsr_b);
    uint8_t* s = readback(&upA);
    int md = 0, ms = 0;
    for (int i = 0; i < OW * OH; i++)
      for (int c = 0; c < 3; c++) {
        md = fmax(md, abs(o[i * 4 + c] - in_flat[c]));
        ms = fmax(ms, abs(s[i * 4 + c] - in_flat[c]));
      }
    char msg[96];
    snprintf(msg, sizeof msg, "FSR 1 (EASU+RCAS): diferença máxima %d/255", md);
    check(md <= 1, msg);
    snprintf(msg, sizeof msg, "SGSR 1: diferença máxima %d/255", ms);
    check(ms <= 1, msg);
    free(o);
    free(s);
  }

  // ---- 2. gradiente suave: upscalers ~ bilinear (mapeamento de coordenadas sem meio texel errado) ----
  printf("2. gradiente suave (upscalers ~ blit bilinear do upstream)\n");
  {
    const char* names[2] = {"FSR 1", "SGSR 1"};
    for (int k = 0; k < 2; k++) {
      uint8_t* o;
      if (k == 0) {
        pass("PSEasu", &upA, 2, 0, 0, easu_a, easu_b);
        pass("PSRcas", &upB, 13, 0, 0, rcas_a, rcas_b);
        o = readback(&upB);
      } else {
        pass("PSSgsr1", &upA, 2, 0, 0, metrics, sgsr_b);
        o = readback(&upA);
      }
      double sum = 0;
      float mx = 0;
      for (int y = 2; y < OH - 2; y++)  // (bordas da imagem: clamp de cada filtro difere)
        for (int x = 2; x < OW - 2; x++)
          for (int c = 0; c < 3; c++) {
            float d = fabsf(o[(y * OW + x) * 4 + c] / 255.0f - bilinear_ref(in_grad, c, (float)x, (float)y));
            sum += d;
            if (d > mx) mx = d;
          }
      double mean = sum / ((OW - 4) * (OH - 4) * 3);
      char msg[128];
      snprintf(msg, sizeof msg, "%s: erro médio %.2f/255, máximo %.1f/255", names[k], mean * 255, mx * 255);
      check(mean * 255 < 1.5 && mx * 255 < 6, msg);
      free(o);
    }
  }

  // ---- 3. degrau vertical: sem deslocamento de meio pixel, sem overshoot, sem NaN ----
  printf("3. degrau (posição da borda, limites)\n");
  {
    const char* names[2] = {"FSR 1", "SGSR 1"};
    for (int k = 0; k < 2; k++) {
      uint8_t* o;
      if (k == 0) {
        pass("PSEasu", &upA, 3, 0, 0, easu_a, easu_b);
        pass("PSRcas", &upB, 13, 0, 0, rcas_a, rcas_b);
        o = readback(&upB);
      } else {
        pass("PSSgsr1", &upA, 3, 0, 0, metrics, sgsr_b);
        o = readback(&upA);
      }
      // Cruzamento de 50 % na linha do meio, interpolado: deve ficar em OW/2 (+- 1 px).
      int y = OH / 2;
      float cross = -1;
      for (int x = 0; x + 1 < OW; x++) {
        float a = o[(y * OW + x) * 4] / 255.0f, b = o[(y * OW + x + 1) * 4] / 255.0f;
        if (a < 0.5f && b >= 0.5f) { cross = x + 0.5f + (0.5f - a) / (b - a) - 0.5f; break; }
      }
      // Linhas iguais (a borda é vertical: nada pode variar em y).
      int row_diff = 0;
      for (int yy = 2; yy < OH - 2; yy++)
        for (int x = 0; x < OW; x++) row_diff = fmax(row_diff, abs(o[(yy * OW + x) * 4] - o[(y * OW + x) * 4]));
      // FSR 1: os 12 taps do EASU não formam um filtro separável, então numa borda vertical dura o
      // resultado varia um pouco com a fração vertical de cada linha (inerente ao EASU; ver seção 5,
      // que compara com o shader do BOTW). SGSR 1 tem de sair idêntico.
      int rows_equal = row_diff <= (k == 0 ? 6 : 1);
      char msg[128];
      snprintf(msg, sizeof msg, "%s: borda em x=%.2f (bilinear: %.2f), maior diferença entre linhas %d/255", names[k], cross, OW / 2.0 - 0.5, row_diff);
      check(fabsf(cross - (OW / 2.0f - 0.5f)) < 0.25f && rows_equal, msg);
      free(o);
    }
  }

  // ---- 4. borda serrilhada: o AA atua só perto da borda e não sai do intervalo ----
  printf("4. borda serrilhada (AA)\n");
  for (int k = 0; k < 2; k++) {
    if (k) {
      pass("PSSmaaEdge", &edges, 4, 0, 0, metrics, zero);
      pass("PSSmaaWeight", &weights, 11, 5, 6, metrics, zero);
      pass("PSSmaaBlend", &aa, 4, 12, 0, metrics, zero);
    } else {
      pass("PSFxaa", &aa, 4, 0, 0, metrics, zero);
    }
    uint8_t* o = readback(&aa);
    int changed = 0, far_changed = 0, partial = 0;
    for (int y = 0; y < H; y++)
      for (int x = 0; x < W; x++) {
        int i = (y * W + x) * 4, d = abs(o[i] - in_diag[i]);
        // distância até a borda (x*3 = y*4 + 20), em pixels
        float dist = fabsf(x * 3.0f - (y * 4.0f + 20)) / 5.0f;
        if (d > 2) { changed++; if (dist > 3) far_changed++; }
        if (o[i] > 20 && o[i] < 235) partial++;
      }
    char msg[160];
    snprintf(msg, sizeof msg, "%s: %d pixels suavizados (%d com tom intermediário), %d longe da borda", k ? "SMAA" : "FXAA", changed, partial, far_changed);
    check(changed > 10 && partial > 10 && far_changed == 0, msg);
    free(o);
  }

  // ---- 5. port fiel: mesmo resultado que os shaders GLSL do BOTWRecompAndroid ----
  if (argc > 2) {
    printf("5. comparação com os shaders do BOTWRecompAndroid (%s)\n", argv[2]);
    botw_init();
    Img* inputs[4] = {&flat, &grad, &step, &diag};
    const char* in_names[4] = {"lisa", "gradiente", "degrau", "serrilhada"};
    Img ref_small = image(W, H, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 20), ref_big = image(OW, OH, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 21),
        ref_big2 = image(OW, OH, VK_FORMAT_R8G8B8A8_UNORM, 4, NULL, 22);
    uint32_t slots[4] = {1, 2, 3, 4};
    for (int k = 0; k < 4; k++) {
      int d_fxaa = 0, d_fsr = 0, d_sgsr = 0;
      pass("PSFxaa", &aa, slots[k], 0, 0, metrics, zero);
      botw_pass(argv[2], "fxaa.frag", &ref_small, inputs[k], metrics, zero);
      uint8_t *o = readback(&aa), *r = readback(&ref_small);
      for (int i = 0; i < W * H; i++) for (int c = 0; c < 3; c++) d_fxaa = fmax(d_fxaa, abs(o[i * 4 + c] - r[i * 4 + c]));
      free(o); free(r);
      pass("PSEasu", &upA, slots[k], 0, 0, easu_a, easu_b);
      pass("PSRcas", &upB, 13, 0, 0, rcas_a, rcas_b);
      botw_pass(argv[2], "easu.frag", &ref_big, inputs[k], easu_a, easu_b);
      botw_pass(argv[2], "rcas.frag", &ref_big2, &ref_big, rcas_a, rcas_b);
      o = readback(&upB); r = readback(&ref_big2);
      for (int i = 0; i < OW * OH; i++) for (int c = 0; c < 3; c++) d_fsr = fmax(d_fsr, abs(o[i * 4 + c] - r[i * 4 + c]));
      free(o); free(r);
      pass("PSSgsr1", &upA, slots[k], 0, 0, metrics, sgsr_b);
      botw_pass(argv[2], "sgsr1.frag", &ref_big, inputs[k], metrics, sgsr_b);
      o = readback(&upA); r = readback(&ref_big);
      for (int i = 0; i < OW * OH; i++) for (int c = 0; c < 3; c++) d_sgsr = fmax(d_sgsr, abs(o[i * 4 + c] - r[i * 4 + c]));
      free(o); free(r);
      char msg[160];
      snprintf(msg, sizeof msg, "imagem %s: diferença máxima FXAA %d, FSR 1 %d, SGSR 1 %d (/255)", in_names[k], d_fxaa, d_fsr, d_sgsr);
      check(d_fxaa <= 1 && d_fsr <= 1 && d_sgsr <= 1, msg);
    }
  }

  printf(failures ? "\nRESULTADO: %d falha(s)\n" : "\nRESULTADO: todos os testes passaram\n", failures);
  return failures ? 1 : 0;
}
