/* Minimal SDL_GPU surface implemented on the PS5 RADV Vulkan device.
 *
 * It provides the compute, storage-image and transfer subset used by the
 * pinned khdays-port renderers. The same Vulkan device owns the KHR_display
 * swapchain, so completed game images go straight to VideoOut without a CPU
 * framebuffer readback. */
#define VK_NO_PROTOTYPES 1
#include <volk.h>
#include "SDL3/SDL.h"
#include "sdl_gpu_vulkan.h"
#include "vulkan_dispatch_required.h"

#include <stdarg.h>

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance, const char *);

#define KHDAYS_MAX_SWAP_IMAGES 5u
#define KHDAYS_MAX_DESCRIPTOR_SETS 96u
#define KHDAYS_UNIFORM_BYTES 65536u
#define KHDAYS_MAX_TOUCHED_TEXTURES 64u

typedef struct RetiredResource {
    struct RetiredResource *next;
    unsigned kind;
} RetiredResource;

enum { RETIRED_BUFFER = 1, RETIRED_TRANSFER, RETIRED_TEXTURE, RETIRED_PIPELINE };

struct SDL_GPUBuffer {
    RetiredResource retired;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
};

struct SDL_GPUTransferBuffer {
    RetiredResource retired;
    VkBuffer buffer;
    VkDeviceMemory memory;
    VkDeviceSize size;
    void *mapped;
    int coherent;
    int writes;
};

struct SDL_GPUTexture {
    RetiredResource retired;
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkFormat format;
    VkImageLayout layout;
    uint32_t width, height;
};

struct SDL_GPUComputePipeline {
    RetiredResource retired;
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout sets[3];
    uint32_t buffer_count, image_count, uniform_count;
};

struct SDL_GPUCommandBuffer {
    struct SDL_GPUDevice *device;
    int open;
    int failed;
    char failure[512];
    VkDescriptorSet sets[3];
    SDL_GPUComputePipeline *pipeline;
    SDL_GPUBuffer *buffers[32];
    SDL_GPUTexture *images[16];
    uint32_t buffer_count, image_count;
    uint32_t uniform_offset, uniform_size;
    SDL_GPUTexture *touched[KHDAYS_MAX_TOUCHED_TEXTURES];
    VkImageLayout old_layout[KHDAYS_MAX_TOUCHED_TEXTURES];
    uint32_t touched_count;
};

struct SDL_GPUDevice {
    VkInstance instance;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family;
    VkPhysicalDeviceMemoryProperties memory;
    VkPhysicalDeviceProperties properties;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkFormat swap_format;
    VkExtent2D extent;
    VkImage swap_images[KHDAYS_MAX_SWAP_IMAGES];
    VkSemaphore acquire[KHDAYS_MAX_SWAP_IMAGES];
    VkSemaphore rendered[KHDAYS_MAX_SWAP_IMAGES];
    uint32_t swap_count, frame_slot;
    VkCommandPool command_pool;
    VkCommandBuffer command;
    VkFence fence;
    VkDescriptorPool descriptor_pool;
    VkBuffer uniform_buffer;
    VkDeviceMemory uniform_memory;
    uint8_t *uniform_map;
    VkDeviceSize uniform_alignment;
    int uniform_coherent;
    int pending;
    uint32_t command_serial, present_serial;
    RetiredResource *retired;
    SDL_GPUCommandBuffer command_api;
    void (*log)(const char *);
};

static char last_error[512];

static bool command_error(SDL_GPUCommandBuffer *command, const char *format, ...)
{
    if (command != NULL && command->open && command->failed)
        return false;
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    if (command != NULL && command->open) {
        command->failed = 1;
        memcpy(command->failure, last_error, sizeof(command->failure));
    }
    return false;
}

static void latch_current_error(SDL_GPUCommandBuffer *command)
{
    if (command != NULL && command->open && !command->failed) {
        command->failed = 1;
        memcpy(command->failure, last_error, sizeof(command->failure));
    }
}

static PFN_vkVoidFunction VKAPI_CALL icd_proc(VkInstance instance, const char *name)
{
    return vk_icdGetInstanceProcAddr(instance, name);
}

bool SDL_SetError(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(last_error, sizeof(last_error), format, args);
    va_end(args);
    return false;
}

const char *SDL_GetError(void) { return last_error; }
const char *khdays_vk_error(void) { return last_error; }

size_t SDL_strlcpy(char *destination, const char *source, size_t size)
{
    const size_t length = strlen(source);
    if (size != 0) {
        const size_t count = length < size - 1u ? length : size - 1u;
        memcpy(destination, source, count);
        destination[count] = '\0';
    }
    return length;
}

static void report(SDL_GPUDevice *device, const char *format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    if (device != NULL && device->log != NULL) device->log(line);
}

static int has_instance_extension(const char *wanted)
{
    uint32_t count = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, NULL) != VK_SUCCESS || count == 0) return 0;
    VkExtensionProperties *properties = calloc(count, sizeof(*properties));
    if (properties == NULL) return 0;
    int found = 0;
    if (vkEnumerateInstanceExtensionProperties(NULL, &count, properties) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) found |= strcmp(properties[i].extensionName, wanted) == 0;
    }
    free(properties);
    return found;
}

static int has_device_extension(VkPhysicalDevice physical, const char *wanted)
{
    uint32_t count = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, NULL) != VK_SUCCESS || count == 0) return 0;
    VkExtensionProperties *properties = calloc(count, sizeof(*properties));
    if (properties == NULL) return 0;
    int found = 0;
    if (vkEnumerateDeviceExtensionProperties(physical, NULL, &count, properties) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) found |= strcmp(properties[i].extensionName, wanted) == 0;
    }
    free(properties);
    return found;
}

static uint32_t memory_type(SDL_GPUDevice *device, uint32_t bits,
                            VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                            int *coherent)
{
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < device->memory.memoryTypeCount; ++i) {
        const VkMemoryPropertyFlags flags = device->memory.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) == 0 || (flags & required) != required) continue;
        if (fallback == UINT32_MAX) fallback = i;
        if ((flags & preferred) == preferred) {
            if (coherent != NULL) *coherent = (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
            return i;
        }
    }
    if (fallback != UINT32_MAX && coherent != NULL) {
        *coherent = (device->memory.memoryTypes[fallback].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    }
    return fallback;
}

static int make_buffer(SDL_GPUDevice *device, VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred,
                       VkBuffer *buffer, VkDeviceMemory *memory, int *coherent)
{
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size ? size : 4;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkResult result = vkCreateBuffer(device->device, &info, NULL, buffer);
    if (result != VK_SUCCESS) return SDL_SetError("vkCreateBuffer failed (%d)", result);
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device->device, *buffer, &requirements);
    const uint32_t type = memory_type(device, requirements.memoryTypeBits, required, preferred, coherent);
    if (type == UINT32_MAX) {
        vkDestroyBuffer(device->device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
        return SDL_SetError("no compatible Vulkan buffer memory type");
    }
    VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = vkAllocateMemory(device->device, &allocation, NULL, memory);
    if (result == VK_SUCCESS) result = vkBindBufferMemory(device->device, *buffer, *memory, 0);
    if (result != VK_SUCCESS) {
        if (*memory != VK_NULL_HANDLE) vkFreeMemory(device->device, *memory, NULL);
        vkDestroyBuffer(device->device, *buffer, NULL);
        *memory = VK_NULL_HANDLE;
        *buffer = VK_NULL_HANDLE;
        return SDL_SetError("Vulkan buffer memory setup failed (%d)", result);
    }
    return 1;
}

static int make_image(SDL_GPUDevice *device, uint32_t width, uint32_t height, VkFormat format,
                      VkImage *image, VkDeviceMemory *memory)
{
    VkImageCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = (VkExtent3D){width, height, 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult result = vkCreateImage(device->device, &info, NULL, image);
    if (result != VK_SUCCESS) return SDL_SetError("vkCreateImage failed (%d)", result);
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device->device, *image, &requirements);
    const uint32_t type = memory_type(device, requirements.memoryTypeBits, 0,
                                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, NULL);
    if (type == UINT32_MAX) {
        vkDestroyImage(device->device, *image, NULL);
        *image = VK_NULL_HANDLE;
        return SDL_SetError("no compatible Vulkan image memory type");
    }
    VkMemoryAllocateInfo allocation = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    result = vkAllocateMemory(device->device, &allocation, NULL, memory);
    if (result == VK_SUCCESS) result = vkBindImageMemory(device->device, *image, *memory, 0);
    if (result != VK_SUCCESS) {
        if (*memory != VK_NULL_HANDLE) vkFreeMemory(device->device, *memory, NULL);
        vkDestroyImage(device->device, *image, NULL);
        *memory = VK_NULL_HANDLE;
        *image = VK_NULL_HANDLE;
        return SDL_SetError("Vulkan image memory setup failed (%d)", result);
    }
    return 1;
}

static void collect_retired(SDL_GPUDevice *device)
{
    while (device->retired != NULL) {
        RetiredResource *resource = device->retired;
        device->retired = resource->next;
        switch (resource->kind) {
        case RETIRED_BUFFER: {
            SDL_GPUBuffer *buffer = (SDL_GPUBuffer *)resource;
            vkDestroyBuffer(device->device, buffer->buffer, NULL);
            vkFreeMemory(device->device, buffer->memory, NULL);
            break;
        }
        case RETIRED_TRANSFER: {
            SDL_GPUTransferBuffer *buffer = (SDL_GPUTransferBuffer *)resource;
            if (buffer->mapped != NULL) vkUnmapMemory(device->device, buffer->memory);
            vkDestroyBuffer(device->device, buffer->buffer, NULL);
            vkFreeMemory(device->device, buffer->memory, NULL);
            break;
        }
        case RETIRED_TEXTURE: {
            SDL_GPUTexture *texture = (SDL_GPUTexture *)resource;
            vkDestroyImageView(device->device, texture->view, NULL);
            vkDestroyImage(device->device, texture->image, NULL);
            vkFreeMemory(device->device, texture->memory, NULL);
            break;
        }
        case RETIRED_PIPELINE: {
            SDL_GPUComputePipeline *pipeline = (SDL_GPUComputePipeline *)resource;
            vkDestroyPipeline(device->device, pipeline->pipeline, NULL);
            vkDestroyPipelineLayout(device->device, pipeline->layout, NULL);
            for (uint32_t i = 0; i < 3; ++i)
                vkDestroyDescriptorSetLayout(device->device, pipeline->sets[i], NULL);
            break;
        }
        }
        free(resource);
    }
}

static int wait_pending(SDL_GPUDevice *device)
{
    if (device->pending) {
        const VkResult result = vkWaitForFences(device->device, 1, &device->fence, VK_TRUE, UINT64_MAX);
        if (result != VK_SUCCESS) return SDL_SetError("Vulkan frame fence failed (%d)", result);
        device->pending = 0;
    }
    if (!device->command_api.open) collect_retired(device);
    return 1;
}

static void retire_resource(SDL_GPUDevice *device, RetiredResource *resource, unsigned kind)
{
    if (resource->kind != 0) return;
    resource->kind = kind;
    resource->next = device->retired;
    device->retired = resource;
    /* SDL release is deferred: a recorded copy/dispatch still owns its
     * resources, even though the application has released its wrapper. */
    if (!device->command_api.open) (void)wait_pending(device);
}

static int begin_command(SDL_GPUDevice *device)
{
    if (!wait_pending(device)) return 0;
    if (device->command_api.open) return SDL_SetError("Vulkan command buffer already open");
    VkResult result = vkResetDescriptorPool(device->device, device->descriptor_pool, 0);
    if (result == VK_SUCCESS) result = vkResetCommandBuffer(device->command, 0);
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (result == VK_SUCCESS) result = vkBeginCommandBuffer(device->command, &begin);
    if (result != VK_SUCCESS) return SDL_SetError("Vulkan command begin failed (%d)", result);
    memset(&device->command_api, 0, sizeof(device->command_api));
    device->command_api.device = device;
    device->command_api.open = 1;
    ++device->command_serial;
    if (device->command_serial <= 8)
        report(device, "GPU command=%u begin", device->command_serial);
    return 1;
}

static void remember_texture(SDL_GPUCommandBuffer *command, SDL_GPUTexture *texture)
{
    if (texture == NULL) return;
    for (uint32_t i = 0; i < command->touched_count; ++i) {
        if (command->touched[i] == texture) return;
    }
    if (command->touched_count < KHDAYS_MAX_TOUCHED_TEXTURES) {
        const uint32_t i = command->touched_count++;
        command->touched[i] = texture;
        command->old_layout[i] = texture->layout;
    }
}

static void image_transition(SDL_GPUCommandBuffer *command, SDL_GPUTexture *texture,
                             VkImageLayout next, VkAccessFlags src_access,
                             VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                             VkPipelineStageFlags dst_stage)
{
    if (texture == NULL) return;
    remember_texture(command, texture);
    if (texture->layout == next && src_access == 0 && dst_access == 0) return;
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = texture->layout;
    barrier.newLayout = next;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture->image;
    barrier.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (texture->layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    }
    vkCmdPipelineBarrier(command->device->command, src_stage, dst_stage, 0,
                         0, NULL, 0, NULL, 1, &barrier);
    texture->layout = next;
}

static int flush_memory(SDL_GPUDevice *device, VkDeviceMemory memory, int coherent,
                        VkDeviceSize offset, VkDeviceSize size)
{
    if (coherent) return 1;
    VkMappedMemoryRange range = {VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    range.memory = memory;
    range.offset = offset;
    range.size = size == VK_WHOLE_SIZE ? VK_WHOLE_SIZE : size;
    const VkResult result = vkFlushMappedMemoryRanges(device->device, 1, &range);
    return result == VK_SUCCESS || SDL_SetError("Vulkan memory flush failed (%d)", result);
}

static void restore_cancelled_textures(SDL_GPUCommandBuffer *command)
{
    for (uint32_t i = 0; i < command->touched_count; ++i) {
        command->touched[i]->layout = command->old_layout[i];
    }
}

static void clear_touched(SDL_GPUCommandBuffer *command)
{
    command->touched_count = 0;
}

static int reject_command(SDL_GPUCommandBuffer *command)
{
    char failure[sizeof(last_error)];
    memcpy(failure, command->failed ? command->failure : last_error, sizeof(failure));
    report(command->device, "GPU command=%u rejected: %s", command->device->command_serial, failure);
    (void)SDL_CancelGPUCommandBuffer(command);
    return SDL_SetError("%s", failure);
}

static int submit_command(SDL_GPUCommandBuffer *command, int wait)
{
    if (command == NULL) return SDL_SetError("invalid Vulkan command buffer");
    SDL_GPUDevice *device = command->device;
    if (command != &device->command_api || !command->open) return SDL_SetError("invalid Vulkan command buffer");
    if (command->failed) return reject_command(command);
    if (!flush_memory(device, device->uniform_memory, device->uniform_coherent, 0, VK_WHOLE_SIZE))
        return reject_command(command);
    VkResult result = vkEndCommandBuffer(device->command);
    if (result != VK_SUCCESS) {
        command_error(command, "vkEndCommandBuffer failed (%d)", result);
        return reject_command(command);
    }
    if (!wait_pending(device)) return reject_command(command);
    result = vkResetFences(device->device, 1, &device->fence);
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &device->command;
    if (device->command_serial <= 8)
        report(device, "GPU command=%u queue submit begin", device->command_serial);
    if (result == VK_SUCCESS) result = vkQueueSubmit(device->queue, 1, &submit, device->fence);
    if (result != VK_SUCCESS) {
        command_error(command, "Vulkan queue submit failed (%d)", result);
        return reject_command(command);
    }
    command->open = 0;
    device->pending = 1;
    clear_touched(command);
    const int completed = !wait || wait_pending(device);
    if (device->command_serial <= 8)
        report(device, "GPU command=%u submit %s", device->command_serial, completed ? "complete" : "failed");
    return completed;
}

static int make_display_surface(SDL_GPUDevice *device, unsigned wanted_width, unsigned wanted_height)
{
    uint32_t count = 0;
    VkResult result = vkGetPhysicalDeviceDisplayPropertiesKHR(device->physical, &count, NULL);
    if (result != VK_SUCCESS || count == 0) return SDL_SetError("Vulkan exposes no VideoOut display (%d)", result);
    VkDisplayPropertiesKHR *displays = calloc(count, sizeof(*displays));
    if (displays == NULL) return SDL_SetError("no memory for Vulkan display list");
    result = vkGetPhysicalDeviceDisplayPropertiesKHR(device->physical, &count, displays);
    if (result != VK_SUCCESS || count == 0) {
        free(displays);
        return SDL_SetError("Vulkan display enumeration failed (%d)", result);
    }
    const VkDisplayKHR display = displays[0].display;
    const VkExtent2D native_extent = displays[0].physicalResolution;
    free(displays);

    count = 0;
    result = vkGetDisplayModePropertiesKHR(device->physical, display, &count, NULL);
    if (result != VK_SUCCESS || count == 0) return SDL_SetError("VideoOut has no Vulkan display mode (%d)", result);
    VkDisplayModePropertiesKHR *modes = calloc(count, sizeof(*modes));
    if (modes == NULL) return SDL_SetError("no memory for VideoOut modes");
    result = vkGetDisplayModePropertiesKHR(device->physical, display, &count, modes);
    if (result != VK_SUCCESS || count == 0) {
        free(modes);
        return SDL_SetError("VideoOut mode enumeration failed (%d)", result);
    }
    uint32_t selected = 0;
    uint64_t selected_distance = UINT64_MAX;
    for (uint32_t i = 0; i < count; ++i) {
        const VkExtent2D candidate = modes[i].parameters.visibleRegion;
        const uint64_t dx = candidate.width > wanted_width ? candidate.width - wanted_width : wanted_width - candidate.width;
        const uint64_t dy = candidate.height > wanted_height ? candidate.height - wanted_height : wanted_height - candidate.height;
        const uint64_t distance = dx * dx + dy * dy;
        if (distance < selected_distance) { selected = i; selected_distance = distance; }
        if (candidate.width == wanted_width && candidate.height == wanted_height) { selected = i; break; }
    }
    VkDisplayModeKHR mode = modes[selected].displayMode;
    VkExtent2D extent = modes[selected].parameters.visibleRegion;
    free(modes);
    VkDisplaySurfaceCreateInfoKHR surface_info = {VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
    surface_info.displayMode = mode;
    surface_info.planeIndex = 0;
    surface_info.planeStackIndex = 0;
    surface_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    surface_info.globalAlpha = 1.0f;
    surface_info.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
    surface_info.imageExtent = extent.width && extent.height ? extent : native_extent;
    result = vkCreateDisplayPlaneSurfaceKHR(device->instance, &surface_info, NULL, &device->surface);
    if (result != VK_SUCCESS) return SDL_SetError("Vulkan VideoOut surface creation failed (%d)", result);
    VkBool32 supported = VK_FALSE;
    result = vkGetPhysicalDeviceSurfaceSupportKHR(device->physical, device->family, device->surface, &supported);
    if (result != VK_SUCCESS || !supported) return SDL_SetError("Vulkan compute queue cannot present to VideoOut");
    VkSurfaceCapabilitiesKHR caps;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical, device->surface, &caps);
    if (result != VK_SUCCESS) return SDL_SetError("Vulkan VideoOut capability query failed (%d)", result);
    if ((caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) == 0)
        return SDL_SetError("VideoOut swapchain cannot receive Vulkan blits");
    device->extent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : surface_info.imageExtent;
    return device->extent.width != 0 && device->extent.height != 0 ? 1 :
        SDL_SetError("Vulkan reported an empty VideoOut extent");
}

static int make_swapchain(SDL_GPUDevice *device)
{
    uint32_t count = 0;
    VkResult result = vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical, device->surface, &count, NULL);
    if (result != VK_SUCCESS || count == 0) return SDL_SetError("VideoOut exposes no Vulkan surface formats");
    VkSurfaceFormatKHR *formats = calloc(count, sizeof(*formats));
    if (formats == NULL) return SDL_SetError("no memory for VideoOut formats");
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical, device->surface, &count, formats);
    if (result != VK_SUCCESS || count == 0) {
        free(formats);
        return SDL_SetError("VideoOut format enumeration failed (%d)", result);
    }
    VkSurfaceFormatKHR selected = formats[0];
    for (uint32_t i = 0; i < count; ++i) {
        if (formats[i].format == VK_FORMAT_B8G8R8A8_UNORM || formats[i].format == VK_FORMAT_R8G8B8A8_UNORM) {
            selected = formats[i];
            break;
        }
    }
    free(formats);
    VkSurfaceCapabilitiesKHR caps;
    result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical, device->surface, &caps);
    if (result != VK_SUCCESS) return SDL_SetError("VideoOut capability refresh failed (%d)", result);
    uint32_t image_count = caps.minImageCount + 1u;
    if (image_count < 3u) image_count = 3u;
    if (caps.maxImageCount && image_count > caps.maxImageCount) image_count = caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if ((caps.supportedCompositeAlpha & alpha) == 0) {
        for (uint32_t bit = 1; bit <= VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR; bit <<= 1) {
            if (caps.supportedCompositeAlpha & bit) { alpha = (VkCompositeAlphaFlagBitsKHR)bit; break; }
        }
    }
    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = device->surface;
    info.minImageCount = image_count;
    info.imageFormat = selected.format;
    info.imageColorSpace = selected.colorSpace;
    info.imageExtent = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : device->extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    result = vkCreateSwapchainKHR(device->device, &info, NULL, &device->swapchain);
    if (result != VK_SUCCESS) return SDL_SetError("VideoOut Vulkan swapchain creation failed (%d)", result);
    device->extent = info.imageExtent;
    device->swap_format = info.imageFormat;
    count = KHDAYS_MAX_SWAP_IMAGES;
    result = vkGetSwapchainImagesKHR(device->device, device->swapchain, &count, device->swap_images);
    if (result != VK_SUCCESS || count < 2 || count > KHDAYS_MAX_SWAP_IMAGES)
        return SDL_SetError("VideoOut returned an unsupported swapchain image count (%u, %d)", count, result);
    device->swap_count = count;
    VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < count; ++i) {
        if (vkCreateSemaphore(device->device, &semaphore, NULL, &device->rendered[i]) != VK_SUCCESS)
            return SDL_SetError("could not create VideoOut render semaphore");
    }
    for (uint32_t i = 0; i < KHDAYS_MAX_SWAP_IMAGES; ++i) {
        if (vkCreateSemaphore(device->device, &semaphore, NULL, &device->acquire[i]) != VK_SUCCESS)
            return SDL_SetError("could not create VideoOut acquire semaphore");
    }
    report(device, "Vulkan VideoOut ready: %ux%u, %u images, %s", device->extent.width,
           device->extent.height, count, device->properties.deviceName);
    return 1;
}

SDL_GPUDevice *khdays_vk_device_open(unsigned width, unsigned height, void (*log)(const char *))
{
    last_error[0] = '\0';
    SDL_GPUDevice *device = calloc(1, sizeof(*device));
    if (device == NULL) { SDL_SetError("no memory for Vulkan device"); return NULL; }
    device->log = log;
    report(device, "BOOT loader: resolving linked RADV global commands");
    volkInitializeCustom(icd_proc);
#define REQUIRE_COMMAND(name) do { \
    if (vk##name == NULL) { SDL_SetError("missing Vulkan entrypoint: vk" #name); goto failed; } \
} while (0);
    KHDAYS_VK_GLOBAL_COMMANDS(REQUIRE_COMMAND)
    report(device, "BOOT loader: global commands ready; querying KHR_display extensions");
    if (!has_instance_extension(VK_KHR_SURFACE_EXTENSION_NAME) ||
        !has_instance_extension(VK_KHR_DISPLAY_EXTENSION_NAME)) {
        SDL_SetError("PS5 RADV does not expose vkCreateInstance/KHR_display");
        goto failed;
    }
    const char *extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Kingdom Hearts 358/2 Days Native";
    app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    app.pEngineName = "khdays-port GPU renderer";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 1);
    /* The pinned compute shaders use SPIR-V 1.3 (Vulkan 1.1). */
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance_info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instance_info.pApplicationInfo = &app;
    instance_info.enabledExtensionCount = 2;
    instance_info.ppEnabledExtensionNames = extensions;
    report(device, "BOOT instance: vkCreateInstance begin");
    VkResult result = vkCreateInstance(&instance_info, NULL, &device->instance);
    if (result != VK_SUCCESS) { SDL_SetError("vkCreateInstance failed (%d)", result); goto failed; }
    report(device, "BOOT instance: created; resolving instance commands");
    /* Device commands are loaded only once a device exists. In particular,
     * validate GetDeviceProcAddr before volkLoadDevice calls through it. */
    volkLoadInstanceOnly(device->instance);
    KHDAYS_VK_INSTANCE_COMMANDS(REQUIRE_COMMAND)
    report(device, "BOOT instance: commands ready; enumerating physical devices");
    uint32_t physical_count = 0;
    result = vkEnumeratePhysicalDevices(device->instance, &physical_count, NULL);
    if (result != VK_SUCCESS || physical_count == 0) { SDL_SetError("RADV found no Vulkan GPU (%d)", result); goto failed; }
    VkPhysicalDevice *physicals = calloc(physical_count, sizeof(*physicals));
    if (physicals == NULL) { SDL_SetError("no memory for Vulkan devices"); goto failed; }
    result = vkEnumeratePhysicalDevices(device->instance, &physical_count, physicals);
    if (result != VK_SUCCESS || physical_count == 0) { free(physicals); SDL_SetError("Vulkan GPU enumeration failed"); goto failed; }
    device->physical = physicals[0];
    free(physicals);
    vkGetPhysicalDeviceProperties(device->physical, &device->properties);
    if (device->properties.apiVersion < VK_API_VERSION_1_1) {
        SDL_SetError("PS5 compute shaders require Vulkan 1.1 / SPIR-V 1.3");
        goto failed;
    }
    report(device, "BOOT physical device: %s; Vulkan %u.%u.%u; requesting 1.1 for SPIR-V 1.3",
           device->properties.deviceName, VK_VERSION_MAJOR(device->properties.apiVersion),
           VK_VERSION_MINOR(device->properties.apiVersion), VK_VERSION_PATCH(device->properties.apiVersion));
    vkGetPhysicalDeviceMemoryProperties(device->physical, &device->memory);
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical, &family_count, NULL);
    VkQueueFamilyProperties *families = calloc(family_count ? family_count : 1, sizeof(*families));
    if (families == NULL) { SDL_SetError("no memory for Vulkan queues"); goto failed; }
    vkGetPhysicalDeviceQueueFamilyProperties(device->physical, &family_count, families);
    device->family = UINT32_MAX;
    for (uint32_t i = 0; i < family_count; ++i) {
        if ((families[i].queueFlags & (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) ==
            (VK_QUEUE_COMPUTE_BIT | VK_QUEUE_GRAPHICS_BIT)) { device->family = i; break; }
    }
    free(families);
    if (device->family == UINT32_MAX) { SDL_SetError("RADV exposes no graphics/compute queue"); goto failed; }
    if (!has_device_extension(device->physical, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
        SDL_SetError("RADV does not expose VK_KHR_swapchain"); goto failed;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queue_info = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queue_info.queueFamilyIndex = device->family;
    queue_info.queueCount = 1;
    queue_info.pQueuePriorities = &priority;
    const char *device_extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo device_info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    device_info.queueCreateInfoCount = 1;
    device_info.pQueueCreateInfos = &queue_info;
    device_info.enabledExtensionCount = 1;
    device_info.ppEnabledExtensionNames = device_extensions;
    report(device, "BOOT device: vkCreateDevice begin; queue family=%u", device->family);
    result = vkCreateDevice(device->physical, &device_info, NULL, &device->device);
    if (result != VK_SUCCESS) { SDL_SetError("vkCreateDevice failed (%d)", result); goto failed; }
    report(device, "BOOT device: created; resolving device commands");
    volkLoadDevice(device->device);
    KHDAYS_VK_DEVICE_COMMANDS(REQUIRE_COMMAND)
#undef REQUIRE_COMMAND
    report(device, "BOOT device: all required commands ready; acquiring queue");
    vkGetDeviceQueue(device->device, device->family, 0, &device->queue);
    report(device, "BOOT display: querying VideoOut surface");
    if (!make_display_surface(device, width, height)) goto failed;
    report(device, "BOOT display: creating swapchain %ux%u", device->extent.width, device->extent.height);
    if (!make_swapchain(device)) goto failed;

    report(device, "BOOT commands: creating command/descriptor pools and uniform memory");
    VkCommandPoolCreateInfo pool = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = device->family;
    result = vkCreateCommandPool(device->device, &pool, NULL, &device->command_pool);
    VkCommandBufferAllocateInfo command = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    command.commandPool = device->command_pool;
    command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command.commandBufferCount = 1;
    if (result == VK_SUCCESS) result = vkAllocateCommandBuffers(device->device, &command, &device->command);
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (result == VK_SUCCESS) result = vkCreateFence(device->device, &fence, NULL, &device->fence);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan command queue setup failed (%d)", result); goto failed; }
    const VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2048},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2048},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024},
    };
    VkDescriptorPoolCreateInfo descriptors = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    descriptors.maxSets = KHDAYS_MAX_DESCRIPTOR_SETS;
    descriptors.poolSizeCount = 3;
    descriptors.pPoolSizes = pool_sizes;
    result = vkCreateDescriptorPool(device->device, &descriptors, NULL, &device->descriptor_pool);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan descriptor pool setup failed (%d)", result); goto failed; }
    VkPhysicalDeviceProperties physical_properties;
    vkGetPhysicalDeviceProperties(device->physical, &physical_properties);
    device->uniform_alignment = physical_properties.limits.minUniformBufferOffsetAlignment;
    if (device->uniform_alignment == 0) device->uniform_alignment = 1;
    if (!make_buffer(device, KHDAYS_UNIFORM_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &device->uniform_buffer, &device->uniform_memory, &device->uniform_coherent)) goto failed;
    result = vkMapMemory(device->device, device->uniform_memory, 0, VK_WHOLE_SIZE, 0,
                         (void **)&device->uniform_map);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan uniform ring map failed (%d)", result); goto failed; }
    report(device, "BOOT complete: Vulkan device and display initialized");
    return device;

failed:
    report(device, "Vulkan initialization failed: %s", SDL_GetError());
    khdays_vk_device_close(device);
    return NULL;
}

void khdays_vk_device_close(SDL_GPUDevice *device)
{
    if (device == NULL) return;
    if (device->device != VK_NULL_HANDLE) {
        if (vkDeviceWaitIdle != NULL) vkDeviceWaitIdle(device->device);
        if (device->command_api.open) {
            restore_cancelled_textures(&device->command_api);
            if (device->command && vkResetCommandBuffer != NULL)
                (void)vkResetCommandBuffer(device->command, 0);
            device->command_api.open = 0;
        }
        collect_retired(device);
        if (device->uniform_map != NULL) vkUnmapMemory(device->device, device->uniform_memory);
        for (uint32_t i = 0; i < KHDAYS_MAX_SWAP_IMAGES; ++i) {
            if (device->acquire[i]) vkDestroySemaphore(device->device, device->acquire[i], NULL);
            if (device->rendered[i]) vkDestroySemaphore(device->device, device->rendered[i], NULL);
        }
        if (device->swapchain) vkDestroySwapchainKHR(device->device, device->swapchain, NULL);
        if (device->descriptor_pool) vkDestroyDescriptorPool(device->device, device->descriptor_pool, NULL);
        if (device->uniform_buffer) vkDestroyBuffer(device->device, device->uniform_buffer, NULL);
        if (device->uniform_memory) vkFreeMemory(device->device, device->uniform_memory, NULL);
        if (device->fence) vkDestroyFence(device->device, device->fence, NULL);
        if (device->command_pool) vkDestroyCommandPool(device->device, device->command_pool, NULL);
        if (vkDestroyDevice != NULL) vkDestroyDevice(device->device, NULL);
    }
    if (device->surface && device->instance) vkDestroySurfaceKHR(device->instance, device->surface, NULL);
    if (device->instance && vkDestroyInstance != NULL) vkDestroyInstance(device->instance, NULL);
    free(device);
}

bool khdays_vk_wait_idle(SDL_GPUDevice *device)
{
    if (device == NULL || device->device == VK_NULL_HANDLE) return true;
    const VkResult result = vkDeviceWaitIdle(device->device);
    if (result != VK_SUCCESS) return SDL_SetError("Vulkan device idle wait failed (%d)", result);
    device->pending = 0;
    if (!device->command_api.open) collect_retired(device);
    return true;
}

unsigned khdays_vk_width(SDL_GPUDevice *device) { return device ? device->extent.width : 0; }
unsigned khdays_vk_height(SDL_GPUDevice *device) { return device ? device->extent.height : 0; }
SDL_GPUShaderFormat SDL_GetGPUShaderFormats(SDL_GPUDevice *device) { return device ? SDL_GPU_SHADERFORMAT_SPIRV : 0; }

static VkFormat vk_format(SDL_GPUTextureFormat format)
{
    switch (format) {
    case SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
    case SDL_GPU_TEXTUREFORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

bool SDL_GPUTextureSupportsFormat(SDL_GPUDevice *device, SDL_GPUTextureFormat format,
                                  Uint32 type, SDL_GPUTextureUsageFlags usage)
{
    (void)type;
    if (device == NULL) return false;
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(device->physical, vk_format(format), &properties);
    VkFormatFeatureFlags needed = 0;
    if (usage & (SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE |
                 SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE))
        needed |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    if (usage & SDL_GPU_TEXTUREUSAGE_SAMPLER) needed |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (properties.optimalTilingFeatures & needed) == needed;
}

SDL_GPUBuffer *SDL_CreateGPUBuffer(SDL_GPUDevice *device, const SDL_GPUBufferCreateInfo *info)
{
    if (device == NULL || info == NULL) { SDL_SetError("invalid Vulkan buffer request"); return NULL; }
    SDL_GPUBuffer *out = calloc(1, sizeof(*out));
    if (out == NULL) { SDL_SetError("no memory for Vulkan buffer wrapper"); return NULL; }
    if (!make_buffer(device, info->size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &out->buffer, &out->memory, NULL)) { free(out); return NULL; }
    out->size = info->size;
    return out;
}

void SDL_ReleaseGPUBuffer(SDL_GPUDevice *device, SDL_GPUBuffer *buffer)
{
    if (device == NULL || buffer == NULL) return;
    retire_resource(device, &buffer->retired, RETIRED_BUFFER);
}

SDL_GPUTransferBuffer *SDL_CreateGPUTransferBuffer(SDL_GPUDevice *device,
                                                    const SDL_GPUTransferBufferCreateInfo *info)
{
    if (device == NULL || info == NULL) { SDL_SetError("invalid Vulkan transfer buffer request"); return NULL; }
    SDL_GPUTransferBuffer *out = calloc(1, sizeof(*out));
    if (out == NULL) { SDL_SetError("no memory for Vulkan transfer buffer wrapper"); return NULL; }
    const int writes = (info->usage & SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD) != 0;
    const VkBufferUsageFlags usage = writes ? VK_BUFFER_USAGE_TRANSFER_SRC_BIT : VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (!make_buffer(device, info->size, usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &out->buffer, &out->memory, &out->coherent)) {
        free(out);
        return NULL;
    }
    out->size = info->size;
    out->writes = writes;
    return out;
}

void SDL_ReleaseGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *buffer)
{
    if (device == NULL || buffer == NULL) return;
    retire_resource(device, &buffer->retired, RETIRED_TRANSFER);
}

void *SDL_MapGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *buffer, bool cycle)
{
    (void)cycle;
    if (device == NULL || buffer == NULL || buffer->mapped != NULL) { SDL_SetError("invalid Vulkan transfer map"); return NULL; }
    const VkResult result = vkMapMemory(device->device, buffer->memory, 0, VK_WHOLE_SIZE, 0, &buffer->mapped);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan transfer map failed (%d)", result); return NULL; }
    return buffer->mapped;
}

void SDL_UnmapGPUTransferBuffer(SDL_GPUDevice *device, SDL_GPUTransferBuffer *buffer)
{
    if (device == NULL || buffer == NULL || buffer->mapped == NULL) return;
    if (buffer->writes && !flush_memory(device, buffer->memory, buffer->coherent, 0, VK_WHOLE_SIZE))
        latch_current_error(&device->command_api);
    vkUnmapMemory(device->device, buffer->memory);
    buffer->mapped = NULL;
}

SDL_GPUTexture *SDL_CreateGPUTexture(SDL_GPUDevice *device, const SDL_GPUTextureCreateInfo *info)
{
    if (device == NULL || info == NULL || info->width == 0 || info->height == 0 || info->num_levels > 1 ||
        info->sample_count != SDL_GPU_SAMPLECOUNT_1) { SDL_SetError("unsupported Vulkan texture request"); return NULL; }
    SDL_GPUTexture *out = calloc(1, sizeof(*out));
    if (out == NULL) { SDL_SetError("no memory for Vulkan texture wrapper"); return NULL; }
    out->format = vk_format(info->format);
    out->width = info->width;
    out->height = info->height;
    if (out->format == VK_FORMAT_UNDEFINED || !make_image(device, out->width, out->height, out->format,
                                                          &out->image, &out->memory)) { free(out); return NULL; }
    VkImageViewCreateInfo view = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = out->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = out->format;
    view.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    const VkResult result = vkCreateImageView(device->device, &view, NULL, &out->view);
    if (result != VK_SUCCESS) {
        vkDestroyImage(device->device, out->image, NULL);
        vkFreeMemory(device->device, out->memory, NULL);
        free(out);
        SDL_SetError("Vulkan image view creation failed (%d)", result);
        return NULL;
    }
    out->layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return out;
}

void SDL_ReleaseGPUTexture(SDL_GPUDevice *device, SDL_GPUTexture *texture)
{
    if (device == NULL || texture == NULL) return;
    retire_resource(device, &texture->retired, RETIRED_TEXTURE);
}

static VkDescriptorSetLayout make_set_layout(SDL_GPUDevice *device, uint32_t count,
                                             VkDescriptorType type)
{
    VkDescriptorSetLayoutBinding bindings[32];
    if (count > 32) { SDL_SetError("Vulkan compute layout exceeds 32 bindings"); return VK_NULL_HANDLE; }
    for (uint32_t i = 0; i < count; ++i) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i, .descriptorType = type, .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    VkDescriptorSetLayoutCreateInfo info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    info.bindingCount = count;
    info.pBindings = count ? bindings : NULL;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    const VkResult result = vkCreateDescriptorSetLayout(device->device, &info, NULL, &layout);
    if (result != VK_SUCCESS) SDL_SetError("Vulkan descriptor layout creation failed (%d)", result);
    return result == VK_SUCCESS ? layout : VK_NULL_HANDLE;
}

SDL_GPUComputePipeline *SDL_CreateGPUComputePipeline(SDL_GPUDevice *device,
                                                       const SDL_GPUComputePipelineCreateInfo *info)
{
    if (device == NULL || info == NULL || info->format != SDL_GPU_SHADERFORMAT_SPIRV || info->code == NULL ||
        info->code_size == 0 || info->code_size % 4u != 0 || info->entrypoint == NULL ||
        info->num_samplers != 0 || info->num_uniform_buffers > 1) {
        SDL_SetError("unsupported Vulkan compute pipeline request"); return NULL;
    }
    SDL_GPUComputePipeline *out = calloc(1, sizeof(*out));
    if (out == NULL) { SDL_SetError("no memory for Vulkan pipeline wrapper"); return NULL; }
    out->buffer_count = info->num_readonly_storage_buffers + info->num_readwrite_storage_buffers;
    out->image_count = info->num_readonly_storage_textures + info->num_readwrite_storage_textures;
    out->uniform_count = info->num_uniform_buffers;
    out->sets[0] = make_set_layout(device, out->buffer_count, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    out->sets[1] = make_set_layout(device, out->image_count, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    out->sets[2] = make_set_layout(device, out->uniform_count, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    if (out->sets[0] == VK_NULL_HANDLE || out->sets[1] == VK_NULL_HANDLE || out->sets[2] == VK_NULL_HANDLE) goto failed;
    VkPipelineLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout_info.setLayoutCount = 3;
    layout_info.pSetLayouts = out->sets;
    VkResult result = vkCreatePipelineLayout(device->device, &layout_info, NULL, &out->layout);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan pipeline layout creation failed (%d)", result); goto failed; }
    VkShaderModuleCreateInfo shader_info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shader_info.codeSize = info->code_size;
    shader_info.pCode = (const uint32_t *)info->code;
    VkShaderModule shader = VK_NULL_HANDLE;
    result = vkCreateShaderModule(device->device, &shader_info, NULL, &shader);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan SPIR-V module creation failed (%d)", result); goto failed; }
    VkPipelineShaderStageCreateInfo stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader;
    stage.pName = info->entrypoint;
    VkComputePipelineCreateInfo pipeline = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipeline.stage = stage;
    pipeline.layout = out->layout;
    result = vkCreateComputePipelines(device->device, VK_NULL_HANDLE, 1, &pipeline, NULL, &out->pipeline);
    vkDestroyShaderModule(device->device, shader, NULL);
    if (result != VK_SUCCESS) { SDL_SetError("Vulkan compute pipeline creation failed (%d)", result); goto failed; }
    return out;
failed:
    if (out->pipeline) vkDestroyPipeline(device->device, out->pipeline, NULL);
    if (out->layout) vkDestroyPipelineLayout(device->device, out->layout, NULL);
    for (uint32_t i = 0; i < 3; ++i) if (out->sets[i]) vkDestroyDescriptorSetLayout(device->device, out->sets[i], NULL);
    free(out);
    return NULL;
}

void SDL_ReleaseGPUComputePipeline(SDL_GPUDevice *device, SDL_GPUComputePipeline *pipeline)
{
    if (device == NULL || pipeline == NULL) return;
    retire_resource(device, &pipeline->retired, RETIRED_PIPELINE);
}

SDL_GPUCommandBuffer *SDL_AcquireGPUCommandBuffer(SDL_GPUDevice *device)
{
    return device != NULL && begin_command(device) ? &device->command_api : NULL;
}

bool SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer *command)
{
    if (command == NULL || !command->open) return SDL_SetError("invalid Vulkan cancel");
    restore_cancelled_textures(command);
    const VkResult result = vkResetCommandBuffer(command->device->command, 0);
    command->open = 0;
    clear_touched(command);
    collect_retired(command->device);
    return result == VK_SUCCESS || SDL_SetError("Vulkan command cancel failed (%d)", result);
}

bool SDL_SubmitGPUCommandBuffer(SDL_GPUCommandBuffer *command)
{
    return submit_command(command, 1) != 0;
}

SDL_GPUCopyPass *SDL_BeginGPUCopyPass(SDL_GPUCommandBuffer *command)
{
    if (command == NULL || !command->open || command->failed) {
        (void)command_error(command, "invalid Vulkan copy pass");
        return NULL;
    }
    return command;
}
void SDL_EndGPUCopyPass(SDL_GPUCopyPass *pass) { (void)pass; }

void SDL_UploadToGPUBuffer(SDL_GPUCopyPass *pass, const SDL_GPUTransferBufferLocation *source,
                           const SDL_GPUBufferRegion *destination, bool cycle)
{
    (void)cycle;
    if (pass == NULL || !pass->open || source == NULL || destination == NULL ||
        source->transfer_buffer == NULL || destination->buffer == NULL) { command_error(pass, "invalid Vulkan buffer upload"); return; }
    if (pass->failed) return;
    if (source->offset > source->transfer_buffer->size ||
        destination->size > source->transfer_buffer->size - source->offset ||
        destination->offset > destination->buffer->size ||
        destination->size > destination->buffer->size - destination->offset || destination->size == 0) {
        command_error(pass, "Vulkan buffer upload range is out of bounds");
        return;
    }
    VkBufferCopy copy = {source->offset, destination->offset, destination->size};
    vkCmdCopyBuffer(pass->device->command, source->transfer_buffer->buffer, destination->buffer->buffer, 1, &copy);
    VkBufferMemoryBarrier barrier = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = destination->buffer->buffer;
    barrier.offset = destination->offset;
    barrier.size = destination->size;
    vkCmdPipelineBarrier(pass->device->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 1, &barrier, 0, NULL);
}

void SDL_UploadToGPUTexture(SDL_GPUCopyPass *pass, const SDL_GPUTextureTransferInfo *source,
                            const SDL_GPUTextureRegion *destination, bool cycle)
{
    (void)cycle;
    if (pass == NULL || !pass->open || source == NULL || destination == NULL || source->transfer_buffer == NULL ||
        destination->texture == NULL) { command_error(pass, "invalid Vulkan texture upload"); return; }
    if (pass->failed) return;
    SDL_GPUTexture *texture = destination->texture;
    image_transition(pass, texture, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy copy = {0};
    copy.bufferOffset = source->offset;
    copy.bufferRowLength = source->pixels_per_row;
    copy.bufferImageHeight = source->rows_per_layer;
    copy.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, destination->mip_level,
                                                       destination->layer, 1};
    copy.imageOffset = (VkOffset3D){(int32_t)destination->x, (int32_t)destination->y, (int32_t)destination->z};
    copy.imageExtent = (VkExtent3D){destination->w, destination->h, destination->d};
    vkCmdCopyBufferToImage(pass->device->command, source->transfer_buffer->buffer, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_transition(pass, texture, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

void SDL_CopyGPUTextureToTexture(SDL_GPUCopyPass *pass, const SDL_GPUTextureLocation *source,
                                 const SDL_GPUTextureLocation *destination, Uint32 width, Uint32 height,
                                 Uint32 depth, bool cycle)
{
    (void)cycle;
    if (pass == NULL || !pass->open || source == NULL || destination == NULL ||
        source->texture == NULL || destination->texture == NULL) { command_error(pass, "invalid Vulkan image copy"); return; }
    if (pass->failed) return;
    SDL_GPUTexture *from = source->texture, *to = destination->texture;
    image_transition(pass, from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    image_transition(pass, to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy copy = {0};
    copy.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, source->mip_level, source->layer, 1};
    copy.srcOffset = (VkOffset3D){(int32_t)source->x, (int32_t)source->y, (int32_t)source->z};
    copy.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, destination->mip_level, destination->layer, 1};
    copy.dstOffset = (VkOffset3D){(int32_t)destination->x, (int32_t)destination->y, (int32_t)destination->z};
    copy.extent = (VkExtent3D){width, height, depth};
    vkCmdCopyImage(pass->device->command, from->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   to->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    image_transition(pass, from, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    image_transition(pass, to, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

SDL_GPUComputePass *SDL_BeginGPUComputePass(SDL_GPUCommandBuffer *command,
                                            const SDL_GPUStorageTextureReadWriteBinding *textures,
                                            Uint32 texture_count,
                                            const SDL_GPUStorageBufferReadWriteBinding *buffers,
                                            Uint32 buffer_count)
{
    if (command == NULL || !command->open || texture_count > SDL_arraysize(command->images) ||
        buffer_count > SDL_arraysize(command->buffers) || (texture_count && textures == NULL) ||
        (buffer_count && buffers == NULL)) { command_error(command, "invalid Vulkan compute pass"); return NULL; }
    if (command->failed) return NULL;
    command->image_count = texture_count;
    command->buffer_count = buffer_count;
    for (Uint32 i = 0; i < texture_count; ++i) command->images[i] = textures[i].texture;
    for (Uint32 i = 0; i < buffer_count; ++i) command->buffers[i] = buffers[i].buffer;
    return command;
}

void SDL_EndGPUComputePass(SDL_GPUComputePass *pass) { (void)pass; }

void SDL_BindGPUComputePipeline(SDL_GPUComputePass *pass, SDL_GPUComputePipeline *pipeline)
{
    if (pass == NULL || !pass->open || pipeline == NULL) { command_error(pass, "invalid Vulkan compute pipeline bind"); return; }
    if (pass->failed) return;
    pass->pipeline = pipeline;
}

void SDL_BindGPUComputeStorageBuffers(SDL_GPUComputePass *pass, Uint32 first,
                                     SDL_GPUBuffer *const *buffers, Uint32 count)
{
    if (pass == NULL || !pass->open || first > SDL_arraysize(pass->buffers) || count > SDL_arraysize(pass->buffers) - first ||
        (count && buffers == NULL)) {
        command_error(pass, "invalid Vulkan storage buffer bind"); return;
    }
    if (pass->failed) return;
    for (Uint32 i = 0; i < count; ++i) pass->buffers[first + i] = buffers[i];
    if (first + count > pass->buffer_count) pass->buffer_count = first + count;
}

void SDL_BindGPUComputeStorageTextures(SDL_GPUComputePass *pass, Uint32 first,
                                      SDL_GPUTexture *const *textures, Uint32 count)
{
    if (pass == NULL || !pass->open || first > SDL_arraysize(pass->images) || count > SDL_arraysize(pass->images) - first ||
        (count && textures == NULL)) {
        command_error(pass, "invalid Vulkan storage texture bind"); return;
    }
    if (pass->failed) return;
    for (Uint32 i = 0; i < count; ++i) pass->images[first + i] = textures[i];
    if (first + count > pass->image_count) pass->image_count = first + count;
}

void SDL_PushGPUComputeUniformData(SDL_GPUCommandBuffer *command, Uint32 slot,
                                   const void *data, Uint32 length)
{
    if (command == NULL || !command->open || data == NULL || length == 0 || slot != 0) { command_error(command, "invalid Vulkan uniform data"); return; }
    if (command->failed) return;
    const VkDeviceSize align = command->device->uniform_alignment;
    const VkDeviceSize offset = (command->uniform_offset + align - 1u) / align * align;
    if (offset + length > KHDAYS_UNIFORM_BYTES) { command_error(command, "Vulkan uniform ring exhausted"); return; }
    memcpy(command->device->uniform_map + offset, data, length);
    command->uniform_offset = (uint32_t)(offset + length);
    command->uniform_size = length;
}

static int bind_compute_descriptors(SDL_GPUComputePass *pass)
{
    SDL_GPUDevice *device = pass->device;
    SDL_GPUComputePipeline *pipeline = pass->pipeline;
    if (pipeline == NULL) return command_error(pass, "Vulkan compute dispatch has no pipeline");
    if (pass->buffer_count < pipeline->buffer_count || pass->image_count < pipeline->image_count ||
        (pipeline->uniform_count && pass->uniform_size == 0))
        return command_error(pass, "Vulkan compute dispatch is missing a declared resource");
    for (uint32_t i = 0; i < pipeline->image_count; ++i) {
        if (pass->images[i] == NULL) return command_error(pass, "Vulkan compute image binding %u is empty", i);
        image_transition(pass, pass->images[i], VK_IMAGE_LAYOUT_GENERAL,
                         VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    VkDescriptorSetLayout layouts[3] = {pipeline->sets[0], pipeline->sets[1], pipeline->sets[2]};
    VkDescriptorSetAllocateInfo allocation = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocation.descriptorPool = device->descriptor_pool;
    allocation.descriptorSetCount = 3;
    allocation.pSetLayouts = layouts;
    VkResult result = vkAllocateDescriptorSets(device->device, &allocation, pass->sets);
    if (result != VK_SUCCESS) return command_error(pass, "Vulkan compute descriptor allocation failed (%d)", result);

    VkDescriptorBufferInfo buffer_info[32];
    VkWriteDescriptorSet writes[65];
    uint32_t write_count = 0;
    for (uint32_t i = 0; i < pipeline->buffer_count; ++i) {
        if (pass->buffers[i] == NULL) return command_error(pass, "Vulkan compute buffer binding %u is empty", i);
        buffer_info[i] = (VkDescriptorBufferInfo){pass->buffers[i]->buffer, 0,
                                                   pass->buffers[i]->size ? pass->buffers[i]->size : VK_WHOLE_SIZE};
        writes[write_count] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[write_count].dstSet = pass->sets[0];
        writes[write_count].dstBinding = i;
        writes[write_count].descriptorCount = 1;
        writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[write_count].pBufferInfo = &buffer_info[i];
        ++write_count;
    }
    VkDescriptorImageInfo image_info[16];
    for (uint32_t i = 0; i < pipeline->image_count; ++i) {
        image_info[i] = (VkDescriptorImageInfo){VK_NULL_HANDLE, pass->images[i]->view, VK_IMAGE_LAYOUT_GENERAL};
        writes[write_count] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[write_count].dstSet = pass->sets[1];
        writes[write_count].dstBinding = i;
        writes[write_count].descriptorCount = 1;
        writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[write_count].pImageInfo = &image_info[i];
        ++write_count;
    }
    VkDescriptorBufferInfo uniform_info = {device->uniform_buffer, 0, pass->uniform_size};
    if (pipeline->uniform_count) {
        uniform_info.offset = pass->uniform_offset - pass->uniform_size;
        writes[write_count] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        writes[write_count].dstSet = pass->sets[2];
        writes[write_count].dstBinding = 0;
        writes[write_count].descriptorCount = 1;
        writes[write_count].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[write_count].pBufferInfo = &uniform_info;
        ++write_count;
    }
    if (write_count) vkUpdateDescriptorSets(device->device, write_count, writes, 0, NULL);
    return 1;
}

void SDL_DispatchGPUCompute(SDL_GPUComputePass *pass, Uint32 x, Uint32 y, Uint32 z)
{
    if (pass == NULL || !pass->open || pass->failed) return;
    if (pass->device->command_serial <= 8)
        report(pass->device, "GPU command=%u compute groups=%ux%ux%u buffers=%u images=%u uniform=%u",
               pass->device->command_serial, x, y, z, pass->buffer_count, pass->image_count, pass->uniform_size);
    if (!bind_compute_descriptors(pass)) return;
    VkMemoryBarrier barrier = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(pass->device->command, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
    vkCmdBindPipeline(pass->device->command, VK_PIPELINE_BIND_POINT_COMPUTE, pass->pipeline->pipeline);
    vkCmdBindDescriptorSets(pass->device->command, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pass->pipeline->layout, 0, 3, pass->sets, 0, NULL);
    vkCmdDispatch(pass->device->command, x, y, z);
    pass->uniform_size = 0;
}

static void swap_image_barrier(SDL_GPUDevice *device, VkImage image, VkImageLayout old_layout,
                               VkImageLayout new_layout, VkAccessFlags src_access,
                               VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                               VkPipelineStageFlags dst_stage)
{
    VkImageMemoryBarrier barrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(device->command, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

static void blit(SDL_GPUDevice *device, SDL_GPUTexture *texture, VkImage target,
                 int left, int top, int width, int height)
{
    /* Clear and the main/PiP blits write overlapping swapchain pixels.
     * Command order alone does not establish the required WAW dependency. */
    VkMemoryBarrier order = {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    order.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    order.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(device->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &order, 0, NULL, 0, NULL);
    VkImageBlit region = {0};
    region.srcSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = (VkOffset3D){(int32_t)texture->width, (int32_t)texture->height, 1};
    region.dstSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.dstOffsets[0] = (VkOffset3D){left, top, 0};
    region.dstOffsets[1] = (VkOffset3D){left + width, top + height, 1};
    vkCmdBlitImage(device->command, texture->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_NEAREST);
}

bool khdays_vk_present(SDL_GPUDevice *device, SDL_GPUTexture *upper, SDL_GPUTexture *lower,
                       unsigned layout)
{
    if (device == NULL || upper == NULL || lower == NULL || device->swapchain == VK_NULL_HANDLE)
        return SDL_SetError("Vulkan present has no swapchain or game screens");
    SDL_GPUCommandBuffer *command = &device->command_api;
    if (!begin_command(device)) return false;
    const uint32_t presentation = ++device->present_serial;
    if (presentation <= 3) report(device, "WSI frame=%u acquire begin", presentation);
    const uint32_t slot = device->frame_slot++ % KHDAYS_MAX_SWAP_IMAGES;
    uint32_t index = 0;
    VkResult result = vkAcquireNextImageKHR(device->device, device->swapchain, UINT64_MAX,
                                            device->acquire[slot], VK_NULL_HANDLE, &index);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        (void)SDL_CancelGPUCommandBuffer(command);
        return SDL_SetError("VideoOut image acquire failed (%d)", result);
    }
    if (index >= device->swap_count) {
        (void)SDL_CancelGPUCommandBuffer(command);
        return SDL_SetError("VideoOut acquired invalid image %u of %u", index, device->swap_count);
    }
    if (presentation <= 3) report(device, "WSI frame=%u acquired image=%u; recording blits", presentation, index);
    VkImage destination = device->swap_images[index];
    /* Transition the render targets before issuing any blit. Vulkan command
     * buffers execute in recorded order, so doing this after the blit (as the
     * first prototype did) is too late to make the transfer read legal. */
    image_transition(command, upper, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
    if (lower != upper) {
        image_transition(command, lower, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
    }
    swap_image_barrier(device, destination, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkClearColorValue black = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(device->command, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);

    const int sw = (int)device->extent.width, sh = (int)device->extent.height;
    const int active = (layout == 1 || layout == 3) ? 1 : 0;
    SDL_GPUTexture *main = active ? lower : upper;
    SDL_GPUTexture *pip = active ? upper : lower;
    if (layout == 5) {
        const int source_w = upper->width > 0 ? (int)upper->width : 256;
        const int source_h = upper->height > 0 ? (int)upper->height : 192;
        int width = sw, height = (int)((int64_t)sw * source_h / source_w);
        if (height > sh) { height = sh; width = (int)((int64_t)sh * source_w / source_h); }
        blit(device, upper, destination, (sw - width) / 2, (sh - height) / 2, width, height);
    } else if (layout == 6) {
        const int source_w = upper->width > 0 ? (int)upper->width : 256;
        const int source_h = upper->height > 0 ? (int)upper->height : 192;
        int width = sw, height = (int)((int64_t)sw * (source_h * 2) / source_w);
        if (height > sh) { height = sh; width = (int)((int64_t)sh * source_w / (source_h * 2)); }
        const int left = (sw - width) / 2, top = (sh - height) / 2;
        blit(device, upper, destination, left, top, width, height / 2);
        blit(device, lower, destination, left, top + height / 2, width, height - height / 2);
    } else if (layout == 7) {
        const int source_w = upper->width > 0 ? (int)upper->width : 256;
        const int source_h = upper->height > 0 ? (int)upper->height : 192;
        int width = sw, height = (int)((int64_t)sw * source_h / (source_w * 2));
        if (height > sh) { height = sh; width = (int)((int64_t)sh * source_w * 2 / source_h); }
        const int left = (sw - width) / 2, top = (sh - height) / 2;
        blit(device, upper, destination, left, top, width / 2, height);
        blit(device, lower, destination, left + width / 2, top, width - width / 2, height);
    } else if (layout == 4) {
        const int width = sh * 2 / 3;
        const int left = (sw - width) / 2;
        blit(device, upper, destination, left, 0, width, sh / 2);
        blit(device, lower, destination, left, sh / 2, width, sh - sh / 2);
    } else {
        int width = sw, left = 0;
        if (layout >= 2) { width = sh * 4 / 3; left = (sw - width) / 2; }
        blit(device, main, destination, left, 0, width, sh);
        const int pip_width = sw / 4;
        const int pip_height = pip_width * 3 / 4;
        const int margin = sw / 80;
        blit(device, pip, destination, sw - pip_width - margin, margin, pip_width, pip_height);
    }
    /* The two game images were blitted above; return them to the compute layout for the next frame. */
    image_transition(command, upper, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    if (lower != upper) {
        image_transition(command, lower, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                         VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    swap_image_barrier(device, destination, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                       VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    result = vkEndCommandBuffer(device->command);
    if (result != VK_SUCCESS) {
        restore_cancelled_textures(command);
        command->open = 0;
        return SDL_SetError("Vulkan present recording failed (%d)", result);
    }
    command->open = 0;
    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &device->acquire[slot];
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &device->command;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &device->rendered[index];
    result = vkResetFences(device->device, 1, &device->fence);
    if (presentation <= 3) report(device, "WSI frame=%u queue submit begin", presentation);
    if (result == VK_SUCCESS) result = vkQueueSubmit(device->queue, 1, &submit, device->fence);
    if (result != VK_SUCCESS) return SDL_SetError("Vulkan present submit failed (%d)", result);
    device->pending = 1;
    clear_touched(command);
    VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &device->rendered[index];
    present.swapchainCount = 1;
    present.pSwapchains = &device->swapchain;
    present.pImageIndices = &index;
    if (presentation <= 3) report(device, "WSI frame=%u queue present begin", presentation);
    result = vkQueuePresentKHR(device->queue, &present);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        return SDL_SetError("Vulkan VideoOut present failed (%d)", result);
    if (presentation <= 3) report(device, "WSI frame=%u presented result=%d", presentation, result);
    return true;
}

void SDL_DestroyTexture(SDL_Texture *texture) { (void)texture; }
