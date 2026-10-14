#include "khdays_ps5/frontend_core.h"
#include "khdays_ps5/gameplay_core.h"
#include "platform/ps5/frontend_session.h"

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

    bool read(const std::string_view path,
              std::vector<std::uint8_t>& bytes) const override {
        if (!contains(path)) {
            return false;
        }
        bytes = {0x4b, 0x48, 0x44, 0x53};
        return true;
    }
};

class TestRenderer final : public Renderer {
public:
    void draw(const Frame& frame) override { last = frame; ++draws; }
    Frame last{};
    int draws = 0;
};

class TestAdapter final : public khdays::platform::ps5::FrontendAdapter {
public:
    bool initialize(const int width, const int height) override {
        initialized = true;
        output_width = width;
        output_height = height;
        return true;
    }
    Input poll_input() override {
        return frame_count++ == 0 ? Input::one(Button::Start) : Input{};
    }
    void draw(const Frame& frame) override { last = frame; ++draws; }
    void present() override { ++presents; }
    void wait_for_next_frame(const std::int64_t ns) override { last_wait_ns = ns; }
    void shutdown() override { shutdown_called = true; }

    bool initialized = false;
    bool shutdown_called = false;
    int output_width = 0;
    int output_height = 0;
    int frame_count = 0;
    int draws = 0;
    int presents = 0;
    std::int64_t last_wait_ns = 0;
    Frame last{};
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
    assert(renderer.last.assets.size() == 1);
    assert(renderer.last.assets.front().loaded);
    assert(renderer.last.assets.front().bytes.size() == 4);
    assert(renderer.last.output_width == 3840);
    assert(renderer.last.output_height == 2160);
    assert(renderer.last.viewport.x == 512);
    assert(renderer.last.viewport.y == 24);
    assert(renderer.last.viewport.width == 2816);
    assert(renderer.last.viewport.height == 2112);
    assert(renderer.last.viewport.integer_scale == 11);

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

    Config session_config;
    session_config.boot_pair_frames = 1;
    TestAdapter adapter;
    khdays::platform::ps5::FrontendSession session(adapter, session_config,
                                                   resources);
    assert(session.start(&error));
    assert(session.frame());
    assert(adapter.initialized && adapter.presents == 1);
    assert(adapter.output_width == 3840 && adapter.output_height == 2160);
    assert(adapter.last.scene == SceneId::Title);
    assert(adapter.last_wait_ns > 0);
    session.shutdown();
    assert(adapter.shutdown_called);

    khdays::ps5::gameplay::GameplayCore gameplay;
    gameplay.start_mission(42, 1);
    gameplay.begin_encounter(10);
    gameplay.tick(khdays::ps5::gameplay::Command::Attack);
    assert(gameplay.hud().mission.complete);
    const auto encoded = khdays::ps5::gameplay::encode_save(gameplay.save());
    khdays::ps5::gameplay::SaveData restored;
    assert(khdays::ps5::gameplay::decode_save(encoded, restored));
    assert(restored.mission.id == 42);
    return 0;
}
