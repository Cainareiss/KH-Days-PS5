#pragma once

#include <cstdint>

#include "khdays_ps5/frontend_core.h"

namespace khdays::platform::ps5 {

// PS5-side services required by the neutral front-end. This header deliberately
// contains no Sony or Vulkan headers, so the scene/game libraries remain
// buildable on the PC. The concrete implementation will be added once the
// public PS5 payload toolchain is installed.
class FrontendAdapter : public khdays::ps5::frontend::Renderer {
public:
    virtual ~FrontendAdapter() = default;

    // Create VideoOut/display and the renderer-owned presentation resources.
    virtual bool initialize(int width, int height) = 0;

    // Return one frame of input mapped to the game's device-neutral buttons.
    virtual khdays::ps5::frontend::Input poll_input() = 0;

    // Present the frame after SceneManager::render() has drawn it. The PS5
    // implementation owns acquire/submit/present and vblank synchronization.
    virtual void present() = 0;

    // Keep the game clock separate from the display implementation. The caller
    // uses this to pace 60 Hz or the original 59.8261 Hz DS cadence.
    virtual void wait_for_next_frame(std::int64_t duration_ns) = 0;

    virtual void shutdown() = 0;
};

}  // namespace khdays::platform::ps5

