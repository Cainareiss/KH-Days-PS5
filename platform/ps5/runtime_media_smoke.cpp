#include "runtime_media_pipeline.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

struct Capture final {
    std::size_t textures = 0;
    std::size_t meshes = 0;
    std::size_t video_frames = 0;
};

bool upload_texture(void* user, const char*, const std::uint32_t width,
                    const std::uint32_t height, const std::uint8_t* rgba,
                    const std::size_t size) {
    auto& capture = *static_cast<Capture*>(user);
    if (width == 0U || height == 0U || rgba == nullptr
        || size != static_cast<std::size_t>(width) * height * 4U) {
        return false;
    }
    ++capture.textures;
    return true;
}

bool upload_mesh(void* user, const char*,
                 const khdays::platform::ps5::RuntimeGpuVertex* vertices,
                 const std::size_t vertex_count, const std::uint32_t* indices,
                 const std::size_t index_count, const char*) {
    auto& capture = *static_cast<Capture*>(user);
    if (vertices == nullptr || vertex_count == 0U || indices == nullptr
        || index_count == 0U || index_count % 3U != 0U) {
        return false;
    }
    ++capture.meshes;
    return true;
}

bool present_video(void* user, const std::uint64_t, const std::uint32_t width,
                   const std::uint32_t height, const std::uint8_t* rgba,
                   const std::size_t size) {
    auto& capture = *static_cast<Capture*>(user);
    if (width == 0U || height == 0U || rgba == nullptr
        || size != static_cast<std::size_t>(width) * height * 4U) {
        return false;
    }
    ++capture.video_frames;
    return true;
}

class PullAudio final : public khdays::platform::ps5::AudioOutput {
public:
    bool start(const khdays::platform::ps5::AudioFill fill, void* user) override {
        if (fill == nullptr) {
            return false;
        }
        constexpr int kProbeFrames = 48'000;
        std::vector<std::int16_t> samples(static_cast<std::size_t>(kProbeFrames) * 2U, 0);
        fill(samples.data(), kProbeFrames, user);
        nonzero_samples = static_cast<std::size_t>(std::count_if(
            samples.begin(), samples.end(),
            [](const std::int16_t sample) { return sample != 0; }));
        active = true;
        return true;
    }
    void stop() override { active = false; }

    bool active = false;
    std::size_t nonzero_samples = 0;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: khdays-ps5-media-smoke <data-root>\n";
        return 2;
    }
    Capture capture;
    khdays::platform::ps5::RuntimeMediaHooks hooks;
    hooks.user = &capture;
    hooks.upload_texture = &upload_texture;
    hooks.upload_mesh = &upload_mesh;
    hooks.present_video_frame = &present_video;
    khdays::platform::ps5::RuntimeMediaPipeline pipeline(argv[1], hooks);
    std::string error;
    if (!pipeline.upload_kaph_model("ba/ch/ro/def.p.z", &error)) {
        std::cerr << "model: " << error << '\n';
        return 1;
    }
    if (!pipeline.queue_sdat_stream("snd/sound_data.sdat", 0U, &error)) {
        std::cerr << "audio: " << error << '\n';
        return 1;
    }
    if (!pipeline.open_mods_video("mv/802.mods", &error)) {
        std::cerr << "video open: " << error << '\n';
        return 1;
    }
    if (!pipeline.advance_video(0, &error)) {
        std::cerr << "initial video frame: " << error << '\n';
        return 1;
    }
    const auto frame_ns = pipeline.video_frame_duration_ns();
    if (frame_ns <= 1 || !pipeline.advance_video(frame_ns - 1, &error)
        || capture.video_frames != 1U
        || !pipeline.advance_video(1, &error)) {
        std::cerr << "video pacing: " << error << '\n';
        return 1;
    }
    PullAudio audio;
    if (!pipeline.start_audio(audio)) {
        std::cerr << "audio output did not start\n";
        return 1;
    }
    pipeline.stop_audio();
    const auto stats = pipeline.stats();
    std::cout << "textures=" << stats.textures_uploaded
              << " meshes=" << stats.meshes_uploaded
              << " video_frames=" << stats.video_frames_presented
              << " audio_frames=" << stats.audio_frames_queued
              << " audio_nonzero=" << audio.nonzero_samples << '\n';
    return capture.textures > 0U && capture.meshes > 0U
        && capture.video_frames == 2U && audio.nonzero_samples > 0U ? 0 : 1;
}
