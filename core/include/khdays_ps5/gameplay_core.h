#pragma once

#include <cstdint>
#include <vector>

namespace khdays::ps5::gameplay {

enum class Command : std::uint8_t {
    None,
    Attack,
    Guard,
    Cancel,
};

struct PlayerStats final {
    std::uint16_t level = 1;
    std::uint32_t experience = 0;
    std::int32_t hp = 100;
    std::int32_t max_hp = 100;
    std::uint32_t munny = 0;
};

struct MissionState final {
    std::uint32_t id = 1;
    std::uint32_t objective_target = 3;
    std::uint32_t objective_progress = 0;
    bool complete = false;
};

struct CombatState final {
    bool active = false;
    std::int32_t enemy_hp = 0;
    std::int32_t enemy_max_hp = 0;
    std::uint32_t combo = 0;
    bool guarding = false;
};

struct HudState final {
    PlayerStats player{};
    MissionState mission{};
    CombatState combat{};
};

// Compact, versioned representation used by the platform save provider. It
// intentionally contains no pointers or strings, so it is endian-stable after
// the magic/version checks in encode_save/decode_save.
struct SaveData final {
    std::uint32_t version = 1;
    PlayerStats player{};
    MissionState mission{};
};

[[nodiscard]] std::vector<std::uint8_t> encode_save(const SaveData& save);
[[nodiscard]] bool decode_save(const std::vector<std::uint8_t>& bytes,
                               SaveData& save);

class GameplayCore final {
public:
    void start_mission(std::uint32_t mission_id,
                       std::uint32_t objective_target = 3);
    void begin_encounter(std::int32_t enemy_hp);
    void tick(Command command);

    [[nodiscard]] const HudState& hud() const noexcept { return hud_; }
    [[nodiscard]] SaveData save() const noexcept;
    [[nodiscard]] bool load(const SaveData& save) noexcept;

private:
    void attack();
    void finish_enemy();
    HudState hud_{};
};

}  // namespace khdays::ps5::gameplay
