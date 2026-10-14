#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include "ui/PcgColorPreviewSync.h"
#include "ui/PcgControllerSelection.h"
#include "ui/PcgDraggableWindow.h"
#include "ui/PcgLoadingScreen.h"
#include "ui/PcgScreenshotSavedNotice.h"
#include "ui/PcgTooltip.h"
#include "ui/UI.h"
#include "zeroerr/unittest.h"

// pcg_color_preview_sync.cpp
using eve::ui::PcgColorPreviewSync;
TEST_CASE("ui.pcgColorPreview.multipliesNonBlackSource") {
    PcgColorPreviewSync sync;
    REQUIRE(sync.sync(.2f, .3f, .4f, .5f).ok());
    CHECK_EQ(sync.getRed(), .5f);
    CHECK_EQ(sync.getGreen(), .75f);
    CHECK_EQ(sync.getBlue(), 1.f);
    CHECK_EQ(sync.getAlpha(), 1.25f);
}
TEST_CASE("ui.pcgColorPreview.blackUsesReadableGrayAndRejectsInvalid") {
    PcgColorPreviewSync sync;
    REQUIRE(sync.sync(0, 0, 0, .2f).ok());
    CHECK_EQ(sync.getRed(), .5f);
    CHECK_EQ(sync.getGreen(), .5f);
    CHECK_EQ(sync.getBlue(), .5f);
    CHECK_EQ(sync.getAlpha(), 1.f);
    CHECK(!sync.sync(std::numeric_limits<float>::quiet_NaN(), 0, 0, 1).ok());
    CHECK_EQ(sync.getRed(), .5f);
}

// pcg_controller_selection.cpp
using eve::ui::PcgControllerSelection;
TEST_CASE("ui.pcgControllerSelection.firstMatchWins") {
    PcgControllerSelection s;
    REQUIRE(s.add("Flying", 1, "flying").ok());
    REQUIRE(s.add("Duplicate", 1, "duplicate").ok());
    REQUIRE(s.add("Third", 3, "third").ok());
    auto r = s.refresh(1);
    REQUIRE(r.ok());
    CHECK(r.value());
    CHECK_EQ(s.getSelectedIndex(), 0);
    auto a = s.getVisible(0);
    auto b = s.getVisible(1);
    auto c = s.getVisible(2);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    REQUIRE(c.ok());
    CHECK(a.value());
    CHECK(!b.value());
    CHECK(!c.value());
    auto id = s.getWidgetId(0);
    REQUIRE(id.ok());
    CHECK_EQ(id.value(), "flying");
}
TEST_CASE("ui.pcgControllerSelection.noMatchPreservesSnapshot") {
    PcgControllerSelection s;
    REQUIRE(s.add("A", 1, "a").ok());
    REQUIRE(s.add("B", 2, "b").ok());
    REQUIRE(s.refresh(2).ok());
    auto miss = s.refresh(99);
    REQUIRE(miss.ok());
    CHECK(!miss.value());
    CHECK_EQ(s.getSelectedIndex(), 1);
    auto visible = s.getVisible(1);
    REQUIRE(visible.ok());
    CHECK(visible.value());
    CHECK(!s.getVisible(2).ok());
    CHECK(!s.add("", 3, "x").ok());
    CHECK_EQ(s.getCount(), 2);
}

// pcg_draggable_window.cpp
using eve::ui::PcgDraggableWindow;
using eve::ui::PcgUiRequestStatus;
TEST_CASE("ui.pcgDraggableWindow.matchesPcgClampAndScale") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-100, 100, 200, 100, 1920, 1080, 2).ok());
    REQUIRE(w.drag(100, -300).ok());
    CHECK_EQ(w.getX(), -100.f);
    CHECK_EQ(w.getY(), 50.f);
    REQUIRE(w.drag(-10000, 10000).ok());
    CHECK_EQ(w.getX(), -1820.f);
    CHECK_EQ(w.getY(), 1030.f);
}
TEST_CASE("ui.pcgDraggableWindow.pointerDownRaisesAndMiddleResets") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-300, 200, 200, 100, 1280, 720, 1).ok());
    REQUIRE(w.drag(100, 100).ok());
    w.pointerDown(false);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::Requested);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::None);
    w.pointerDown(true);
    CHECK_EQ(w.getX(), -300.f);
    CHECK_EQ(w.getY(), 200.f);
    CHECK_EQ(w.consumeBringToFront(), PcgUiRequestStatus::Requested);
}
TEST_CASE("ui.pcgDraggableWindow.rejectsInvalidGeometryAtomically") {
    PcgDraggableWindow w;
    REQUIRE(w.configure(-10, 20, 100, 80, 800, 600, 1).ok());
    CHECK(!w.configure(0, 0, 900, 80, 800, 600, 1).ok());
    CHECK_EQ(w.getX(), -10.f);
    CHECK(!w.drag(std::numeric_limits<float>::quiet_NaN(), 0).ok());
    CHECK_EQ(w.getY(), 20.f);
}

// pcg_loading_screen.cpp
using eve::ui::PcgLoadingScreen;

TEST_CASE("ui.pcgLoadingScreen.progressCompletesAndFades") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(2.f).ok());
    screen.onLoadProgressStarted();
    CHECK(screen.getCanvasVisible());
    CHECK_EQ(screen.getProgress(), 0.f);
    CHECK_EQ(screen.getBackgroundAlpha(), 1.f);
    REQUIRE(screen.onLoadProgressUpdated(.75f).ok());
    CHECK_EQ(screen.getProgress(), .75f);
    CHECK(!screen.getFading());
    REQUIRE(screen.onLoadProgressUpdated(1.f).ok());
    CHECK(screen.getFading());
    CHECK(!screen.getProgressVisible());
    CHECK(!screen.getTextVisible());
    REQUIRE(screen.tick(.25f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(), .5f);
    REQUIRE(screen.tick(.25f).ok());
    CHECK(!screen.getCanvasVisible());
    CHECK(!screen.getFading());
}

TEST_CASE("ui.pcgLoadingScreen.buildsTimeoutDiagnosticTransactionally") {
    PcgLoadingScreen screen;
    CHECK(!screen.addMissingScene("tile", "tile-impostor").ok());
    screen.beginTimeout();
    auto scene = screen.addMissingScene("Terrain_1", "Impostor_1");
    REQUIRE(scene.ok());
    REQUIRE(screen.addRegularReference(scene.value(), "Biome Loader").ok());
    REQUIRE(screen.addRegularReference(scene.value(), "Player").ok());
    REQUIRE(screen.addImpostorReference(scene.value(), "Far Camera").ok());
    REQUIRE(screen.endTimeout().ok());
    CHECK(screen.getFading());
    CHECK(screen.getTimeoutMessage().find("Terrain_1") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Biome Loader, Player, ") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Impostor_1") != std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Far Camera, ") != std::string::npos);
    CHECK(!screen.endTimeout().ok());
}

TEST_CASE("ui.pcgLoadingScreen.invalidEventsPreservePublishedStateAndBindRealVm") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(1.f).ok());
    screen.onLoadProgressStarted();
    REQUIRE(screen.onLoadProgressUpdated(.4f).ok());
    CHECK(!screen.onLoadProgressUpdated(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(screen.getProgress(), .4f);
    CHECK(!screen.tick(-1.f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(), 1.f);

    ssq::VM vm(1024);
    auto    table = vm.addTable("eve");
    eve::ui::UI::expose(table);
    vm.run(vm.compileSource(R"(
      local s=eve.PcgLoadingScreen();assert(s.configure(4.0).ok);
      s.onLoadProgressStarted();assert(s.getCanvasVisible() && s.getProgress()==0.0);
      assert(s.onLoadProgressUpdated(1.0).ok && s.getFading());
      assert(s.tick(0.25).ok && !s.getCanvasVisible() && s.getBackgroundAlpha()==0.0);
      s.beginTimeout();local i=s.addMissingScene("West","West Impostor");assert(i.ok);
      assert(s.addRegularReference(i.value,"Biome").ok && s.endTimeout().ok);
      assert(s.getTimeoutMessage().len()>0);
    )"));
}

// pcg_screenshot_saved_notice.cpp
using eve::ui::PcgScreenshotSavedNotice;
TEST_CASE("ui.pcgScreenshotNotice.waitsForEndFrameAndExpires") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 1).ok());
    REQUIRE(n.request("shots/a.png").ok());
    CHECK(n.getPending());
    CHECK(!n.getVisible());
    REQUIRE(n.tick(10).ok());
    CHECK(!n.getVisible());
    REQUIRE(n.endFrame(10).ok());
    CHECK(n.getVisible());
    CHECK(n.getPathVisible());
    CHECK_EQ(n.getPath(), "shots/a.png");
    REQUIRE(n.tick(10.999f).ok());
    CHECK(n.getVisible());
    REQUIRE(n.tick(11).ok());
    CHECK(!n.getVisible());
}
TEST_CASE("ui.pcgScreenshotNotice.restartAndDisableCancelOldNotice") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 2).ok());
    REQUIRE(n.request("a.png").ok());
    REQUIRE(n.endFrame(1).ok());
    REQUIRE(n.request("b.png").ok());
    CHECK(!n.getVisible());
    REQUIRE(n.endFrame(2).ok());
    REQUIRE(n.tick(3.5f).ok());
    CHECK(n.getVisible());
    REQUIRE(n.configure(false, 2).ok());
    CHECK(!n.getVisible());
    CHECK(!n.getPending());
    REQUIRE(n.request("ignored.png").ok());
    CHECK(!n.getPending());
}
TEST_CASE("ui.pcgScreenshotNotice.invalidInputsAreAtomic") {
    PcgScreenshotSavedNotice n;
    REQUIRE(n.configure(true, 1).ok());
    REQUIRE(n.request("ok.png").ok());
    CHECK(!n.configure(true, std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK(n.getPending());
    CHECK(!n.endFrame(-1).ok());
    CHECK(n.getPending());
}

// pcg_tooltip.cpp
using eve::ui::PcgTooltipManager;
TEST_CASE("ui.pcgTooltip.profileUsesFirstSubstringMatch") {
    PcgTooltipManager m;
    REQUIRE(m.addTooltip("camera.fov", "FOV", "Field of view").ok());
    REQUIRE(m.addTooltip("camera.fov.extra", "Other", "Later").ok());
    auto found = m.enter(0, "fov", 10);
    REQUIRE(found.ok());
    CHECK(found.value());
    CHECK(m.getPending());
    REQUIRE(m.tick(11.9f, 100, 200, 1000, 800, false).ok());
    CHECK(!m.getVisible());
    REQUIRE(m.tick(12, 100, 200, 1000, 800, false).ok());
    CHECK(m.getVisible());
    CHECK_EQ(m.getHeader(), "FOV");
    auto removed = m.removeTooltip("fov");
    REQUIRE(removed.ok());
    CHECK(removed.value());
    CHECK_EQ(m.getCount(), 1);
}
TEST_CASE("ui.pcgTooltip.triggerModesPivotThemeAndDismissal") {
    PcgTooltipManager m;
    REQUIRE(m.configure(1, 0, 1, 2, 3, 4).ok());
    REQUIRE(m.enterText(0, "ignored", "UI", 0).ok());
    CHECK(!m.getPending());
    REQUIRE(m.enterText(1, "long content", "Scene", 1).ok());
    REQUIRE(m.tick(2, 400, 700, 800, 800, false).ok());
    CHECK(m.getVisible());
    CHECK_EQ(m.getPivotX(), .5f);
    CHECK_EQ(m.getPivotY(), 2.625f);
    CHECK(m.getHeaderVisible());
    CHECK(m.getWrapEnabled());
    CHECK_EQ(m.getBackgroundR(), 1.f);
    CHECK_EQ(m.getForeground(), 0.f);
    REQUIRE(m.tick(2.1f, 400, 100, 800, 800, true).ok());
    CHECK(!m.getVisible());
}
TEST_CASE("ui.pcgTooltip.exitAndInvalidConfigAreSafe") {
    PcgTooltipManager m;
    REQUIRE(m.configure(2, 1, 0, 2, 2, 40).ok());
    REQUIRE(m.enterText(0, "text", "", 0).ok());
    REQUIRE(m.exit(0).ok());
    CHECK(!m.getPending());
    CHECK(!m.getVisible());
    CHECK(!m.configure(3, 0, 1, 1, 1, 10).ok());
    CHECK(!m.tick(-1, 0, 0, 100, 100, false).ok());
    auto miss = m.removeTooltip("missing");
    REQUIRE(miss.ok());
    CHECK(!miss.value());
}
