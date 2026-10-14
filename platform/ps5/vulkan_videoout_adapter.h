#pragma once

#include "frontend_adapter.h"

#include <cstdint>

namespace khdays::platform::ps5 {

// The PS5 Vulkan template owns VkInstance/VkDevice/VkSwapchainKHR and the
// VideoOut surface. These hooks are the narrow seam between that real render
// loop and the portable front-end. A PS5 executable supplies callbacks after
// its VulkanExampleBase has created the device and swapchain.
struct VulkanVideoOutHooks final {
    void* user = nullptr;
    bool (*initialize)(void* user, std::uint32_t width,
                       std::uint32_t height) = nullptr;
    khdays::ps5::frontend::Input (*poll_input)(void* user) = nullptr;
    bool (*draw_frame)(void* user,
                       const khdays::ps5::frontend::Frame& frame) = nullptr;
    bool (*present)(void* user) = nullptr;
    void (*wait_for_next_frame)(void* user, std::int64_t duration_ns) = nullptr;
    void (*shutdown)(void* user) = nullptr;
};

class VulkanVideoOutAdapter final : public FrontendAdapter {
public:
    explicit VulkanVideoOutAdapter(VulkanVideoOutHooks hooks)
        : hooks_(hooks) {}

    bool initialize(int width, int height) override;
    khdays::ps5::frontend::Input poll_input() override;
    void draw(const khdays::ps5::frontend::Frame& frame) override;
    void present() override;
    void wait_for_next_frame(std::int64_t duration_ns) override;
    void shutdown() override;

    [[nodiscard]] bool initialized() const noexcept { return initialized_; }
    [[nodiscard]] bool frame_ok() const noexcept { return frame_ok_; }
    [[nodiscard]] const char* last_error() const noexcept { return last_error_; }

private:
    void fail(const char* message) noexcept { last_error_ = message; }

    VulkanVideoOutHooks hooks_{};
    bool initialized_ = false;
    bool frame_ok_ = true;
    const char* last_error_ = "";
};

}  // namespace khdays::platform::ps5
