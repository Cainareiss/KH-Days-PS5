#pragma once

#include "runtime_media_pipeline.h"
#include "vulkan_media_backend.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace khdays::platform::ps5 {

// Concrete owner used by the PS5 Vulkan title. It joins decoded game data,
// GPU resources and the template's native sceAudioOut-backed output.
class NativeMediaRuntime final {
public:
    NativeMediaRuntime(vks::VulkanDevice* device, VkQueue transfer_queue,
                       std::filesystem::path data_root);
    ~NativeMediaRuntime();
    NativeMediaRuntime(const NativeMediaRuntime&) = delete;
    NativeMediaRuntime& operator=(const NativeMediaRuntime&) = delete;

    bool start_audio(std::string* error = nullptr);
    bool load_model(const std::filesystem::path& relative_path,
                    std::string* error = nullptr);
    bool play_sdat_stream(const std::filesystem::path& relative_path,
                          std::size_t stream_index,
                          std::string* error = nullptr);
    bool open_video(const std::filesystem::path& relative_path,
                    std::string* error = nullptr);
    bool tick_video(std::int64_t elapsed_ns, std::string* error = nullptr);

    bool record_meshes(VkCommandBuffer command_buffer,
                       VulkanMediaBackend::TextureBind bind_texture,
                       void* user) const;
    bool record_video_quad(VkCommandBuffer command_buffer) const;
    [[nodiscard]] const VkDescriptorImageInfo* video_descriptor() const;
    [[nodiscard]] RuntimeMediaStats stats() const;
    [[nodiscard]] bool video_finished() const;
    void shutdown();

private:
    VulkanMediaBackend gpu_;
    RuntimeMediaPipeline media_;
    TemplatePs5AudioOutput audio_;
    bool stopped_ = false;
};

}  // namespace khdays::platform::ps5
