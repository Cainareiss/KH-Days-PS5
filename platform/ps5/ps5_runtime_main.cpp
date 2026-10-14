#include "khdays_ps5/gameplay_core.h"

#include <cstdio>

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
    std::printf("KH-Days PS5 runtime: mission=%u hp=%d/%d complete=%d\n",
                restored.mission.id, restored.player.hp, restored.player.max_hp,
                restored.mission.complete ? 1 : 0);
    return restored.mission.complete ? 0 : 1;
}
