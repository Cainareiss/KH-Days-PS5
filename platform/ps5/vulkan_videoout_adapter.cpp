#include "vulkan_videoout_adapter.h"

namespace khdays::platform::ps5 {

bool VulkanVideoOutAdapter::initialize(const int width, const int height) {
    if (initialized_ || width <= 0 || height <= 0 || hooks_.initialize == nullptr) {
        fail("Vulkan/VideoOut initialize hook is unavailable");
        return false;
    }
    initialized_ = hooks_.initialize(hooks_.user,
                                     static_cast<std::uint32_t>(width),
                                     static_cast<std::uint32_t>(height));
    if (!initialized_) {
        fail("Vulkan/VideoOut initialization failed");
    }
    return initialized_;
}

khdays::ps5::frontend::Input VulkanVideoOutAdapter::poll_input() {
    if (!initialized_ || hooks_.poll_input == nullptr) {
        return {};
    }
    return hooks_.poll_input(hooks_.user);
}

void VulkanVideoOutAdapter::draw(const khdays::ps5::frontend::Frame& frame) {
    if (!initialized_ || hooks_.draw_frame == nullptr) {
        frame_ok_ = false;
        fail("Vulkan draw hook is unavailable");
        return;
    }
    frame_ok_ = hooks_.draw_frame(hooks_.user, frame);
    if (!frame_ok_) {
        fail("Vulkan frame recording failed");
    }
}

void VulkanVideoOutAdapter::draw_world(
    const khdays::ps5::world::WorldFrame& frame) {
    if (!initialized_ || hooks_.draw_world == nullptr) {
        frame_ok_ = false;
        fail("Vulkan world draw hook is unavailable");
        return;
    }
    frame_ok_ = hooks_.draw_world(hooks_.user, frame);
    if (!frame_ok_) {
        fail("Vulkan world frame recording failed");
    }
}

void VulkanVideoOutAdapter::present() {
    if (!initialized_ || hooks_.present == nullptr) {
        frame_ok_ = false;
        fail("VideoOut present hook is unavailable");
        return;
    }
    frame_ok_ = hooks_.present(hooks_.user);
    if (!frame_ok_) {
        fail("VideoOut present failed");
    }
}

void VulkanVideoOutAdapter::wait_for_next_frame(const std::int64_t duration_ns) {
    if (initialized_ && hooks_.wait_for_next_frame != nullptr) {
        hooks_.wait_for_next_frame(hooks_.user, duration_ns);
    }
}

void VulkanVideoOutAdapter::shutdown() {
    if (initialized_ && hooks_.shutdown != nullptr) {
        hooks_.shutdown(hooks_.user);
    }
    initialized_ = false;
}

}  // namespace khdays::platform::ps5
