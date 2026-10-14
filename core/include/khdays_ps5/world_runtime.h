#pragma once

#include "khdays_ps5/gameplay_core.h"

#include <cstdint>
#include <string>
#include <vector>

namespace khdays::ps5::world {

enum class Command : std::uint8_t {
    None,
    MoveLeft,
    MoveRight,
    MoveUp,
    MoveDown,
    Attack,
    SkipCutscene,
    Save,
};

struct Vec2 final {
    float x = 0.0f;
    float y = 0.0f;
};

struct EnemyView final {
    std::uint32_t id = 0;
    Vec2 position{};
    std::int32_t hp = 0;
    bool defeated = false;
};

struct CutsceneEvent final {
    std::uint32_t frame = 0;
    std::string speaker;
    std::string subtitle_ptbr;
};

struct WorldFrame final {
    std::string world_name;
    std::uint32_t room = 0;
    Vec2 player{};
    std::vector<EnemyView> enemies;
    // Original extracted assets consumed by the PS5 draw callback. The
    // callback resolves these under /app0 and uploads/decodes them on demand.
    std::vector<std::string> asset_paths;
    gameplay::HudState hud{};
    bool cutscene_active = false;
    std::uint32_t cutscene_frame = 0;
    const CutsceneEvent* cutscene_event = nullptr;
    bool save_requested = false;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void draw_world(const WorldFrame& frame) = 0;
};

// First playable vertical slice. It deliberately owns no Vulkan/SDL state;
// the PS5 adapter only receives WorldFrame snapshots to draw.
class FirstWorldRuntime final {
public:
    void start();
    void tick(Command command);
    void render(Renderer& renderer) const;

    [[nodiscard]] bool finished() const noexcept { return finished_; }
    [[nodiscard]] const WorldFrame& frame() const noexcept { return frame_; }
    [[nodiscard]] gameplay::SaveData save() const noexcept;
    [[nodiscard]] bool load(const gameplay::SaveData& save) noexcept;

private:
    void begin_cutscene();
    void finish_cutscene();
    void refresh_frame();
    void attack_nearest();

    WorldFrame frame_{};
    gameplay::GameplayCore gameplay_{};
    std::vector<CutsceneEvent> cutscene_{};
    std::uint32_t cutscene_frame_ = 0;
    std::uint32_t next_enemy_ = 0;
    bool finished_ = false;
};

}  // namespace khdays::ps5::world
