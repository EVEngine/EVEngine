#include "zeroerr/unittest.h"
#include "ui/PcgDraggableWindow.h"
#include <limits>
using eve::ui::PcgDraggableWindow;
using eve::ui::PcgUiRequestStatus;
TEST_CASE("ui.pcgDraggableWindow.matchesPcgClampAndScale") { PcgDraggableWindow w; REQUIRE(w.configure(-100,100,200,100,1920,1080,2).ok()); REQUIRE(w.drag(100,-300).ok()); CHECK_EQ(w.getX(),-100.f); CHECK_EQ(w.getY(),50.f); REQUIRE(w.drag(-10000,10000).ok()); CHECK_EQ(w.getX(),-1820.f); CHECK_EQ(w.getY(),1030.f); }
TEST_CASE("ui.pcgDraggableWindow.pointerDownRaisesAndMiddleResets") { PcgDraggableWindow w; REQUIRE(w.configure(-300,200,200,100,1280,720,1).ok()); REQUIRE(w.drag(100,100).ok()); w.pointerDown(false); CHECK_EQ(w.consumeBringToFront(),PcgUiRequestStatus::Requested); CHECK_EQ(w.consumeBringToFront(),PcgUiRequestStatus::None); w.pointerDown(true); CHECK_EQ(w.getX(),-300.f); CHECK_EQ(w.getY(),200.f); CHECK_EQ(w.consumeBringToFront(),PcgUiRequestStatus::Requested); }
TEST_CASE("ui.pcgDraggableWindow.rejectsInvalidGeometryAtomically") { PcgDraggableWindow w; REQUIRE(w.configure(-10,20,100,80,800,600,1).ok()); CHECK(!w.configure(0,0,900,80,800,600,1).ok()); CHECK_EQ(w.getX(),-10.f); CHECK(!w.drag(std::numeric_limits<float>::quiet_NaN(),0).ok()); CHECK_EQ(w.getY(),20.f); }
