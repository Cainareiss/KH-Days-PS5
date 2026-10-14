#pragma once

#include "audio_output.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace khdays::platform::ps5 {

struct RuntimeGpuVertex final {
    std::array<float, 3> position{};
    std::array<float, 2> texcoord{};
    std::array<std::uint8_t, 4> color{};
    std::array<std::uint32_t, 4> joints{};
    std::array<float, 4> weights{};
};

// Implemented by the concrete Vulkan/VideoOut owner. All pointers remain
// valid for the duration of the callback; the backend must copy or upload them
// before returning.
struct RuntimeMediaHooks final {
    void* user = nullptr;
    bool (*upload_texture)(void* user, const char* key, std::uint32_t width,
                           std::uint32_t height, const std::uint8_t* rgba,
                           std::size_t rgba_bytes) = nullptr;
    bool (*upload_mesh)(void* user, const char* key,
                        const RuntimeGpuVertex* vertices,
                        std::size_t vertex_count,
                        const std::uint32_t* indices,
                        std::size_t index_count,
                        const char* texture_key) = nullptr;
    bool (*present_video_frame)(void* user, std::uint64_t frame_index,
                                std::uint32_t width, std::uint32_t height,
                                const std::uint8_t* rgba,
                                std::size_t rgba_bytes) = nullptr;
};

struct RuntimeMediaStats final {
    std::size_t textures_uploaded = 0;
    std::size_t meshes_uploaded = 0;
    std::size_t video_frames_presented = 0;
    std::size_t audio_frames_queued = 0;
};

class RuntimeMediaPipeline final {
public:
    RuntimeMediaPipeline(std::filesystem::path data_root,
                         RuntimeMediaHooks hooks);
    ~RuntimeMediaPipeline();
    RuntimeMediaPipeline(RuntimeMediaPipeline&&) noexcept;
    RuntimeMediaPipeline& operator=(RuntimeMediaPipeline&&) noexcept;
    RuntimeMediaPipeline(const RuntimeMediaPipeline&) = delete;
    RuntimeMediaPipeline& operator=(const RuntimeMediaPipeline&) = delete;

    bool upload_kaph_model(const std::filesystem::path& relative_path,
                           std::string* error = nullptr);
    bool queue_sdat_stream(const std::filesystem::path& relative_path,
                           std::size_t stream_index,
                           std::string* error = nullptr);
    bool open_mods_video(const std::filesystem::path& relative_path,
                         std::string* error = nullptr);
    bool decode_next_video_frame(std::string* error = nullptr);
    // Advances MODS playback using the container's own frame rate. The first
    // call presents frame zero immediately; subsequent calls may decode more
    // than one frame when the render loop has fallen behind.
    bool advance_video(std::int64_t elapsed_ns, std::string* error = nullptr);

    bool start_audio(AudioOutput& output);
    void stop_audio();
    [[nodiscard]] bool video_finished() const;
    [[nodiscard]] std::int64_t video_frame_duration_ns() const;
    [[nodiscard]] RuntimeMediaStats stats() const;

private:
    static void fill_audio(std::int16_t* interleaved_stereo,
                           int frame_count, void* user);

    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace khdays::platform::ps5
