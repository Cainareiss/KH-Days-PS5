/* Run the actual backend and Volk with an ICD that deliberately omits each
 * required entrypoint. No GPU or PS5 service is mocked as having succeeded:
 * the test stops at the dispatch contract, before rendering or presentation. */
#define VK_NO_PROTOTYPES 1
#include <volk.h>
#include "sdl_gpu_vulkan.h"
#include "vulkan_dispatch_required.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *missing;
static unsigned instances_created, instances_destroyed, devices_created, devices_destroyed;
static unsigned reports;

static void VKAPI_CALL unexpected(void)
{
    fputs("unexpected mock command reached beyond dispatch validation\n", stderr);
    abort();
}
static VkResult VKAPI_CALL create_instance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *a, VkInstance *out)
{
    (void)ci; (void)a;
    *out = (VkInstance)(uintptr_t)1;
    ++instances_created;
    return VK_SUCCESS;
}
static void VKAPI_CALL destroy_instance(VkInstance i, const VkAllocationCallbacks *a)
{
    (void)i; (void)a;
    ++instances_destroyed;
}
static VkResult VKAPI_CALL instance_extensions(const char *layer, uint32_t *count, VkExtensionProperties *out)
{
    (void)layer;
    if (out) {
        memset(out, 0, 2 * sizeof(*out));
        strcpy(out[0].extensionName, VK_KHR_SURFACE_EXTENSION_NAME);
        strcpy(out[1].extensionName, VK_KHR_DISPLAY_EXTENSION_NAME);
    }
    *count = 2;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL physicals(VkInstance i, uint32_t *count, VkPhysicalDevice *out)
{
    (void)i;
    if (out) *out = (VkPhysicalDevice)(uintptr_t)2;
    *count = 1;
    return VK_SUCCESS;
}
static void VKAPI_CALL properties(VkPhysicalDevice p, VkPhysicalDeviceProperties *out)
{
    (void)p;
    memset(out, 0, sizeof(*out));
    out->apiVersion = VK_API_VERSION_1_1;
    strcpy(out->deviceName, "dispatch-contract fixture");
}
static void VKAPI_CALL memories(VkPhysicalDevice p, VkPhysicalDeviceMemoryProperties *out)
{
    (void)p;
    memset(out, 0, sizeof(*out));
}
static void VKAPI_CALL queues(VkPhysicalDevice p, uint32_t *count, VkQueueFamilyProperties *out)
{
    (void)p;
    if (out) {
        memset(out, 0, sizeof(*out));
        out->queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        out->queueCount = 1;
    }
    *count = 1;
}
static VkResult VKAPI_CALL device_extensions(VkPhysicalDevice p, const char *layer, uint32_t *count, VkExtensionProperties *out)
{
    (void)p; (void)layer;
    if (out) {
        memset(out, 0, sizeof(*out));
        strcpy(out->extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    *count = 1;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL create_device(VkPhysicalDevice p, const VkDeviceCreateInfo *ci, const VkAllocationCallbacks *a, VkDevice *out)
{
    (void)p; (void)ci; (void)a;
    *out = (VkDevice)(uintptr_t)3;
    ++devices_created;
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL wait_idle(VkDevice d) { (void)d; return VK_SUCCESS; }
static void VKAPI_CALL destroy_device(VkDevice d, const VkAllocationCallbacks *a)
{
    (void)d; (void)a;
    ++devices_destroyed;
}
static PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice d, const char *name)
{
    (void)d;
    if (!strcmp(name, missing)) return NULL;
    if (!strcmp(name, "vkDeviceWaitIdle")) return (PFN_vkVoidFunction)wait_idle;
    if (!strcmp(name, "vkDestroyDevice")) return (PFN_vkVoidFunction)destroy_device;
    return unexpected;
}
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance i, const char *name)
{
    (void)i;
    if (!strcmp(name, missing)) return NULL;
#define ROUTE(n, f) if (!strcmp(name, "vk" #n)) return (PFN_vkVoidFunction)f
    ROUTE(CreateInstance, create_instance);
    ROUTE(DestroyInstance, destroy_instance);
    ROUTE(EnumerateInstanceExtensionProperties, instance_extensions);
    ROUTE(EnumeratePhysicalDevices, physicals);
    ROUTE(GetPhysicalDeviceProperties, properties);
    ROUTE(GetPhysicalDeviceMemoryProperties, memories);
    ROUTE(GetPhysicalDeviceQueueFamilyProperties, queues);
    ROUTE(EnumerateDeviceExtensionProperties, device_extensions);
    ROUTE(CreateDevice, create_device);
    ROUTE(GetDeviceProcAddr, device_proc);
#undef ROUTE
    return unexpected;
}
static void capture(const char *line) { assert(line && *line); ++reports; }

int main(void)
{
#define NAME(n) "vk" #n,
    static const char *const global[] = {KHDAYS_VK_GLOBAL_COMMANDS(NAME)};
    static const char *const instance[] = {KHDAYS_VK_INSTANCE_COMMANDS(NAME)};
    static const char *const device[] = {KHDAYS_VK_DEVICE_COMMANDS(NAME)};
#undef NAME
    const char *const *groups[] = {global, instance, device};
    const size_t sizes[] = {sizeof(global)/sizeof(*global), sizeof(instance)/sizeof(*instance), sizeof(device)/sizeof(*device)};
    unsigned cases = 0;
    for (unsigned group = 0; group < 3; ++group) {
        for (size_t n = 0; n < sizes[group]; ++n) {
            instances_created = instances_destroyed = devices_created = devices_destroyed = reports = 0;
            missing = groups[group][n];
            assert(khdays_vk_device_open(1920, 1080, capture) == NULL);
            assert(strstr(khdays_vk_error(), missing));
            assert(strstr(khdays_vk_error(), "missing Vulkan entrypoint:"));
            assert(reports >= 2);
            assert(instances_created == (group != 0));
            assert(instances_destroyed == (group != 0 && strcmp(missing, "vkDestroyInstance") != 0));
            assert(devices_created == (group == 2));
            assert(devices_destroyed == (group == 2 && strcmp(missing, "vkDestroyDevice") != 0));
            ++cases;
        }
    }
    printf("Vulkan bootstrap: PASS (%u missing-entrypoint cases, safe failure and partial cleanup)\n", cases);
    return 0;
}
