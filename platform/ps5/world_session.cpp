#include "world_session.h"

#include <cmath>
#include <utility>

namespace khdays::platform::ps5 {

bool WorldSession::start(std::string* error) {
    if (!adapter_.initialize(static_cast<int>(config_.output_width),
                             static_cast<int>(config_.output_height))) {
        if (error != nullptr) {
            *error = adapter_.last_error();
        }
        return false;
    }
    world_.start();
    started_ = true;
    return true;
}

khdays::ps5::world::Command WorldSession::command_for(
    const khdays::ps5::frontend::Input input) {
    using khdays::ps5::frontend::Button;
    using khdays::ps5::world::Command;
    if (input.has(Button::Left)) return Command::MoveLeft;
    if (input.has(Button::Right)) return Command::MoveRight;
    if (input.has(Button::Up)) return Command::MoveUp;
    if (input.has(Button::Down)) return Command::MoveDown;
    if (input.has(Button::Start)) return Command::SkipCutscene;
    if (input.has(Button::Confirm)) return Command::Attack;
    return Command::None;
}

bool WorldSession::frame() {
    if (!started_) {
        return false;
    }
    world_.tick(command_for(adapter_.poll_input()));
    world_.render(adapter_);
    adapter_.present();
    const auto duration_ns = static_cast<std::int64_t>(std::llround(
        1'000'000'000.0 / config_.refresh_hz));
    adapter_.wait_for_next_frame(duration_ns);
    return adapter_.frame_ok() && !world_.finished();
}

void WorldSession::shutdown() {
    if (started_) {
        adapter_.shutdown();
        started_ = false;
    }
}

}  // namespace khdays::platform::ps5
