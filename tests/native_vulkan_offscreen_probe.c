/* Execute the production SDL_GPU/Vulkan shim and pinned compute renderers on
 * the PC's real Vulkan GPU. Only title/VideoOut initialization and the texture
 * replacement registry are substituted. No PS5 WSI success is simulated. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../native/ps5/sdl_gpu_vulkan.c"
#include "gpu_raster3d.h"
#include "gfx_render.h"
#include "runtime/display/frame_package.h"
#include "runtime/mods/textures.h"
#include "gfx/onescreen.h"

static PFN_vkGetInstanceProcAddr host_loader;
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance i, const char *name)
{
    return host_loader(i, name);
}
int khdays_textures_replacements(void) { return 0; }
const KhdaysTextureReplacement *khdays_textures_replacement(int id) { (void)id; return NULL; }
uint32_t khdays_textures_version(int id) { (void)id; return 0; }
void khdays_textures_read_begin(int id) { (void)id; }
void khdays_textures_read_end(int id) { (void)id; }

extern SDL_GPUTexture *khdays_ps5_gfx_screen_raw(int engine, int *w, int *h);
extern SDL_GPUTexture *khdays_ps5_gfx_picture_raw(int *w, int *h);

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "probe line %d: %s; %s\n", \
    __LINE__, #test, SDL_GetError()); exit(1); } } while (0)
#define VK_CHECK(call) do { VkResult r_ = (call); if (r_ != VK_SUCCESS) { \
    fprintf(stderr, "probe line %d: %s returned %d\n", __LINE__, #call, r_); exit(1); } } while (0)

static SDL_GPUDevice *open_offscreen(void)
{
    HMODULE loader = LoadLibraryA("vulkan-1.dll");
    CHECK(loader != NULL);
    host_loader = (PFN_vkGetInstanceProcAddr)(void *)GetProcAddress(loader, "vkGetInstanceProcAddr");
    CHECK(host_loader != NULL);
    volkInitializeCustom(host_loader);
    SDL_GPUDevice *d = calloc(1, sizeof(*d));
    CHECK(d != NULL);
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "KH Days actual GPU offscreen probe";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&ci, NULL, &d->instance));
    volkLoadInstanceOnly(d->instance);
    uint32_t count = 1;
    VK_CHECK(vkEnumeratePhysicalDevices(d->instance, &count, &d->physical));
    vkGetPhysicalDeviceProperties(d->physical, &d->properties);
    vkGetPhysicalDeviceMemoryProperties(d->physical, &d->memory);
    printf("REAL GPU: %s, Vulkan %u.%u.%u\n", d->properties.deviceName,
           VK_VERSION_MAJOR(d->properties.apiVersion), VK_VERSION_MINOR(d->properties.apiVersion),
           VK_VERSION_PATCH(d->properties.apiVersion));
    CHECK(d->properties.apiVersion >= VK_API_VERSION_1_1);
    vkGetPhysicalDeviceQueueFamilyProperties(d->physical, &count, NULL);
    VkQueueFamilyProperties *families = calloc(count, sizeof(*families));
    CHECK(families != NULL);
    vkGetPhysicalDeviceQueueFamilyProperties(d->physical, &count, families);
    d->family = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { d->family = i; break; }
    free(families);
    CHECK(d->family != UINT32_MAX);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = d->family;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    VkDeviceCreateInfo di = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    VK_CHECK(vkCreateDevice(d->physical, &di, NULL, &d->device));
    volkLoadDevice(d->device);
    vkGetDeviceQueue(d->device, d->family, 0, &d->queue);
    VkCommandPoolCreateInfo pi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = d->family;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(d->device, &pi, NULL, &d->command_pool));
    VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = d->command_pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(d->device, &ai, &d->command));
    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(d->device, &fi, NULL, &d->fence));
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2048},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
    };
    VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = KHDAYS_MAX_DESCRIPTOR_SETS;
    dpi.poolSizeCount = SDL_arraysize(sizes);
    dpi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(d->device, &dpi, NULL, &d->descriptor_pool));
    d->uniform_alignment = d->properties.limits.minUniformBufferOffsetAlignment;
    if (!d->uniform_alignment) d->uniform_alignment = 1;
    CHECK(make_buffer(d, KHDAYS_UNIFORM_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &d->uniform_buffer, &d->uniform_memory, &d->uniform_coherent));
    VK_CHECK(vkMapMemory(d->device, d->uniform_memory, 0, VK_WHOLE_SIZE, 0, (void **)&d->uniform_map));
    return d;
}

static void check_pixels(SDL_GPUDevice *d, SDL_GPUTexture *texture, uint32_t expected, const char *label)
{
    CHECK(texture != NULL && texture->format == VK_FORMAT_R8G8B8A8_UNORM);
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    int coherent = 0;
    size_t pixels = (size_t)texture->width * texture->height;
    CHECK(make_buffer(d, pixels * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      &buffer, &memory, &coherent));
    SDL_GPUCommandBuffer *command = SDL_AcquireGPUCommandBuffer(d);
    CHECK(command != NULL);
    image_transition(command, texture, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {0};
    copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = (VkExtent3D){texture->width, texture->height, 1};
    vkCmdCopyImageToBuffer(d->command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    image_transition(command, texture, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    CHECK(SDL_SubmitGPUCommandBuffer(command));
    uint32_t *mapped = NULL;
    VK_CHECK(vkMapMemory(d->device, memory, 0, VK_WHOLE_SIZE, 0, (void **)&mapped));
    if (!coherent) {
        VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = memory;
        range.size = VK_WHOLE_SIZE;
        VK_CHECK(vkInvalidateMappedMemoryRanges(d->device, 1, &range));
    }
    for (size_t i = 0; i < pixels; ++i) {
        if (mapped[i] != expected) {
            fprintf(stderr, "%s: pixel %zu got %08x expected %08x\n", label, i, mapped[i], expected);
            exit(1);
        }
    }
    printf("PIXELS PASS: %s (%ux%u, %zu pixels == %08x)\n", label,
           texture->width, texture->height, pixels, expected);
    vkUnmapMemory(d->device, memory);
    vkDestroyBuffer(d->device, buffer, NULL);
    vkFreeMemory(d->device, memory, NULL);
}

int main(void)
{
    SDL_GPUDevice *d = open_offscreen();
    CHECK(khdays_raster3d_open(d));
    CHECK(khdays_gfx_render_open(d));
    puts("PIPELINES PASS: all six pinned SPIR-V compute pipelines created on real GPU");
    KhdaysFramePackage *p = calloc(1, sizeof(*p));
    CHECK(p != NULL);
    p->wants = 0x2a; /* exact payload flags recorded by console build 017 */
    p->info[0].is3d = 1;
    p->desc2d[0][KD_FLAGS] = 1 | KDF_BG0_IS_3D | (1u << KDF_BG_ON_SHIFT);
    p->desc2d[0][KD_LAYERS + KDL_KIND] = KDK_3D;
    p->desc2d[1][KD_FLAGS] = 1 | KDF_ENGINE_B;
    p->desc2d[0][KD_BACKDROP] = 0x001f; /* red */
    p->desc2d[1][KD_BACKDROP] = 0x03e0; /* green */
    CHECK(khdays_gfx_render_draw(p, 1, 0));
    int width, height;
    check_pixels(d, khdays_ps5_gfx_screen_raw(0, &width, &height), 0xff0000ff, "initial 2D-only screen A");
    check_pixels(d, khdays_ps5_gfx_screen_raw(1, &width, &height), 0xff00ff00, "initial 2D-only screen B");

    p->wants |= KHDAYS_WANT_3D;
    p->list3d.widen = 1.0f;
    const uint32_t clear = 0x001f7c00, depth = 0x7fff;
    memcpy(p->io3d + 0x50, &clear, sizeof(clear));
    memcpy(p->io3d + 0x54, &depth, sizeof(depth));
    CHECK(khdays_raster3d_rasterize(p, 2, 0));
    CHECK(khdays_gfx_render_draw(p, 2, 1));
    check_pixels(d, khdays_ps5_gfx_screen_raw(0, &width, &height), 0xffff0000, "3D clear composed at 2x");

    KhdaysG3dVertex vertices[4] = {
        {.x=0,.y=0,.z=0,.w=4096,.r=63},
        {.x=256,.y=0,.z=0,.w=4096,.r=63},
        {.x=256,.y=192,.z=0,.w=4096,.r=63},
        {.x=0,.y=192,.z=0,.w=4096,.r=63},
    };
    KhdaysG3dPolygon polygon = {.attr=(31u<<16)|(1u<<24), .first=0, .count=4,
                               .ymin=0, .ymax=192, .replacement=-1};
    p->list3d.npolys = 1;
    p->list3d.nverts = 4;
    p->list3d.polys = &polygon;
    p->list3d.verts = vertices;
    CHECK(khdays_raster3d_rasterize(p, 2, 0));
    CHECK(khdays_gfx_render_draw(p, 2, 1));
    check_pixels(d, khdays_ps5_gfx_screen_raw(0, &width, &height), 0xff0000ff, "opaque 3D geometry at 2x");

    polygon.tex = 7u << 26; /* native 8x8 direct-colour DS texture, VRAM offset 0 */
    p->disp3dcnt = 1;
    for (unsigned i = 0; i < 4; ++i) vertices[i].r = vertices[i].g = vertices[i].b = 63;
    for (unsigned i = 0; i < 64; ++i) {
        const uint16_t texel = 0x83e0; /* opaque green BGR555 */
        memcpy(p->tex + i * 2u, &texel, 2);
    }
    ++p->tex_version[0];
    CHECK(khdays_raster3d_rasterize(p, 2, 0));
    CHECK(khdays_gfx_render_draw(p, 2, 1));
    check_pixels(d, khdays_ps5_gfx_screen_raw(0, &width, &height), 0xff00ff00, "native textured 3D geometry at 2x");

    const uint32_t backdrop_a[] = {0xff563412,0xff563412,0xff563412,0xff563412};
    const uint32_t backdrop_b[] = {0xff987654,0xff987654,0xff987654,0xff987654,
                                0xff987654,0xff987654,0xff987654,0xff987654};
    KhdaysOneScreenDraw one = {0};
    one.frame = KOS_UPPER;
    one.rule = KOB_UPPER;
    one.columns = 256;
    one.backdrop = (const uint8_t *)backdrop_a;
    one.backdrop_w = one.backdrop_h = 2;
    one.backdrop_ax = one.backdrop_ay = 0.5f;
    p->wants = KHDAYS_WANT_2D;
    CHECK(khdays_gfx_render_draw_one(p, 1, 0, &one));
    check_pixels(d, khdays_ps5_gfx_picture_raw(&width, &height), 0xff563412, "one-screen backdrop upload/release");
    one.backdrop = (const uint8_t *)backdrop_b;
    one.backdrop_w = 4;
    CHECK(khdays_gfx_render_draw_one(p, 2, 0, &one));
    check_pixels(d, khdays_ps5_gfx_picture_raw(&width, &height), 0xff987654, "one-screen backdrop replacement/resize");
    khdays_gfx_render_set_shown(1.0f);
    CHECK(khdays_gfx_render_draw_one(p, 2, 0, &one));
    check_pixels(d, khdays_ps5_gfx_picture_raw(&width, &height), 0xff987654, "one-screen shrink compute");
    khdays_gfx_render_close();
    khdays_raster3d_close();
    khdays_vk_device_close(d);
    free(p);
    puts("PASS: production backend/renderers executed and GPU pixels read back; PS5 WSI not exercised");
    return 0;
}
