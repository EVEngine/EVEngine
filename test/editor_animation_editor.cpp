#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation_editor/AnimationEditorModule.h"
#include "animation_editor/EditorAnimationClip.h"
#include "common/Capability.h"
#include "common/EditorAutomation.h"
#include "editor/Editor.h"

using namespace eve::editor;

TEST_CASE("editor.animation.clip_automation_owns_headless_targets") {
    Editor editor;
    eve::animation_editor::AnimationEditorModule adapter;
    auto* automation = eve::cap::query<eve::IEditorAutomation>();
    REQUIRE(automation != nullptr);

    const std::string commands = automation->invoke("commands", "{}");
    CHECK(commands.find("animation.clip.settings.set.v1") != std::string::npos);
    CHECK(commands.find("animation.clip.track.set.v1") != std::string::npos);

    const std::string created =
        automation->invoke("target-create", R"({"target":"agent.clip","type":"animation-clip"})");
    REQUIRE(created.find("\"status\":\"applied\"") != std::string::npos);

    const std::string settings = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.settings.set.v1","payload":{"duration":2,"sampleRate":60,"loop":true}})");
    REQUIRE(settings.find("\"status\":\"applied\"") != std::string::npos);

    const std::string tracked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.track.set.v1","payload":{"id":"hips-track","bone":"Hips","keys":[{"id":"start","time":0,"px":0,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1},{"id":"end","time":2,"px":4,"py":0,"pz":0,"rx":0,"ry":0,"rz":0,"rw":1,"sx":1,"sy":1,"sz":1}]}})");
    REQUIRE(tracked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string masked = automation->invoke(
        "execute",
        R"({"target":"agent.clip","command":"animation.clip.mask.set.v1","payload":{"bone":"Hips","weight":0.25}})");
    REQUIRE(masked.find("\"status\":\"applied\"") != std::string::npos);

    const std::string inspected = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(inspected.find("animation-clip-document") != std::string::npos);
    CHECK(inspected.find("hips-track") != std::string::npos);
    CHECK(inspected.find("Hips") != std::string::npos);

    const std::string undone = automation->invoke("undo", R"({"target":"agent.clip"})");
    CHECK(undone.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterUndo = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterUndo.find("0.25") == std::string::npos);

    const std::string closed = automation->invoke("target-close", R"({"target":"agent.clip"})");
    CHECK(closed.find("\"status\":\"applied\"") != std::string::npos);
    const std::string afterClose = automation->invoke("inspect", R"({"target":"agent.clip"})");
    CHECK(afterClose.find("not-found") != std::string::npos);
}
