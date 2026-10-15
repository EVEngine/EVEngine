#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

using namespace eve::animation;


TEST_CASE("animation.script.composesClipTimelineFromReflectedKeys") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        anim <- eve.Animation();
        clip <- anim.newClip("project-action");
        clip.setDuration(2.0);
        clip.addPositionKey(0, 0.0, 0.0, 0.0, 0.0);
        clip.addPositionKey(0, 1.0, 1.0, 2.0, 3.0);
        edited <- clip.setPositionKey(0, 1, 0.5, 4.0, 5.0, 6.0);
        keyTime <- clip.getPositionKeyTime(0, 1);
        keyX <- clip.getPositionKeyX(0, 1);
        clip.addEvent(0.25, "step", "left");
        eventEdited <- clip.setEvent(0, 0.75, "land", "hard");
        eventTime <- clip.getEventTime(0);
        eventName <- clip.getEventName(0);
        trackCount <- clip.getTrackCount();
        keyRemoved <- clip.removePositionKey(0, 0);
    )"));

    CHECK(vm.find("edited").toBool());
    CHECK_EQ(vm.find("keyTime").toFloat(), 0.5f);
    CHECK_EQ(vm.find("keyX").toFloat(), 4.f);
    CHECK(vm.find("eventEdited").toBool());
    CHECK_EQ(vm.find("eventTime").toFloat(), 0.75f);
    CHECK_EQ(vm.find("eventName").toString(), std::string("land"));
    CHECK_EQ(vm.find("trackCount").toInt(), 1);
    CHECK(vm.find("keyRemoved").toBool());
}
