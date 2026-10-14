#include "khdays_ps5/gameplay_core.h"

#include <algorithm>

namespace khdays::ps5::gameplay {

namespace {

constexpr std::uint32_t kMagic = 0x4b485356;  // KHSV
constexpr std::size_t kEncodedSize = 44;

template <typename T>
void put(std::vector<std::uint8_t>& bytes, std::size_t& at, const T value) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        bytes[at++] = static_cast<std::uint8_t>(value >> (i * 8));
    }
}

template <typename T>
bool get(const std::vector<std::uint8_t>& bytes, std::size_t& at, T& value) {
    if (at + sizeof(T) > bytes.size()) {
        return false;
    }
    value = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        value |= static_cast<T>(bytes[at++]) << (i * 8);
    }
    return true;
}

}  // namespace

std::vector<std::uint8_t> encode_save(const SaveData& save) {
    std::vector<std::uint8_t> bytes(kEncodedSize, 0);
    std::size_t at = 0;
    put(bytes, at, kMagic);
    put(bytes, at, save.version);
    put(bytes, at, save.player.level);
    put(bytes, at, save.player.experience);
    put(bytes, at, save.player.hp);
    put(bytes, at, save.player.max_hp);
    put(bytes, at, save.player.munny);
    put(bytes, at, save.mission.id);
    put(bytes, at, save.mission.objective_target);
    put(bytes, at, save.mission.objective_progress);
    put(bytes, at, static_cast<std::uint8_t>(save.mission.complete));
    return bytes;
}

bool decode_save(const std::vector<std::uint8_t>& bytes, SaveData& save) {
    if (bytes.size() != kEncodedSize) {
        return false;
    }
    std::size_t at = 0;
    std::uint32_t magic = 0;
    std::uint8_t complete = 0;
    if (!get(bytes, at, magic) || magic != kMagic
        || !get(bytes, at, save.version) || save.version != 1
        || !get(bytes, at, save.player.level)
        || !get(bytes, at, save.player.experience)
        || !get(bytes, at, save.player.hp)
        || !get(bytes, at, save.player.max_hp)
        || !get(bytes, at, save.player.munny)
        || !get(bytes, at, save.mission.id)
        || !get(bytes, at, save.mission.objective_target)
        || !get(bytes, at, save.mission.objective_progress)
        || !get(bytes, at, complete)) {
        return false;
    }
    save.mission.complete = complete != 0;
    return save.player.max_hp > 0 && save.player.hp >= 0
        && save.player.hp <= save.player.max_hp
        && save.mission.objective_progress <= save.mission.objective_target;
}

void GameplayCore::start_mission(const std::uint32_t mission_id,
                                  const std::uint32_t objective_target) {
    hud_.mission = MissionState{mission_id, objective_target, 0, false};
    hud_.combat = {};
}

void GameplayCore::begin_encounter(const std::int32_t enemy_hp) {
    hud_.combat.active = enemy_hp > 0;
    hud_.combat.enemy_hp = std::max(0, enemy_hp);
    hud_.combat.enemy_max_hp = std::max(0, enemy_hp);
    hud_.combat.combo = 0;
    hud_.combat.guarding = false;
}

void GameplayCore::tick(const Command command) {
    hud_.combat.guarding = command == Command::Guard;
    if (command == Command::Attack && hud_.combat.active) {
        attack();
    }
}

void GameplayCore::attack() {
    ++hud_.combat.combo;
    hud_.combat.enemy_hp = std::max(0, hud_.combat.enemy_hp - 10);
    if (hud_.combat.enemy_hp == 0) {
        finish_enemy();
    }
}

void GameplayCore::finish_enemy() {
    hud_.combat.active = false;
    hud_.player.experience += 25;
    hud_.player.munny += 10;
    if (hud_.mission.objective_progress < hud_.mission.objective_target) {
        ++hud_.mission.objective_progress;
    }
    hud_.mission.complete = hud_.mission.objective_progress
        >= hud_.mission.objective_target;
}

SaveData GameplayCore::save() const noexcept {
    return SaveData{1, hud_.player, hud_.mission};
}

bool GameplayCore::load(const SaveData& save_data) noexcept {
    if (save_data.version != 1 || save_data.player.max_hp <= 0
        || save_data.player.hp < 0 || save_data.player.hp > save_data.player.max_hp
        || save_data.mission.objective_progress > save_data.mission.objective_target) {
        return false;
    }
    hud_.player = save_data.player;
    hud_.mission = save_data.mission;
    hud_.combat = {};
    return true;
}

}  // namespace khdays::ps5::gameplay
