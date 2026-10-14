#include "khdays_ps5/gameplay_core.h"
#include "khdays_ps5/world_runtime.h"

#include <cstdio>

namespace {
class HeadlessWorldRenderer final : public khdays::ps5::world::Renderer {
public:
    void draw_world(const khdays::ps5::world::WorldFrame& frame) override {
        last = &frame;
    }
    const khdays::ps5::world::WorldFrame* last = nullptr;
};
}

// Minimal Prospero entrypoint. This is intentionally a deterministic runtime
// smoke target: it exercises gameplay/combat/HUD/mission/save code without
// pretending that Vulkan or proprietary game assets are already linked.
int main() {
    khdays::ps5::gameplay::GameplayCore game;
    game.start_mission(1, 1);
    game.begin_encounter(10);
    game.tick(khdays::ps5::gameplay::Command::Attack);

    const auto saved = game.save();
    const auto encoded = khdays::ps5::gameplay::encode_save(saved);
    khdays::ps5::gameplay::SaveData restored;
    if (!khdays::ps5::gameplay::decode_save(encoded, restored)) {
        std::printf("KH-Days PS5 runtime: save validation failed\n");
        return 1;
    }
    khdays::ps5::world::FirstWorldRuntime world;
    HeadlessWorldRenderer renderer;
    world.start();
    world.tick(khdays::ps5::world::Command::SkipCutscene);
    for (int move = 0; move < 8; ++move) {
        world.tick(khdays::ps5::world::Command::MoveRight);
    }
    for (int enemy = 0; enemy < 3; ++enemy) {
        for (int hit = 0; hit < 3; ++hit) {
            world.tick(khdays::ps5::world::Command::Attack);
        }
        for (int move = 0; move < 12; ++move) {
            world.tick(khdays::ps5::world::Command::MoveRight);
        }
    }
    world.render(renderer);
    std::printf("KH-Days PS5 runtime: world=%s mission=%u hp=%d/%d complete=%d\n",
                renderer.last != nullptr ? renderer.last->world_name.c_str() : "none",
                restored.mission.id, restored.player.hp, restored.player.max_hp,
                restored.mission.complete ? 1 : 0);
    return restored.mission.complete && world.finished() ? 0 : 1;
}
