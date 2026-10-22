/* Every Vulkan command called by sdl_gpu_vulkan.c. Kept in three phases so
 * a missing driver entry is rejected before anything calls its NULL pointer. */
#ifndef KHDAYS_VULKAN_DISPATCH_REQUIRED_H
#define KHDAYS_VULKAN_DISPATCH_REQUIRED_H
#define KHDAYS_VK_GLOBAL_COMMANDS(X) \
    X(CreateInstance) X(EnumerateInstanceExtensionProperties)
#define KHDAYS_VK_INSTANCE_COMMANDS(X) \
    X(DestroyInstance) X(EnumeratePhysicalDevices) X(GetDeviceProcAddr) \
    X(GetPhysicalDeviceProperties) X(GetPhysicalDeviceMemoryProperties) \
    X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceFormatProperties) \
    X(EnumerateDeviceExtensionProperties) X(CreateDevice) \
    X(GetPhysicalDeviceDisplayPropertiesKHR) X(GetDisplayModePropertiesKHR) \
    X(CreateDisplayPlaneSurfaceKHR) X(DestroySurfaceKHR) \
    X(GetPhysicalDeviceSurfaceSupportKHR) X(GetPhysicalDeviceSurfaceCapabilitiesKHR) \
    X(GetPhysicalDeviceSurfaceFormatsKHR)
#define KHDAYS_VK_DEVICE_COMMANDS(X) \
    X(DestroyDevice) X(DeviceWaitIdle) X(GetDeviceQueue) \
    X(CreateSwapchainKHR) X(DestroySwapchainKHR) X(GetSwapchainImagesKHR) \
    X(AcquireNextImageKHR) X(QueuePresentKHR) \
    X(CreateSemaphore) X(DestroySemaphore) X(CreateFence) X(DestroyFence) \
    X(WaitForFences) X(ResetFences) X(QueueSubmit) \
    X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
    X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(CreateDescriptorPool) X(DestroyDescriptorPool) X(ResetDescriptorPool) \
    X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) \
    X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
    X(CreatePipelineLayout) X(DestroyPipelineLayout) \
    X(CreateShaderModule) X(DestroyShaderModule) \
    X(CreateComputePipelines) X(DestroyPipeline) \
    X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(BindBufferMemory) \
    X(CreateImage) X(DestroyImage) X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(AllocateMemory) X(FreeMemory) \
    X(MapMemory) X(UnmapMemory) X(FlushMappedMemoryRanges) \
    X(CmdPipelineBarrier) X(CmdCopyBuffer) X(CmdCopyBufferToImage) X(CmdCopyImage) \
    X(CmdBlitImage) X(CmdClearColorImage) X(CmdBindPipeline) \
    X(CmdBindDescriptorSets) X(CmdDispatch)
#endif
