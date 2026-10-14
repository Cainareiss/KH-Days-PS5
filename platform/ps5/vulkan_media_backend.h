#pragma once

#include "runtime_media_pipeline.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include <vulkan/vulkan.h>

namespace vks {
class VulkanDevice;
}

namespace khdays::platform::ps5 {

// Owns the Vulkan resources produced by RuntimeMediaPipeline. The object must
// be created after VulkanExampleBase has initialized its VulkanDevice and
// transfer queue, and destroyed before those objects are torn down.
class VulkanMediaBackend final {
public:
    using TextureBind = bool (*)(void* user, VkCommandBuffer command_buffer,
                                 const VkDescriptorImageInfo* descriptor);

    VulkanMediaBackend(vks::VulkanDevice* device, VkQueue transfer_queue);
    ~VulkanMediaBackend();
    VulkanMediaBackend(VulkanMediaBackend&&) noexcept;
    VulkanMediaBackend& operator=(VulkanMediaBackend&&) noexcept;
    VulkanMediaBackend(const VulkanMediaBackend&) = delete;
    VulkanMediaBackend& operator=(const VulkanMediaBackend&) = delete;

    [[nodiscard]] RuntimeMediaHooks hooks() noexcept;

    // Records all decoded model meshes. The title callback binds the
    // descriptor set for each mesh texture before the indexed draw.
    bool record_meshes(VkCommandBuffer command_buffer,
                       TextureBind bind_texture, void* user) const;

    // Records the full-screen quad for the most recently decoded MODS frame.
    // The caller owns the compatible graphics pipeline and descriptor set.
    bool record_video_quad(VkCommandBuffer command_buffer) const;

    [[nodiscard]] const VkDescriptorImageInfo* video_descriptor() const;
    [[nodiscard]] std::uint64_t video_frame_index() const noexcept;
    [[nodiscard]] std::size_t texture_count() const noexcept;
    [[nodiscard]] std::size_t mesh_count() const noexcept;

    // Vertex layouts used by the title's graphics pipelines.
    static VkVertexInputBindingDescription mesh_binding_description();
    static void mesh_attribute_descriptions(
        VkVertexInputAttributeDescription (&attributes)[5]);
    static VkVertexInputBindingDescription video_binding_description();
    static void video_attribute_descriptions(
        VkVertexInputAttributeDescription (&attributes)[3]);

    void shutdown();

private:
    static bool upload_texture_callback(
        void* user, const char* key, std::uint32_t width,
        std::uint32_t height, const std::uint8_t* rgba,
        std::size_t rgba_bytes);
    static bool upload_mesh_callback(
        void* user, const char* key, const RuntimeGpuVertex* vertices,
        std::size_t vertex_count, const std::uint32_t* indices,
        std::size_t index_count, const char* texture_key);
    static bool present_video_frame_callback(
        void* user, std::uint64_t frame_index, std::uint32_t width,
        std::uint32_t height, const std::uint8_t* rgba,
        std::size_t rgba_bytes);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace khdays::platform::ps5
