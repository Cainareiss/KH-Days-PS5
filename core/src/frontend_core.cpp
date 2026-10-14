#include "khdays_ps5/frontend_core.h"

#include <algorithm>
#include <utility>

namespace khdays::ps5::frontend {

namespace {

constexpr std::uint32_t bit(const Button button) {
    return 1U << static_cast<unsigned>(button);
}

std::string localize_path(std::string path, std::string_view language) {
    const auto marker = path.find('&');
    if (marker != std::string::npos) {
        path.replace(marker, 1, language);
    }
    return path;
}

}  // namespace

bool Input::has(const Button button) const noexcept {
    return (pressed & bit(button)) != 0U;
}

Input Input::one(const Button button) noexcept {
    return Input{bit(button)};
}

Core::Core(Config config, const ResourceProvider& resources)
    : config_(std::move(config)), resources_(resources) {}

std::vector<ResourceRef> Core::resources_for(const SceneId scene,
                                             const std::string_view language) {
    std::vector<ResourceRef> result;
    switch (scene) {
    case SceneId::BootLogo:
        result = {{"ttl/ttl.p2", "container", true}};
        break;
    case SceneId::Opening:
        result = {
            {"mv/802.mods", "movie", true},
            {"op/op.p2", "container", true},
            {"text/font_eu_10all.nftr", "font", true},
            {"db/db_&.p2", "messages", true},
        };
        break;
    case SceneId::Title:
        result = {
            {"ttl/ttl.p2", "container", true},
            {"ttl/ttl_&.p2", "localized-container", false},
            {"snd/sound_data.sdat", "audio", true},
        };
        break;
    case SceneId::MainMenu:
        result = {
            {"UI/mlt/res.p2", "container", true},
            {"UI/mlt/mlt_&.s.z", "localized-strings", true},
            {"text/font_eu_08.nftr", "font", true},
        };
        break;
    case SceneId::SaveFile:
        result = {
            {"UI/cm/cm_save.ui.z", "layout", true},
            {"UI/cm/cmo_&.p2", "localized-container", true},
        };
        break;
    case SceneId::None:
    case SceneId::Gameplay:
    case SceneId::DayTransition:
    case SceneId::Continue:
        break;
    }

    for (auto& resource : result) {
        resource.path = localize_path(std::move(resource.path), language);
    }
    return result;
}

std::vector<ResourceRef> Core::scene_resources() const {
    return resources_for(scene_, config_.language);
}

bool Core::validate_resources(const SceneId scene, std::string* error) const {
    const auto refs = resources_for(scene, config_.language);

    for (const auto& ref : refs) {
        if (ref.required && !resources_.contains(ref.path)) {
            if (error != nullptr) {
                *error = "missing required front-end resource: " + ref.path;
            }
            return false;
        }
    }
    return true;
}

bool Core::start(const SceneId first, std::string* error) {
    running_ = false;
    scene_ = SceneId::None;
    scene_frame_ = 0;
    selection_ = 0;
    title_level_ = 0;
    if (!enter(first, error)) {
        return false;
    }
    running_ = true;
    return true;
}

bool Core::enter(const SceneId next, std::string* error) {
    if (!validate_resources(next, error)) {
        return false;
    }
    scene_ = next;
    scene_frame_ = 0;
    selection_ = 0;
    return true;
}

void Core::title_confirm() {
    if (title_level_ == 0) {
        title_level_ = selection_ == 0 ? 1 : 2;
        selection_ = 0;
        return;
    }
    if (title_level_ == 1) {
        // The PC front-end enters the gameplay/day flow for New Game.
        enter(SceneId::Gameplay);
    } else if (selection_ == 0) {
        enter(SceneId::SaveFile);
    }
}

void Core::save_confirm() {
    enter(SceneId::MainMenu);
}

void Core::menu_confirm() {
    // The PC port exposes the 13-character grid; confirming a character hands
    // control to the gameplay scene.
    enter(SceneId::Gameplay);
}

void Core::tick(const Input input) {
    if (!running_) {
        return;
    }
    ++scene_frame_;

    switch (scene_) {
    case SceneId::BootLogo:
        if (input.has(Button::Confirm) || input.has(Button::Start)
            || scene_frame_ >= config_.boot_pair_frames * 3U) {
            enter(SceneId::Title);
        }
        break;
    case SceneId::Opening:
        if (input.has(Button::Confirm) || input.has(Button::Start)
            || scene_frame_ >= config_.opening_frames) {
            enter(SceneId::Gameplay);
        }
        break;
    case SceneId::Title: {
        if (scene_frame_ <= config_.title_intro_frames) {
            break;
        }
        const int count = title_level_ == 1 ? 1 : 2;
        if (input.has(Button::Down)) {
            selection_ = std::min(count - 1, selection_ + 1);
        }
        if (input.has(Button::Up)) {
            selection_ = std::max(0, selection_ - 1);
        }
        if (input.has(Button::Cancel) && title_level_ != 0) {
            title_level_ = 0;
            selection_ = 0;
        } else if (input.has(Button::Confirm) || input.has(Button::Start)) {
            title_confirm();
        }
        break;
    }
    case SceneId::SaveFile:
        if (input.has(Button::Down)) {
            selection_ = std::min(2, selection_ + 1);
        }
        if (input.has(Button::Up)) {
            selection_ = std::max(0, selection_ - 1);
        }
        if (input.has(Button::Confirm) || input.has(Button::Start)) {
            save_confirm();
        }
        break;
    case SceneId::MainMenu:
        if (input.has(Button::Down)) {
            selection_ = std::min(12, selection_ + 1);
        }
        if (input.has(Button::Up)) {
            selection_ = std::max(0, selection_ - 1);
        }
        if (input.has(Button::Confirm) || input.has(Button::Start)) {
            menu_confirm();
        }
        break;
    case SceneId::Gameplay:
    case SceneId::DayTransition:
    case SceneId::Continue:
    case SceneId::None:
        break;
    }
}

void Core::render(Renderer& renderer) const {
    Frame frame;
    frame.scene = scene_;
    frame.scene_frame = scene_frame_;
    frame.selection = selection_;
    frame.virtual_width = config_.virtual_width;
    frame.virtual_height = config_.virtual_height;
    frame.resources = scene_resources();
    renderer.draw(frame);
}

const char* scene_name(const SceneId scene) noexcept {
    switch (scene) {
    case SceneId::BootLogo: return "BootLogoScene";
    case SceneId::Gameplay: return "GameplayScene";
    case SceneId::DayTransition: return "DayTransitionScene";
    case SceneId::Title: return "TitleScene";
    case SceneId::Opening: return "OpeningScene";
    case SceneId::Continue: return "ContinueScene";
    case SceneId::MainMenu: return "MainMenuScene";
    case SceneId::SaveFile: return "SaveFileScene";
    case SceneId::None: return "None";
    }
    return "UnknownScene";
}

}  // namespace khdays::ps5::frontend
