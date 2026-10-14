#pragma once

#include <cstdint>
#include <string>

#include "frontend_adapter.h"

namespace khdays::platform::ps5 {

// Owns the platform-neutral core and drives one original-rate front-end frame
// through the concrete PS5 adapter. No SDK headers are needed here; only the
// eventual adapter implementation knows about VideoOut/Vulkan/controls.
class FrontendSession final {
public:
    FrontendSession(FrontendAdapter& adapter,
                    khdays::ps5::frontend::Config config,
                    const khdays::ps5::frontend::ResourceProvider& resources);

    [[nodiscard]] bool start(std::string* error = nullptr);
    [[nodiscard]] bool frame();
    void shutdown();

    [[nodiscard]] khdays::ps5::frontend::Core& core() noexcept { return core_; }
    [[nodiscard]] const khdays::ps5::frontend::Core& core() const noexcept {
        return core_;
    }

private:
    FrontendAdapter& adapter_;
    khdays::ps5::frontend::Config config_;
    khdays::ps5::frontend::Core core_;
    bool started_ = false;
};

}  // namespace khdays::platform::ps5
