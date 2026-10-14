#include "volk.h"

#include "vulkan_media_backend.h"

#include <array>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "VulkanBuffer.h"
#include "VulkanDevice.h"
#include "VulkanTexture.h"
#include "VulkanTools.h"

namespace khdays::platform::ps5 {

namespace {

struct VideoVertex final {
    std::array<float, 2> position;
    std::array<float, 2> texcoord;
};

bool rgba_size_is_valid(const std::uint32_t width,
                        const std::uint32_t height,
                        const std::size_t bytes) {
    if (width == 0U || height == 0U) {
        return false;
    }
    const auto expected = static_cast<std::uint64_t>(width)
        * static_cast<std::uint64_t>(height) * 4U;
    if (expected > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    return bytes == static_cast<std::size_t>(expected);
}

}  // namespace

class VulkanMediaBackend::Impl final {
public:
    struct Texture final {
        std::unique_ptr<vks::Texture2D> image;
    };

    struct Mesh final {
        vks::Buffer vertices{};
        vks::Buffer indices{};
        std::uint32_t index_count = 0;
        std::string texture_key;

        void destroy() {
            vertices.destroy();
            indices.destroy();
            index_count = 0;
        }
    };

    Impl(vks::VulkanDevice* input_device, const VkQueue input_queue)
        : device(input_device), transfer_queue(input_queue) {
        if (device == nullptr || device->logicalDevice == VK_NULL_HANDLE
            || transfer_queue == VK_NULL_HANDLE) {
            throw std::invalid_argument("Vulkan media backend needs a live device and queue");
        }
    }

    bool upload_texture(const char* key, const std::uint32_t width,
                        const std::uint32_t height, const std::uint8_t* rgba,
                        const std::size_t rgba_bytes) {
        if (key == nullptr || *key == '\0' || rgba == nullptr
            || !rgba_size_is_valid(width, height, rgba_bytes)) {
            return false;
        }
        auto image = std::make_unique<vks::Texture2D>();
        image->fromBuffer(const_cast<std::uint8_t*>(rgba), rgba_bytes,
                          VK_FORMAT_R8G8B8A8_UNORM, width, height, device,
                          transfer_queue, VK_FILTER_NEAREST,
                          VK_IMAGE_USAGE_SAMPLED_BIT
                              | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        const std::string name(key);
        const auto previous = textures.find(name);
        if (previous != textures.end()) {
            vkDeviceWaitIdle(device->logicalDevice);
            previous->second.image->destroy();
            previous->second.image = std::move(image);
        } else {
            textures.emplace(name, Texture{std::move(image)});
        }
        return true;
    }

    bool upload_mesh(const char* key, const RuntimeGpuVertex* vertices,
                     const std::size_t vertex_count,
                     const std::uint32_t* indices,
                     const std::size_t index_count,
                     const char* texture_key) {
        if (key == nullptr || *key == '\0' || vertices == nullptr
            || vertex_count == 0U || indices == nullptr || index_count == 0U
            || index_count > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const auto vertex_bytes = vertex_count * sizeof(RuntimeGpuVertex);
        const auto index_bytes = index_count * sizeof(std::uint32_t);
        Mesh uploaded;
        vks::Buffer vertex_staging{};
        vks::Buffer index_staging{};
        const auto staging_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 staging_flags, &vertex_staging,
                                 vertex_bytes,
                                 const_cast<RuntimeGpuVertex*>(vertices))
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    staging_flags, &index_staging,
                                    index_bytes,
                                    const_cast<std::uint32_t*>(indices))
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    &uploaded.vertices, vertex_bytes)
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    &uploaded.indices, index_bytes)
                != VK_SUCCESS) {
            vertex_staging.destroy();
            index_staging.destroy();
            uploaded.destroy();
            return false;
        }
        device->copyBuffer(&vertex_staging, &uploaded.vertices, transfer_queue);
        device->copyBuffer(&index_staging, &uploaded.indices, transfer_queue);
        vertex_staging.destroy();
        index_staging.destroy();
        uploaded.index_count = static_cast<std::uint32_t>(index_count);
        uploaded.texture_key = texture_key == nullptr ? "" : texture_key;

        const std::string name(key);
        const auto previous = meshes.find(name);
        if (previous != meshes.end()) {
            vkDeviceWaitIdle(device->logicalDevice);
            previous->second.destroy();
            previous->second = std::move(uploaded);
        } else {
            meshes.emplace(name, std::move(uploaded));
        }
        return true;
    }

    bool ensure_video_quad() {
        if (video_quad_ready) {
            return true;
        }
        constexpr std::array<VideoVertex, 4> vertices{{
            {{{-1.0f, -1.0f}}, {{0.0f, 1.0f}}},
            {{{ 1.0f, -1.0f}}, {{1.0f, 1.0f}}},
            {{{ 1.0f,  1.0f}}, {{1.0f, 0.0f}}},
            {{{-1.0f,  1.0f}}, {{0.0f, 0.0f}}},
        }};
        constexpr std::array<std::uint32_t, 6> indices{{0, 1, 2, 2, 3, 0}};
        vks::Buffer vertex_staging{};
        vks::Buffer index_staging{};
        const auto staging_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                 staging_flags, &vertex_staging,
                                 sizeof(vertices),
                                 const_cast<VideoVertex*>(vertices.data()))
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    staging_flags, &index_staging,
                                    sizeof(indices),
                                    const_cast<std::uint32_t*>(indices.data()))
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
                                        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    &video_vertices, sizeof(vertices))
                != VK_SUCCESS
            || device->createBuffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT
                                        | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    &video_indices, sizeof(indices))
                != VK_SUCCESS) {
            vertex_staging.destroy();
            index_staging.destroy();
            video_vertices.destroy();
            video_indices.destroy();
            return false;
        }
        device->copyBuffer(&vertex_staging, &video_vertices, transfer_queue);
        device->copyBuffer(&index_staging, &video_indices, transfer_queue);
        vertex_staging.destroy();
        index_staging.destroy();
        video_quad_ready = true;
        return true;
    }

    bool present_video_frame(const std::uint64_t frame_index,
                             const std::uint32_t width,
                             const std::uint32_t height,
                             const std::uint8_t* rgba,
                             const std::size_t rgba_bytes) {
        if (rgba == nullptr || !rgba_size_is_valid(width, height, rgba_bytes)
            || !ensure_video_quad()) {
            return false;
        }
        if (video == nullptr || video->width != width || video->height != height) {
            if (video != nullptr) {
                vkDeviceWaitIdle(device->logicalDevice);
                video->destroy();
            }
            video = std::make_unique<vks::Texture2D>();
            video->fromBuffer(const_cast<std::uint8_t*>(rgba), rgba_bytes,
                              VK_FORMAT_R8G8B8A8_UNORM, width, height,
                              device, transfer_queue, VK_FILTER_LINEAR,
                              VK_IMAGE_USAGE_SAMPLED_BIT
                                  | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        } else {
            vks::Buffer staging{};
            if (device->createBuffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                                         | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                     &staging, rgba_bytes,
                                     const_cast<std::uint8_t*>(rgba))
                != VK_SUCCESS) {
                return false;
            }
            VkCommandBuffer command = device->createCommandBuffer(
                VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
            const VkImageSubresourceRange range{
                VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U};
            vks::tools::setImageLayout(
                command, video->image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, range);
            const VkBufferImageCopy copy{
                0U, 0U, 0U,
                {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 0U, 1U},
                {0, 0, 0}, {width, height, 1U}};
            vkCmdCopyBufferToImage(command, staging.buffer, video->image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1U, &copy);
            vks::tools::setImageLayout(
                command, video->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, range);
            device->flushCommandBuffer(command, transfer_queue);
            staging.destroy();
        }
        current_video_frame = frame_index;
        return true;
    }

    void shutdown() {
        if (device == nullptr) {
            return;
        }
        vkDeviceWaitIdle(device->logicalDevice);
        for (auto& [key, mesh] : meshes) {
            (void)key;
            mesh.destroy();
        }
        meshes.clear();
        for (auto& [key, texture] : textures) {
            (void)key;
            texture.image->destroy();
        }
        textures.clear();
        if (video != nullptr) {
            video->destroy();
            video.reset();
        }
        video_vertices.destroy();
        video_indices.destroy();
        video_quad_ready = false;
        device = nullptr;
        transfer_queue = VK_NULL_HANDLE;
    }

    vks::VulkanDevice* device = nullptr;
    VkQueue transfer_queue = VK_NULL_HANDLE;
    std::unordered_map<std::string, Texture> textures;
    std::unordered_map<std::string, Mesh> meshes;
    std::unique_ptr<vks::Texture2D> video;
    vks::Buffer video_vertices{};
    vks::Buffer video_indices{};
    std::uint64_t current_video_frame = 0U;
    bool video_quad_ready = false;
};

VulkanMediaBackend::VulkanMediaBackend(vks::VulkanDevice* device,
                                       const VkQueue transfer_queue)
    : impl_(std::make_unique<Impl>(device, transfer_queue)) {}

VulkanMediaBackend::~VulkanMediaBackend() {
    shutdown();
}

VulkanMediaBackend::VulkanMediaBackend(VulkanMediaBackend&&) noexcept = default;
VulkanMediaBackend& VulkanMediaBackend::operator=(
    VulkanMediaBackend&&) noexcept = default;

RuntimeMediaHooks VulkanMediaBackend::hooks() noexcept {
    return RuntimeMediaHooks{this, &upload_texture_callback,
                             &upload_mesh_callback,
                             &present_video_frame_callback};
}

bool VulkanMediaBackend::record_meshes(
    const VkCommandBuffer command_buffer, const TextureBind bind_texture,
    void* user) const {
    if (impl_ == nullptr || command_buffer == VK_NULL_HANDLE
        || bind_texture == nullptr) {
        return false;
    }
    for (const auto& [key, mesh] : impl_->meshes) {
        (void)key;
        const auto texture = impl_->textures.find(mesh.texture_key);
        const VkDescriptorImageInfo* descriptor = texture == impl_->textures.end()
            ? nullptr : &texture->second.image->descriptor;
        if (!bind_texture(user, command_buffer, descriptor)) {
            return false;
        }
        const VkDeviceSize offset = 0U;
        vkCmdBindVertexBuffers(command_buffer, 0U, 1U,
                               &mesh.vertices.buffer, &offset);
        vkCmdBindIndexBuffer(command_buffer, mesh.indices.buffer, 0U,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(command_buffer, mesh.index_count, 1U, 0U, 0, 0U);
    }
    return true;
}

bool VulkanMediaBackend::record_video_quad(
    const VkCommandBuffer command_buffer) const {
    if (impl_ == nullptr || command_buffer == VK_NULL_HANDLE
        || impl_->video == nullptr || !impl_->video_quad_ready) {
        return false;
    }
    const VkDeviceSize offset = 0U;
    vkCmdBindVertexBuffers(command_buffer, 0U, 1U,
                           &impl_->video_vertices.buffer, &offset);
    vkCmdBindIndexBuffer(command_buffer, impl_->video_indices.buffer, 0U,
                         VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(command_buffer, 6U, 1U, 0U, 0, 0U);
    return true;
}

const VkDescriptorImageInfo* VulkanMediaBackend::video_descriptor() const {
    return impl_ == nullptr || impl_->video == nullptr
        ? nullptr : &impl_->video->descriptor;
}

std::uint64_t VulkanMediaBackend::video_frame_index() const noexcept {
    return impl_ == nullptr ? 0U : impl_->current_video_frame;
}

std::size_t VulkanMediaBackend::texture_count() const noexcept {
    return impl_ == nullptr ? 0U : impl_->textures.size();
}

std::size_t VulkanMediaBackend::mesh_count() const noexcept {
    return impl_ == nullptr ? 0U : impl_->meshes.size();
}

VkVertexInputBindingDescription
VulkanMediaBackend::mesh_binding_description() {
    return {0U, sizeof(RuntimeGpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
}

void VulkanMediaBackend::mesh_attribute_descriptions(
    VkVertexInputAttributeDescription (&attributes)[5]) {
    attributes[0] = {0U, 0U, VK_FORMAT_R32G32B32_SFLOAT,
                     offsetof(RuntimeGpuVertex, position)};
    attributes[1] = {1U, 0U, VK_FORMAT_R32G32_SFLOAT,
                     offsetof(RuntimeGpuVertex, texcoord)};
    attributes[2] = {2U, 0U, VK_FORMAT_R8G8B8A8_UNORM,
                     offsetof(RuntimeGpuVertex, color)};
    attributes[3] = {3U, 0U, VK_FORMAT_R32G32B32A32_UINT,
                     offsetof(RuntimeGpuVertex, joints)};
    attributes[4] = {4U, 0U, VK_FORMAT_R32G32B32A32_SFLOAT,
                     offsetof(RuntimeGpuVertex, weights)};
}

VkVertexInputBindingDescription
VulkanMediaBackend::video_binding_description() {
    return {0U, sizeof(VideoVertex), VK_VERTEX_INPUT_RATE_VERTEX};
}

void VulkanMediaBackend::video_attribute_descriptions(
    VkVertexInputAttributeDescription (&attributes)[2]) {
    attributes[0] = {0U, 0U, VK_FORMAT_R32G32_SFLOAT,
                     offsetof(VideoVertex, position)};
    attributes[1] = {1U, 0U, VK_FORMAT_R32G32_SFLOAT,
                     offsetof(VideoVertex, texcoord)};
}

void VulkanMediaBackend::shutdown() {
    if (impl_ != nullptr) {
        impl_->shutdown();
    }
}

bool VulkanMediaBackend::upload_texture_callback(
    void* user, const char* key, const std::uint32_t width,
    const std::uint32_t height, const std::uint8_t* rgba,
    const std::size_t rgba_bytes) {
    try {
        return user != nullptr
            && static_cast<VulkanMediaBackend*>(user)->impl_->upload_texture(
                key, width, height, rgba, rgba_bytes);
    } catch (...) {
        return false;
    }
}

bool VulkanMediaBackend::upload_mesh_callback(
    void* user, const char* key, const RuntimeGpuVertex* vertices,
    const std::size_t vertex_count, const std::uint32_t* indices,
    const std::size_t index_count, const char* texture_key) {
    try {
        return user != nullptr
            && static_cast<VulkanMediaBackend*>(user)->impl_->upload_mesh(
                key, vertices, vertex_count, indices, index_count, texture_key);
    } catch (...) {
        return false;
    }
}

bool VulkanMediaBackend::present_video_frame_callback(
    void* user, const std::uint64_t frame_index, const std::uint32_t width,
    const std::uint32_t height, const std::uint8_t* rgba,
    const std::size_t rgba_bytes) {
    try {
        return user != nullptr
            && static_cast<VulkanMediaBackend*>(user)->impl_->present_video_frame(
                frame_index, width, height, rgba, rgba_bytes);
    } catch (...) {
        return false;
    }
}

}  // namespace khdays::platform::ps5
