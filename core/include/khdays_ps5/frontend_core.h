#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace khdays::ps5::frontend {

// These IDs match the scene IDs used by the PC runtime. Keeping the numbers
// stable lets the PS5 adapter reuse the same save/title/mission flow.
enum class SceneId : std::int32_t {
    None = 0,
    BootLogo = 1,
    Gameplay = 2,
    DayTransition = 5,
    Title = 7,
    Opening = 11,
    Continue = 12,
    MainMenu = 19,
    SaveFile = 100,
};

enum class Button : std::uint8_t {
    Confirm,
    Cancel,
    Start,
    Up,
    Down,
    Left,
    Right,
};

struct Input final {
    std::uint32_t pressed = 0;

    [[nodiscard]] bool has(const Button button) const noexcept;
    static Input one(Button button) noexcept;
};

struct Config final {
    std::string language = "en";
    std::uint32_t virtual_width = 256;
    std::uint32_t virtual_height = 192;
    double refresh_hz = 59.8261;
    std::uint32_t boot_pair_frames = 91;
    std::uint32_t title_intro_frames = 0;
    std::uint32_t opening_frames = 18'000;
};

struct ResourceRef final {
    std::string path;
    std::string kind;
    bool required = true;
};

class ResourceProvider {
public:
    virtual ~ResourceProvider() = default;
    [[nodiscard]] virtual bool contains(std::string_view path) const = 0;
};

struct Frame final {
    SceneId scene = SceneId::None;
    std::uint64_t scene_frame = 0;
    int selection = 0;
    std::uint32_t virtual_width = 256;
    std::uint32_t virtual_height = 192;
    std::vector<ResourceRef> resources;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual void draw(const Frame& frame) = 0;
};

// Platform-neutral front-end state machine. SDL, VideoOut/Vulkan, input and
// filesystem code stay outside this class; only the scene/resource contract is
// shared with the PC implementation.
class Core final {
public:
    Core(Config config, const ResourceProvider& resources);

    [[nodiscard]] bool start(SceneId first = SceneId::BootLogo,
                              std::string* error = nullptr);
    void tick(Input input = {});
    void render(Renderer& renderer) const;

    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] SceneId scene() const noexcept { return scene_; }
    [[nodiscard]] std::uint64_t scene_frame() const noexcept {
        return scene_frame_;
    }
    [[nodiscard]] int selection() const noexcept { return selection_; }
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    [[nodiscard]] std::vector<ResourceRef> scene_resources() const;

private:
    [[nodiscard]] static std::vector<ResourceRef> resources_for(
        SceneId scene, std::string_view language);
    bool enter(SceneId next, std::string* error = nullptr);
    bool validate_resources(SceneId scene, std::string* error) const;
    void title_confirm();
    void save_confirm();
    void menu_confirm();

    Config config_;
    const ResourceProvider& resources_;
    SceneId scene_ = SceneId::None;
    std::uint64_t scene_frame_ = 0;
    int selection_ = 0;
    int title_level_ = 0;  // 0=root, 1=story, 2=mission
    bool running_ = false;
};

[[nodiscard]] const char* scene_name(SceneId scene) noexcept;

}  // namespace khdays::ps5::frontend
