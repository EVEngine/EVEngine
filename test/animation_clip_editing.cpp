#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimClip.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cmath>
#include <string>

using namespace eve::animation;

TEST_CASE("animation.clip.editableTracksStaySorted") {
    AnimClip clip("editable");
    clip.setDuration(2.f);
    clip.addPositionKey(0, 0.f, 0.f, 0.f, 0.f);
    clip.addPositionKey(0, 1.f, 1.f, 2.f, 3.f);
    clip.addRotationKey(0, 0.5f, 0.f, 0.f, 0.f, 1.f);
    clip.addScaleKey(0, 0.5f, 1.f, 1.f, 1.f);
    clip.addEvent(0.25f, "step", "left");

    REQUIRE(clip.setPositionKey(0, 1, 0.1f, 4.f, 5.f, 6.f));
    CHECK_EQ(clip.getPositionKeyTime(0, 0), 0.f);
    CHECK_EQ(clip.getPositionKeyTime(0, 1), 0.1f);
    CHECK_EQ(clip.getPositionKeyX(0, 1), 4.f);
    REQUIRE(clip.setRotationKey(0, 0, 0.4f, 0.f, 2.f, 0.f, 0.f));
    CHECK(std::fabs(clip.getRotationKeyY(0, 0) - 1.f) < 1e-6f);
    REQUIRE(clip.setEvent(0, 0.75f, "land", "hard"));
    CHECK_EQ(clip.getEventName(0), std::string("land"));
    CHECK_EQ(clip.getEventPayload(0), std::string("hard"));
    CHECK(!clip.removePositionKey(3, 0));
    REQUIRE(clip.removeScaleKey(0, 0));
    CHECK_EQ(clip.getScaleKeyCount(0), 0);
    REQUIRE(clip.clearTrack(0));
    CHECK_EQ(clip.getPositionKeyCount(0), 0);
    CHECK_EQ(clip.getRotationKeyCount(0), 0);
    REQUIRE(clip.removeEvent(0));
    CHECK_EQ(clip.getEventCount(), 0);
}

TEST_CASE("animation.clip.notifyContractRejectsMissingSemanticEvents") {
    AnimClip clip("vault-low");
    clip.setDuration(1.f);
    clip.addEvent(0.25f, "contact.left_hand");
    clip.addEvent(0.75f, "land");

    CHECK(clip.hasEvent("contact.left_hand"));
    CHECK(!clip.hasEvent("contact.right_hand"));
    auto complete = clip.validateNotifyContract({"contact.left_hand", "land"});
    REQUIRE(complete.ok());
    auto missing = clip.validateNotifyContract({"contact.left_hand", "contact.right_hand", "land"});
    CHECK(!missing.ok());
    CHECK_EQ(static_cast<int>(missing.code()), static_cast<int>(eve::StatusCode::Rejected));
}

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
