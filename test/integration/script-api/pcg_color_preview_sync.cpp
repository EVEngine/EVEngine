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


// pcg_controller_selection.cpp
using eve::ui::PcgControllerSelection;


// pcg_draggable_window.cpp
using eve::ui::PcgDraggableWindow;
using eve::ui::PcgUiRequestStatus;


// pcg_loading_screen.cpp
using eve::ui::PcgLoadingScreen;


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


// pcg_tooltip.cpp
using eve::ui::PcgTooltipManager;
