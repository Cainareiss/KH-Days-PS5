#include "khdays_ps5/frontend_core.h"

#include <cassert>
#include <string>
#include <unordered_set>

using namespace khdays::ps5::frontend;

class TestResources final : public ResourceProvider {
public:
    bool contains(const std::string_view path) const override {
        return path == "ttl/ttl.p2" || path == "snd/sound_data.sdat"
            || path == "UI/mlt/res.p2" || path == "UI/mlt/mlt_en.s.z"
            || path == "text/font_eu_08.nftr"
            || path == "UI/cm/cm_save.ui.z"
            || path == "UI/cm/cmo_en.p2";
    }
};

class TestRenderer final : public Renderer {
public:
    void draw(const Frame& frame) override { last = frame; ++draws; }
    Frame last{};
    int draws = 0;
};

int main() {
    Config config;
    config.boot_pair_frames = 1;
    TestResources resources;
    TestRenderer renderer;
    Core core(config, resources);
    std::string error;

    assert(core.start(SceneId::BootLogo, &error));
    core.render(renderer);
    assert(renderer.last.scene == SceneId::BootLogo);
    assert(renderer.last.resources.size() == 1);

    core.tick(Input::one(Button::Start));
    assert(core.scene() == SceneId::Title);
    core.tick(Input::one(Button::Down));
    core.tick(Input::one(Button::Confirm));
    assert(core.scene() == SceneId::Title);
    core.tick(Input::one(Button::Confirm));
    assert(core.scene() == SceneId::SaveFile);
    core.tick(Input::one(Button::Confirm));
    assert(core.scene() == SceneId::MainMenu);
    core.tick(Input::one(Button::Down));
    assert(core.selection() == 1);
    core.tick(Input::one(Button::Confirm));
    assert(core.scene() == SceneId::Gameplay);
    assert(renderer.draws == 1);
    return 0;
}
