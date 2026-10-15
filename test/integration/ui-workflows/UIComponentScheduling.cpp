#include <imgui.h>
#include <stdexcept>
#include "ui/Component.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/unittest.h"

namespace {
class Frame {
public:
    ImGuiContext *previous = ImGui::GetCurrentContext();
    ImGuiContext *context  = ImGui::CreateContext();
    Frame() {
        ImGui::SetCurrentContext(context);
        auto &io              = ImGui::GetIO();
        io.DisplaySize        = ImVec2(800.f, 600.f);
        io.IniFilename        = nullptr;
        unsigned char *pixels = nullptr;
        int            width = 0, height = 0;
        io.KeyMap[ImGuiKey_Space] = 32;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    }
    ~Frame() {
        ImGui::DestroyContext(context);
        if (previous) ImGui::SetCurrentContext(previous);
    }
    void render() {
        ImGui::NewFrame();
        try {
            eve::ui::UISystem::render();
        } catch (...) {
            ImGui::EndFrame();
            throw;
        }
        ImGui::EndFrame();
    }
};
class Panel final : public eve::ui::Component {
public:
    int                 builds          = 0;
    bool                editDuringBuild = false;
    bool                fail            = false;
    eve::ui::WidgetDesc build() override {
        ++builds;
        if (editDuringBuild) {
            editDuringBuild = false;
            markDirty();
            eve::ui::detail::flushPendingComponents();
        }
        if (fail) throw std::runtime_error("injected component failure");
        return eve::ui::text("published", "label");
    }
};
}  // namespace
TEST_CASE("UI.componentScheduling.coalescesAndPreservesReentrantEdits") {
    Frame frame;
    Panel panel;
    panel.mountAs("scheduled-ui");
    frame.render();
    CHECK_EQ(panel.builds, 1);
    panel.editDuringBuild = true;
    for (int i = 0; i < 20; ++i) panel.markDirty();
    frame.render();
    CHECK_EQ(panel.builds, 2);
    CHECK(panel.isDirty());
    frame.render();
    CHECK_EQ(panel.builds, 3);
    CHECK(!panel.isDirty());
    frame.render();
    CHECK_EQ(panel.builds, 3);
    {
        Panel destroyed;
        destroyed.mountAs("destroyed-ui");
        destroyed.markDirty();
    }
    frame.render();
}
TEST_CASE("UI.componentScheduling.failedBatchRetainsRemainingWork") {
    Frame frame;
    Panel first, second;
    first.mountAs("failed-ui");
    second.mountAs("remaining-ui");
    frame.render();
    first.fail = true;
    first.markDirty();
    second.markDirty();
    bool threw = false;
    try {
        frame.render();
    } catch (const std::runtime_error &) {
        threw = true;
    }
    CHECK(threw);
    CHECK(first.isDirty());
    CHECK(second.isDirty());
    first.fail = false;
    frame.render();
    CHECK(!first.isDirty());
    CHECK(!second.isDirty());
    CHECK_EQ(second.builds, 2);
}

#include "ScriptTest.h"

UnitSciptTest(UIScheduledScriptTest, R"SQ(
function verifyScheduledComponent() {
    ::ui <- eve.UI()
    class Scheduled extends eve.UIComponent {
        builds = 0
        fail = false
        edit = false
        function build() {
            builds += 1
            if (edit) { edit = false; markDirty(); eve_ui_flush_components() }
            if (fail) throw "injected script publication failure"
            this.ui().text("published", "script-label")
        }
    }
    local first = Scheduled(eve.UI())
    local second = Scheduled(eve.UI())
    first.mountAs("script-schedule-first")
    second.mountAs("script-schedule-second")
    eve_ui_flush_components()
    if (first.builds != 1 || second.builds != 1) return false
    first.edit = true
    for (local i = 0; i < 20; ++i) first.markDirty()
    eve_ui_flush_components()
    if (first.builds != 2 || !first.dirty) return false
    eve_ui_flush_components()
    if (first.builds != 3 || first.dirty) return false
    first.fail = true
    first.markDirty()
    second.markDirty()
    local threw = false
    try { eve_ui_flush_components() } catch (error) { threw = true }
    if (!threw || !first.dirty || !second.dirty) return false
    first.fail = false
    eve_ui_flush_components()
    return !first.dirty && !second.dirty && second.builds == 2
}
)SQ");
TEST_CASE_FIXTURE(UIScheduledScriptTest, "UI.componentScheduling.scriptCoalescingAndFailureRecovery") {
    CHECK(vm.callFunc(vm.findFunc("verifyScheduledComponent"), vm).toBool());
}
