#include "audio_output.h"

namespace khdays::platform::ps5 {

#if defined(KHDAYS_PS5_NATIVE_BACKEND)
extern "C" bool audio_start(AudioFill fill, void* user);
extern "C" void audio_stop();
#endif

bool TemplatePs5AudioOutput::start(const AudioFill fill, void* user) {
    if (active_ || fill == nullptr) {
        return false;
    }
#if defined(KHDAYS_PS5_NATIVE_BACKEND)
    active_ = audio_start(fill, user);
#else
    (void)user;
    // There is no fake audio device in the host smoke build. This prevents a
    // silent success that could hide a missing PS5 platform link.
    (void)fill;
    active_ = false;
#endif
    return active_;
}

void TemplatePs5AudioOutput::stop() {
    if (!active_) {
        return;
    }
#if defined(KHDAYS_PS5_NATIVE_BACKEND)
    audio_stop();
#endif
    active_ = false;
}

}  // namespace khdays::platform::ps5
