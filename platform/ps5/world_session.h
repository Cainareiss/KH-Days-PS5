#pragma once

#include "khdays_ps5/world_runtime.h"
#include "vulkan_videoout_adapter.h"

#include <string>
#include <utility>

namespace khdays::platform::ps5 {

// Connects the first-world simulation to the same Vulkan/VideoOut adapter used
// by the front-end. The callback implementation owns command buffers, textures
// and swapchain synchronization; this session owns only game timing/state.
class WorldSession final {
public:
    WorldSession(VulkanVideoOutAdapter& adapter,
                 khdays::ps5::frontend::Config config)
        : adapter_(adapter), config_(std::move(config)) {}

    [[nodiscard]] bool start(std::string* error = nullptr);
    [[nodiscard]] bool frame();
    void shutdown();
    [[nodiscard]] const khdays::ps5::world::FirstWorldRuntime& world() const {
        return world_;
    }

private:
    static khdays::ps5::world::Command command_for(
        khdays::ps5::frontend::Input input);

    VulkanVideoOutAdapter& adapter_;
    khdays::ps5::frontend::Config config_;
    khdays::ps5::world::FirstWorldRuntime world_;
    bool started_ = false;
};

}  // namespace khdays::platform::ps5
