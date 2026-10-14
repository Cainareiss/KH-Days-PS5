#include "frontend_session.h"

#include <cmath>
#include <utility>

namespace khdays::platform::ps5 {

FrontendSession::FrontendSession(
    FrontendAdapter& adapter, khdays::ps5::frontend::Config config,
    const khdays::ps5::frontend::ResourceProvider& resources)
    : adapter_(adapter), config_(std::move(config)), core_(config_, resources) {}

bool FrontendSession::start(std::string* error) {
    if (!adapter_.initialize(static_cast<int>(config_.output_width),
                             static_cast<int>(config_.output_height))) {
        if (error != nullptr) {
            *error = "PS5 frontend adapter initialization failed";
        }
        return false;
    }
    if (!core_.start(khdays::ps5::frontend::SceneId::BootLogo, error)) {
        adapter_.shutdown();
        return false;
    }
    started_ = true;
    return true;
}

bool FrontendSession::frame() {
    if (!started_) {
        return false;
    }
    core_.tick(adapter_.poll_input());
    core_.render(adapter_);
    adapter_.present();
    const auto duration_ns = static_cast<std::int64_t>(std::llround(
        1'000'000'000.0 / config_.refresh_hz));
    adapter_.wait_for_next_frame(duration_ns);
    return core_.running();
}

void FrontendSession::shutdown() {
    if (started_) {
        adapter_.shutdown();
        started_ = false;
    }
}

}  // namespace khdays::platform::ps5
