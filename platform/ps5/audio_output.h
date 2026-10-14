#pragma once

#include <cstdint>

namespace khdays::platform::ps5 {

using AudioFill = void (*)(std::int16_t* interleaved_stereo,
                           int frame_count, void* user);

class AudioOutput {
public:
    virtual ~AudioOutput() = default;
    virtual bool start(AudioFill fill, void* user) = 0;
    virtual void stop() = 0;
};

// Uses the public audio contract from PS5_VulkanTemplate/platform.c:
// 48 kHz, signed 16-bit, interleaved stereo, 256-frame grains. The template's
// platform.c must be linked by the final PS5 title for this class to produce
// sound; the host build intentionally remains a no-audio stub.
class TemplatePs5AudioOutput final : public AudioOutput {
public:
    bool start(AudioFill fill, void* user) override;
    void stop() override;
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    bool active_ = false;
};

}  // namespace khdays::platform::ps5
