#include "ui/HudGeometry.h"
#include "ui/UI.h"
#include "ui/UIBackend.h"
#include "ui/UISystem.h"
#include "ui/Widget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <cmath>
#include <limits>

namespace ui = eve::ui;

TEST_CASE("UI.hud.linearFillCropsUvsAndKeepsReverseEdge") {
    const auto left = ui::linearFillRect(.25f, ui::FillDirection::Left, false);
    CHECK_EQ(left.x, .75f);
    CHECK_EQ(left.w, .25f);
    CHECK_EQ(left.u0, .75f);
    CHECK_EQ(left.u1, 1.f);
    const auto up = ui::linearFillRect(.5f, ui::FillDirection::Up, true);
    CHECK_EQ(up.y, .5f);
    CHECK_EQ(up.h, .5f);
    CHECK_EQ(up.v0, 0.f);
    CHECK_EQ(up.v1, 1.f);
    CHECK_EQ(ui::linearFillRect(-1.f, ui::FillDirection::Right, false).w, 0.f);
    CHECK_EQ(ui::linearFillRect(2.f, ui::FillDirection::Down, false).h, 1.f);
    CHECK_EQ(ui::hudFraction(std::numeric_limits<float>::quiet_NaN()), 0.f);
}

TEST_CASE("UI.hud.radialFillDirectionRingAndBoundaryFractions") {
    CHECK_EQ(ui::radialFillMesh(0.f, -90.f, true, 0.f).count, 0);
    CHECK_EQ(ui::radialFillMesh(1.f, 0.f, true, 1.f).count, 0);
    const auto clockwise = ui::radialFillMesh(.25f, -90.f, true, .5f);
    const auto reverse   = ui::radialFillMesh(.25f, -90.f, false, .5f);
    REQUIRE(clockwise.count > 0);
    const auto& end  = clockwise.quads[std::size_t(clockwise.count - 1)];
    const auto& back = reverse.quads[std::size_t(reverse.count - 1)];
    CHECK(std::abs(end.outerX1 - 1.f) < 1e-5f);
    CHECK(std::abs(end.innerX1 - .75f) < 1e-5f);
    CHECK(std::abs(back.outerX1) < 1e-5f);
    CHECK_EQ(ui::radialFillMesh(1.f, -90.f, true, 0.f).count, 64);
}

TEST_CASE("UI.hud.canvasFitsSafeAreaAndPreservesUltrawideAnchors") {
    ui::HudCanvas canvas;
    canvas.enabled   = true;
    canvas.safeLeft  = 20.f;
    canvas.safeRight = 20.f;
    const auto fit   = ui::resolveHudCanvas(canvas, 1000.f, 540.f);
    CHECK_EQ(fit.x, 20.f);
    CHECK_EQ(fit.width, 960.f);
    CHECK_EQ(fit.scale, .5f);
    const auto wide = ui::resolveHudCanvas(canvas, 2600.f, 1080.f);
    CHECK_EQ(wide.width, 2560.f);
    CHECK_EQ(wide.scale, 1.f);
}

TEST_CASE("UI.hud.ninePatchScalePreservesSourceTexelBorders") {
    const auto axis = ui::ninePatchAxis(50.f, 8.f, 12.f, 0.f, 1.f, 100.f, .5f);
    CHECK_EQ(axis.nearSize, 4.f);
    CHECK_EQ(axis.farSize, 6.f);
    CHECK(std::abs(axis.nearUv - .08f) < 1e-5f);
    CHECK(std::abs(axis.farUv - .88f) < 1e-5f);
    const auto tiny = ui::ninePatchAxis(4.f, 8.f, 12.f, 0.f, 1.f, 100.f, .5f);
    CHECK_EQ(tiny.nearSize, 2.f);
    CHECK(std::abs(tiny.nearUv - .04f) < 1e-5f);
}

TEST_CASE("UI.hud.componentsReconcileValuesAndKeepInteractionOnSkillButton") {
    auto handle = ui::UIHost::createHost("hud-components");
    auto host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(
        ui::window("HUD", {ui::resourceBar("hp", 240.f, 24.f, .8f), ui::skillSlot("skill", 64.f, 0, .5f, "Q")}));
    auto value  = host->get().findById("hp/value");
    auto button = host->get().findById("skill/activate");
    auto mask   = host->get().findById("skill/cooldown");
    REQUIRE(value.has_value());
    REQUIRE(button.has_value());
    REQUIRE(mask.has_value());
    CHECK_EQ(value->get().value, .8f);
    CHECK_EQ(int(mask->get().mouseFilter), int(ui::MouseFilter::Ignore));
    CHECK_EQ(int(button->get().focusMode), int(ui::FocusMode::All));
    CHECK(!host->get().setTreeReconcile(
        ui::window("HUD", {ui::resourceBar("hp", 240.f, 24.f, .2f), ui::skillSlot("skill", 64.f, 0, 1.f, "Q")})));
    CHECK_EQ(host->get().findById("hp/value")->get().value, .2f);
    ecs::DestroyEntity(&host->get());
}

TEST_CASE("UI.hud.renderScalesHitRectAndRestoresDesignMetrics") {
    auto* saved           = ImGui::GetCurrentContext();
    auto* context         = ImGui::CreateContext();
    auto& io              = ImGui::GetIO();
    io.DisplaySize        = ImVec2(1000.f, 540.f);
    io.IniFilename        = nullptr;
    unsigned char* pixels = nullptr;
    int            width = 0, height = 0;
    for (int key = 0; key < ImGuiKey_COUNT; ++key) io.KeyMap[key] = key;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    auto handle = ui::UIHost::createHost("hud-scale-contract");
    auto host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::window("HUD", {ui::button("Skill", "button").withSize(100.f, 40.f)}));
    auto meta              = host->get().meta();
    meta->canvas.enabled   = true;
    meta->canvas.safeLeft  = 20.f;
    meta->canvas.safeRight = 20.f;
    meta->overlay          = true;
    meta->overlayBgAlpha   = 0.f;
    meta->hasPos           = true;
    meta->anchorX          = 1.f;
    meta->pivotX           = 1.f;
    meta->posX             = -20.f;
    meta->posY             = 20.f;
    meta->hasSize          = true;
    meta->sizeX            = 200.f;
    meta->sizeY            = 100.f;
    for (int frame = 0; frame < 6; ++frame) {
        meta->inputTransparent = frame >= 3;
        io.MousePos            = ImVec2(880.f, 15.f);
        io.MouseDown[0]        = frame == 1 || frame == 4;
        ImGui::NewFrame();
        ui::UISystem::render();
        CHECK(!ImGui::GetCurrentWindowRead()->WriteAccessed);
        auto* window = ImGui::FindWindowByName("HUD###hud-scale-contract/HUD");
        REQUIRE(window != nullptr);
        CHECK(std::abs(window->Pos.x - 870.f) < 1.f);
        CHECK(std::abs(window->Size.x - 100.f) < 1.f);
        // Last submitted item is the button, in the same physical space used for hit testing.
        CHECK(std::abs(window->DC.CursorMaxPos.x - window->Pos.x - 50.f) < 1.f);
        CHECK_EQ(host->get().findById("button")->get().sizeX, 100.f);
        CHECK_EQ(meta->posX, -20.f);
        CHECK_EQ(meta->sizeX, 200.f);
        ui::UISystem::dispatchEvents();
        const auto click = ui::UISystem::consumeClick();
        if (frame == 2)
            CHECK_EQ(click, std::string("hud-scale-contract/button"));
        else
            CHECK(click.empty());
        if (frame >= 4) CHECK(!io.WantCaptureMouse);
        ImGui::EndFrame();
    }
    ecs::DestroyEntity(&host->get());
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(saved);
}

TEST_CASE("UI.hud.treeJsonPreservesFillAndComponentTextStyle") {
    auto* module            = ui::UI::create();
    auto  fill              = ui::filledImage("reverse", 100.f, 24.f, .4f);
    fill.fillDirection      = ui::FillDirection::Up;
    fill.fillStretch        = true;
    auto radial             = ui::radialFill("ring", 80.f, 80.f, .7f);
    radial.fillStartDegrees = 45.f;
    radial.fillClockwise    = false;
    radial.fillInnerRadius  = .6f;
    REQUIRE(
        ui::UIHost::resolve(module->mountAs("json-hud", ui::window("HUD", {std::move(fill), std::move(radial),
                                                                           ui::resourceBar("hp", 200.f, 24.f, .8f)})))
            .has_value());
    const auto json       = module->saveTreeJson();
    auto       beforeLoad = ui::UIHost::resolve(module->current())->get().nodeHandle("ring");
    REQUIRE(beforeLoad.has_value());
    REQUIRE(module->loadTreeJson(json));
    CHECK(!ui::UIHost::resolveNode(*beforeLoad).has_value());
    auto host = ui::UIHost::resolve(module->current());
    REQUIRE(host.has_value());
    CHECK_EQ(int(host->get().findById("reverse")->get().fillDirection), int(ui::FillDirection::Up));
    CHECK(host->get().findById("reverse")->get().fillStretch);
    CHECK_EQ(host->get().findById("ring")->get().fillInnerRadius, .6f);
    CHECK(!host->get().findById("ring")->get().fillClockwise);
    CHECK_EQ(host->get().findById("hp/label")->get().fontSize, 14.f);
    CHECK_EQ(host->get().findById("hp/label")->get().textAlignX, .5f);
    auto afterLoad = host->get().nodeHandle("ring");
    REQUIRE(afterLoad.has_value());
    CHECK(!module->loadTreeJson(
        R"({"schema":"eve.ui.tree","version":5,"type":"radial-fill","id":"broken","fillInnerRadius":2})"));
    CHECK(host->get().findById("ring").has_value());
    CHECK(ui::UIHost::resolveNode(*afterLoad).has_value());
    ecs::DestroyEntity(&host->get());
}

namespace {
struct HudTextureBackend final : ui::UIBackend {
    bool  queued = true;
    int   calls  = 0;
    float x = 0.f, width = 0.f, u0 = 0.f, u1 = 0.f, alpha = 0.f;
    bool  init(SDL_Window*, eve::graphics::Graphics*) override { return true; }
    void  shutdown() override {}
    bool  isInitialized() const override { return true; }
    void  processEvent(const SDL_Event*) override {}
    void  newFrame() override {}
    bool  usesQueuedTextureDraws() const override { return queued; }
    bool  textureSize(uint64_t, int* w, int* h) const override {
        *w = 100;
        *h = 20;
        return true;
    }
    void* textureHandle(uint64_t) const override { return reinterpret_cast<void*>(uintptr_t{1}); }
    void  queueTextureDraw(uint64_t, float left, float, float w, float, float startU, float, float endU, float, float,
                           float, float, float a, bool) override {
        ++calls;
        x     = left;
        width = w;
        u0    = startU;
        u1    = endU;
        alpha = a;
    }
};
}  // namespace

TEST_CASE("UI.hud.texturedFillMatchesQueuedAndNativeDrawGeometry") {
    auto* saved           = ImGui::GetCurrentContext();
    auto  previousBackend = ui::UISystem::backend();
    auto* context         = ImGui::CreateContext();
    auto& io              = ImGui::GetIO();
    io.DisplaySize        = ImVec2(800.f, 600.f);
    io.IniFilename        = nullptr;
    unsigned char* pixels = nullptr;
    int            width = 0, height = 0;
    for (int key = 0; key < ImGuiKey_COUNT; ++key) io.KeyMap[key] = key;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    HudTextureBackend backend;
    ui::UISystem::setBackend(backend);
    auto handle = ui::UIHost::createHost("hud-texture-contract");
    auto host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    auto fill          = ui::filledImage("fill", 100.f, 20.f, .25f).withUv(.2f, .1f, .8f, .9f);
    fill.textureId     = 7;
    fill.fillDirection = ui::FillDirection::Left;
    fill.tintA         = .8f;
    host->get().setTree(ui::window("HUD", {std::move(fill)}));
    host->get().findById("fill")->get().opacity = .5f;
    auto meta                                   = host->get().meta();
    meta->hasPos                                = true;
    meta->posX                                  = 10.f;
    meta->posY                                  = 10.f;
    meta->hasSize                               = true;
    meta->sizeX                                 = 200.f;
    meta->sizeY                                 = 80.f;
    meta->overlay                               = true;
    meta->overlayBgAlpha                        = 0.f;
    for (int frame = 0; frame < 3; ++frame) {
        backend.queued = frame < 2;
        ImGui::NewFrame();
        ui::UISystem::render();
        if (frame == 1) {
            CHECK(backend.calls > 0);
            CHECK(std::abs(backend.x - 85.f) < 1.f);
            CHECK_EQ(backend.width, 25.f);
            CHECK(std::abs(backend.u0 - .65f) < 1e-5f);
            CHECK(std::abs(backend.u1 - .8f) < 1e-5f);
            CHECK(std::abs(backend.alpha - .4f) < 1e-5f);
        }
        if (frame == 2) {
            auto* window = ImGui::FindWindowByName("HUD###hud-texture-contract/HUD");
            REQUIRE(window != nullptr);
            bool found = false;
            for (const auto& vertex : window->DrawList->VtxBuffer)
                if (std::abs(vertex.pos.x - backend.x) < 1.f && std::abs(vertex.uv.x - backend.u0) < 1e-5f)
                    found = true;
            CHECK(found);
        }
        ImGui::EndFrame();
    }
    ecs::DestroyEntity(&host->get());
    if (previousBackend)
        ui::UISystem::setBackend(previousBackend->get());
    else
        ui::UISystem::clearBackend();
    ImGui::DestroyContext(context);
    ImGui::SetCurrentContext(saved);
}

TEST_CASE("UI.hud.nodeHandlesSurvivePropsButRejectReplacementAndDestroyedHosts") {
    auto handle = ui::UIHost::createHost("hud-node-generation");
    auto host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::resourceBar("hp", 240.f, 24.f, 1.f));
    auto node = host->get().nodeHandle("hp/value");
    REQUIRE(node.has_value());
    CHECK(!host->get().nodeHandle("missing").has_value());
    CHECK(!host->get().setTreeReconcile(ui::resourceBar("hp", 240.f, 24.f, .75f)));
    REQUIRE(ui::UIHost::resolveNode(*node).has_value());
    CHECK_EQ(ui::UIHost::resolveNode(*node)->get().value, .75f);
    host->get().setTree(ui::resourceBar("hp", 240.f, 24.f, .25f));
    CHECK(!ui::UIHost::resolveNode(*node).has_value());
    auto replacement = host->get().nodeHandle("hp/value");
    REQUIRE(replacement.has_value());
    auto outOfRange  = *replacement;
    outOfRange.index = std::numeric_limits<std::size_t>::max();
    CHECK(!ui::UIHost::resolveNode(outOfRange).has_value());
    ecs::DestroyEntity(&host->get());
    CHECK(!ui::UIHost::resolveNode(*replacement).has_value());
}

TEST_CASE("UI.hud.valueTweenRetargetDelayClampAndRebuildCancellation") {
    auto handle = ui::UIHost::createHost("hud-value-tween");
    auto host   = ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    host->get().setTree(ui::resourceBar("hp", 240.f, 24.f, 1.f));
    ui::UiTweenDriver driver;
    auto              value = [&] { return host->get().findById("hp/trail")->get().value; };
    driver.animateItemValue(handle, "hp/trail", 0.f, 100.f, "linear", 20.f, 0.).expect("start");
    driver.tick(10.);
    CHECK_EQ(value(), 1.f);
    driver.tick(70.);
    CHECK_EQ(value(), .5f);
    driver.animateItemValue(handle, "hp/trail", 1.f, 100.f, "linear", 0.f, 70.).expect("retarget");
    CHECK_EQ(driver.itemTweenCount(), 1u);
    driver.tick(120.);
    CHECK_EQ(value(), .75f);
    driver.tick(170.);
    CHECK_EQ(value(), 1.f);
    CHECK_EQ(driver.itemTweenCount(), 0u);
    driver.animateItemValue(handle, "hp/trail", -2.f, 0.f, "linear", 10.f, 200.).expect("delayed jump");
    driver.tick(205.);
    CHECK_EQ(value(), 1.f);
    driver.tick(210.);
    CHECK_EQ(value(), 0.f);
    auto invalid = driver.animateItemValue(handle, "hp/trail", std::numeric_limits<float>::quiet_NaN(), 100.f, "linear",
                                           0.f, 220.);
    CHECK(!invalid.ok());
    driver.animateItemValue(handle, "hp/trail", 1.f, 100.f, "outBack", 0.f, 220.).expect("overshoot");
    driver.tick(290.);
    CHECK(value() <= 1.f);
    driver.animateItemOpacity(handle, "hp/trail", 0.f, 100.f, "linear", 1000.f, 300.).expect("opacity");
    host->get().setTree(ui::resourceBar("hp", 240.f, 24.f, .25f));
    driver.tick(301.);
    CHECK_EQ(driver.itemTweenCount(), 0u);
    CHECK_EQ(value(), .25f);
    CHECK_EQ(host->get().findById("hp/trail")->get().opacity, 1.f);
    ecs::DestroyEntity(&host->get());
}

#include <limits>
#include "ui/UIHost.h"
#include "ui/editing/UiDocument.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::ui_editing;
using namespace eve::editing;
TEST_CASE("editor.ui.hud_fill_snapshot_roundtrip_and_runtime_publication_are_atomic") {
    UiDocumentTarget document("hud-fill");
    auto             root = document.makeCreate({ObjectId("root"), {}, "window", "HUD", {0, 0, 800, 600}});
    REQUIRE(root.ok());
    REQUIRE(document.applyDomainOperation(root.value()).ok());
    auto bar = document.makeCreate({ObjectId("hp"), ObjectId("root"), "filled-image", "HP", {20, 30, 200, 24}});
    REQUIRE(bar.ok());
    REQUIRE(document.applyDomainOperation(bar.value()).ok());
    UiContentValue content;
    content.value         = .75;
    content.fillDirection = "left";
    auto set              = document.makeSetContent(ObjectId("hp"), content);
    REQUIRE(set.ok());
    REQUIRE(document.applyDomainOperation(set.value()).ok());
    UiDocumentTarget restored("restored-hud");
    REQUIRE(restored.loadSnapshot(document.snapshotValue()).ok());
    CHECK_EQ(restored.widget(ObjectId("hp")).value().content.value, .75);
    const auto handle = eve::ui::UIHost::createHost("document-hud");
    auto       host   = eve::ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    UiDocumentRuntimeBridge bridge;
    REQUIRE(bridge.publish(restored, ObjectId("root"), &host->get()).ok());
    CHECK_EQ(host->get().findById("hp")->get().value, .75f);
    CHECK_EQ(int(host->get().findById("hp")->get().fillDirection), int(eve::ui::FillDirection::Left));
    content.textureAsset = "missing.png";
    auto asset           = restored.makeSetContent(ObjectId("hp"), content);
    REQUIRE(asset.ok());
    REQUIRE(restored.applyDomainOperation(asset.value()).ok());
    CHECK(!bridge.publish(restored, ObjectId("root"), &host->get()).ok());
    CHECK_EQ(host->get().findById("hp")->get().value, .75f);
    content.value = 2.0;
    CHECK(!document.makeSetContent(ObjectId("hp"), content).ok());
    ecs::DestroyEntity(&host->get());
}

TEST_CASE("editor.ui.hud_fill_legacy_migration_and_unknown_versions_preserve_document") {
    UiDocumentTarget source("legacy-hud");
    auto             create = source.makeCreate({ObjectId("hp"), {}, "progress", "HP", {0, 0, 200, 24}});
    REQUIRE(create.ok());
    REQUIRE(source.applyDomainOperation(create.value()).ok());
    auto  snapshot        = source.snapshotValue();
    auto& root            = *snapshot.getIf<EditorValue::Object>();
    root["schemaVersion"] = int64_t{2};
    auto& widgets         = *root.at("widgets").getIf<EditorValue::Array>();
    auto& widget          = *widgets[0].getIf<EditorValue::Object>();
    auto& content         = *widget.at("content").getIf<EditorValue::Object>();
    for (const char* field :
         {"value", "fillDirection", "fillStretch", "fillStartDegrees", "fillClockwise", "fillInnerRadius"})
        content.erase(field);
    UiDocumentTarget restored("migration-hud");
    REQUIRE(restored.loadSnapshot(snapshot).ok());
    CHECK_EQ(restored.widget(ObjectId("hp")).value().content.value, 0.0);
    root["schemaVersion"] = int64_t{99};
    CHECK(!restored.loadSnapshot(snapshot).ok());
    CHECK(restored.widget(ObjectId("hp")).ok());
    root["schemaVersion"] = int64_t{3};
    content["value"]      = std::numeric_limits<double>::quiet_NaN();
    CHECK(!restored.loadSnapshot(snapshot).ok());
    CHECK(restored.widget(ObjectId("hp")).ok());
}

TEST_CASE("editor.ui.hud_texture_resolution_caches_keys_and_preserves_tree_on_failure") {
    UiDocumentTarget document("textured-hud");
    auto             root = document.makeCreate({ObjectId("root"), {}, "window", "HUD", {0, 0, 800, 600}});
    REQUIRE(root.ok());
    REQUIRE(document.applyDomainOperation(root.value()).ok());
    for (const char* name : {"hp", "shield"}) {
        auto create = document.makeCreate({ObjectId(name), ObjectId("root"), "filled-image", name, {0, 0, 200, 24}});
        REQUIRE(create.ok());
        REQUIRE(document.applyDomainOperation(create.value()).ok());
        UiContentValue content;
        content.textureAsset = "hud/bar";
        auto set             = document.makeSetContent(ObjectId(name), content);
        REQUIRE(set.ok());
        REQUIRE(document.applyDomainOperation(set.value()).ok());
    }
    auto handle = eve::ui::UIHost::createHost("resolved-textures");
    auto host   = eve::ui::UIHost::resolve(handle);
    REQUIRE(host.has_value());
    UiDocumentRuntimeBridge bridge;
    int                     calls    = 0;
    UiTextureResolver       resolver = [&](const std::string& key) {
        ++calls;
        CHECK_EQ(key, std::string("hud/bar"));
        return eve::editing::applied<std::uint64_t>(42);
    };
    REQUIRE(bridge.publish(document, ObjectId("root"), &host->get(), resolver).ok());
    CHECK_EQ(calls, 1);
    CHECK_EQ(host->get().findById("hp")->get().textureId, 42u);
    CHECK_EQ(host->get().findById("shield")->get().textureId, 42u);
    auto token = host->get().nodeHandle("hp");
    REQUIRE(token.has_value());
    UiTextureResolver fail = [](const std::string&) {
        return eve::editing::failed<std::uint64_t>(EditorStatus::NotFound, RuleId("test.missing"), "missing texture");
    };
    CHECK(!bridge.publish(document, ObjectId("root"), &host->get(), fail).ok());
    CHECK(eve::ui::UIHost::resolveNode(*token).has_value());
    CHECK_EQ(host->get().findById("hp")->get().textureId, 42u);
    CHECK(!bridge
               .publish(document, ObjectId("root"), &host->get(),
                        [](const std::string&) { return eve::editing::applied<std::uint64_t>(0); })
               .ok());
    CHECK_EQ(host->get().findById("hp")->get().textureId, 42u);
    ecs::DestroyEntity(&host->get());
}
