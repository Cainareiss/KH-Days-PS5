#include "native_media_runtime.h"

#include <utility>

#if !defined(KHDAYS_PS5_NATIVE_BACKEND)
#error "NativeMediaRuntime requires KHDAYS_PS5_NATIVE_BACKEND=ON"
#endif

namespace khdays::platform::ps5 {

NativeMediaRuntime::NativeMediaRuntime(
    vks::VulkanDevice* device, const VkQueue transfer_queue,
    std::filesystem::path data_root)
    : gpu_(device, transfer_queue),
      media_(std::move(data_root), gpu_.hooks()) {}

NativeMediaRuntime::~NativeMediaRuntime() {
    shutdown();
}

bool NativeMediaRuntime::start_audio(std::string* error) {
    if (stopped_) {
        if (error != nullptr) {
            *error = "native media runtime is shut down";
        }
        return false;
    }
    if (audio_.active()) {
        return true;
    }
    if (!media_.start_audio(audio_)) {
        if (error != nullptr) {
            *error = "sceAudioOut output could not be opened";
        }
        return false;
    }
    return true;
}

bool NativeMediaRuntime::load_model(
    const std::filesystem::path& relative_path, std::string* error) {
    if (stopped_) {
        if (error != nullptr) {
            *error = "native media runtime is shut down";
        }
        return false;
    }
    return media_.upload_kaph_model(relative_path, error);
}

bool NativeMediaRuntime::play_sdat_stream(
    const std::filesystem::path& relative_path,
    const std::size_t stream_index, std::string* error) {
    if (!start_audio(error)) {
        return false;
    }
    return media_.queue_sdat_stream(relative_path, stream_index, error);
}

bool NativeMediaRuntime::open_video(
    const std::filesystem::path& relative_path, std::string* error) {
    if (!start_audio(error)) {
        return false;
    }
    return media_.open_mods_video(relative_path, error);
}

bool NativeMediaRuntime::tick_video(const std::int64_t elapsed_ns,
                                    std::string* error) {
    if (stopped_) {
        if (error != nullptr) {
            *error = "native media runtime is shut down";
        }
        return false;
    }
    return media_.advance_video(elapsed_ns, error);
}

bool NativeMediaRuntime::record_meshes(
    const VkCommandBuffer command_buffer,
    const VulkanMediaBackend::TextureBind bind_texture, void* user) const {
    return !stopped_
        && gpu_.record_meshes(command_buffer, bind_texture, user);
}

bool NativeMediaRuntime::record_video_quad(
    const VkCommandBuffer command_buffer) const {
    return !stopped_ && gpu_.record_video_quad(command_buffer);
}

const VkDescriptorImageInfo* NativeMediaRuntime::video_descriptor() const {
    return stopped_ ? nullptr : gpu_.video_descriptor();
}

RuntimeMediaStats NativeMediaRuntime::stats() const {
    return media_.stats();
}

bool NativeMediaRuntime::video_finished() const {
    return media_.video_finished();
}

void NativeMediaRuntime::shutdown() {
    if (stopped_) {
        return;
    }
    // The audio thread can access media_'s queue, so stop it before releasing
    // either the decoder state or Vulkan resources.
    media_.stop_audio();
    gpu_.shutdown();
    stopped_ = true;
}

}  // namespace khdays::platform::ps5
