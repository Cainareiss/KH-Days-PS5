#include "khdays_ps5/world_runtime.h"

#include <algorithm>
#include <cmath>

namespace khdays::ps5::world {

void FirstWorldRuntime::start() {
    frame_ = {};
    frame_.world_name = "Twilight Town";
    frame_.room = 0;
    frame_.player = {0.0f, 0.0f};
    frame_.asset_paths = {
        "mi/wd/wd_tt",
        "UI/btl/main.p2",
        "text/font_eu_08.nftr",
        "ba/ch/ro/def.p/slot_7/0000.nsbmd",
        "mi/mi/0001",
    };
    gameplay_.start_mission(1, 3);
    frame_.enemies = {
        {1, {2.0f, 0.0f}, 30, false},
        {2, {5.0f, 1.0f}, 30, false},
        {3, {8.0f, -1.0f}, 30, false},
    };
    next_enemy_ = 0;
    finished_ = false;
    begin_cutscene();
    refresh_frame();
}

void FirstWorldRuntime::begin_cutscene() {
    cutscene_ = {
        {0, "Narrador", "O dia começa em Twilight Town."},
        {90, "Roxas", "Mais um dia..."},
        {180, "Sistema", "Derrote os inimigos e avance pela missão."},
    };
    cutscene_frame_ = 0;
    frame_.cutscene_active = true;
}

void FirstWorldRuntime::finish_cutscene() {
    frame_.cutscene_active = false;
    frame_.cutscene_event = nullptr;
}

void FirstWorldRuntime::tick(const Command command) {
    if (finished_) {
        return;
    }
    frame_.save_requested = command == Command::Save;
    if (frame_.cutscene_active) {
        if (command == Command::SkipCutscene || ++cutscene_frame_ >= 240) {
            finish_cutscene();
        }
        refresh_frame();
        return;
    }

    constexpr float step = 0.25f;
    if (command == Command::MoveLeft) frame_.player.x -= step;
    if (command == Command::MoveRight) frame_.player.x += step;
    if (command == Command::MoveUp) frame_.player.y -= step;
    if (command == Command::MoveDown) frame_.player.y += step;
    frame_.player.x = std::clamp(frame_.player.x, -1.0f, 10.0f);
    frame_.player.y = std::clamp(frame_.player.y, -3.0f, 3.0f);
    if (command == Command::Attack) {
        attack_nearest();
    }
    finished_ = gameplay_.hud().mission.complete;
    refresh_frame();
}

void FirstWorldRuntime::attack_nearest() {
    for (auto& enemy : frame_.enemies) {
        if (enemy.defeated) {
            continue;
        }
        const float dx = enemy.position.x - frame_.player.x;
        const float dy = enemy.position.y - frame_.player.y;
        if (std::sqrt(dx * dx + dy * dy) > 2.0f) {
            continue;
        }
        gameplay_.begin_encounter(enemy.hp);
        gameplay_.tick(gameplay::Command::Attack);
        enemy.hp = gameplay_.hud().combat.enemy_hp;
        if (!gameplay_.hud().combat.active) {
            enemy.defeated = true;
            ++next_enemy_;
        }
        return;
    }
}

void FirstWorldRuntime::refresh_frame() {
    frame_.hud = gameplay_.hud();
    frame_.cutscene_event = nullptr;
    if (frame_.cutscene_active) {
        for (const auto& event : cutscene_) {
            if (event.frame <= cutscene_frame_) {
                frame_.cutscene_event = &event;
            }
        }
    }
}

void FirstWorldRuntime::render(Renderer& renderer) const {
    renderer.draw_world(frame_);
}

gameplay::SaveData FirstWorldRuntime::save() const noexcept {
    return gameplay_.save();
}

bool FirstWorldRuntime::load(const gameplay::SaveData& save_data) noexcept {
    const bool ok = gameplay_.load(save_data);
    finished_ = ok && gameplay_.hud().mission.complete;
    refresh_frame();
    return ok;
}

}  // namespace khdays::ps5::world
