#include "zeroerr/unittest.h"

#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>

#include "ui/PcgLoadingScreen.h"
#include "ui/UI.h"

using eve::ui::PcgLoadingScreen;

TEST_CASE("ui.pcgLoadingScreen.progressCompletesAndFades") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(2.f).ok());
    screen.onLoadProgressStarted();
    CHECK(screen.getCanvasVisible());
    CHECK_EQ(screen.getProgress(),0.f);
    CHECK_EQ(screen.getBackgroundAlpha(),1.f);
    REQUIRE(screen.onLoadProgressUpdated(.75f).ok());
    CHECK_EQ(screen.getProgress(),.75f);
    CHECK(!screen.getFading());
    REQUIRE(screen.onLoadProgressUpdated(1.f).ok());
    CHECK(screen.getFading());
    CHECK(!screen.getProgressVisible());
    CHECK(!screen.getTextVisible());
    REQUIRE(screen.tick(.25f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(),.5f);
    REQUIRE(screen.tick(.25f).ok());
    CHECK(!screen.getCanvasVisible());
    CHECK(!screen.getFading());
}

TEST_CASE("ui.pcgLoadingScreen.buildsTimeoutDiagnosticTransactionally") {
    PcgLoadingScreen screen;
    CHECK(!screen.addMissingScene("tile", "tile-impostor").ok());
    screen.beginTimeout();
    auto scene=screen.addMissingScene("Terrain_1", "Impostor_1");
    REQUIRE(scene.ok());
    REQUIRE(screen.addRegularReference(scene.value(),"Biome Loader").ok());
    REQUIRE(screen.addRegularReference(scene.value(),"Player").ok());
    REQUIRE(screen.addImpostorReference(scene.value(),"Far Camera").ok());
    REQUIRE(screen.endTimeout().ok());
    CHECK(screen.getFading());
    CHECK(screen.getTimeoutMessage().find("Terrain_1")!=std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Biome Loader, Player, ")!=std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Impostor_1")!=std::string::npos);
    CHECK(screen.getTimeoutMessage().find("Far Camera, ")!=std::string::npos);
    CHECK(!screen.endTimeout().ok());
}

TEST_CASE("ui.pcgLoadingScreen.invalidEventsPreservePublishedStateAndBindRealVm") {
    PcgLoadingScreen screen;
    REQUIRE(screen.configure(1.f).ok());
    screen.onLoadProgressStarted();
    REQUIRE(screen.onLoadProgressUpdated(.4f).ok());
    CHECK(!screen.onLoadProgressUpdated(std::numeric_limits<float>::quiet_NaN()).ok());
    CHECK_EQ(screen.getProgress(),.4f);
    CHECK(!screen.tick(-1.f).ok());
    CHECK_EQ(screen.getBackgroundAlpha(),1.f);

    ssq::VM vm(1024);auto table=vm.addTable("eve");eve::ui::UI::expose(table);
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
