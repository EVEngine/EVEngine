#include "audio/AudioZone.h"
#include "audio/Audio.h"

#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>
#include <zeroerr/unittest.h>

using namespace eve::audio;

TEST_CASE("audio.pcgZone.activationFadeTrackAndDelayedExit") {
    AudioZoneProfile profile; profile.radius=10.f; profile.minimumBreakTime=2.f; profile.maximumBreakTime=2.f;
    AudioZoneItem item; item.volume=.8f; item.fadeInTime=2.f; item.fadeOutTime=2.f; item.duration=10.f;
    REQUIRE(profile.addItem(item).ok());
    AudioZoneState state;
    auto start=evaluateAudioZone(profile,state,.1f,0.f,0.f,0.f,.5f,1u);
    REQUIRE(start.ok()); CHECK(start.value().play); CHECK(state.phase==AudioZonePhase::Active);
    auto fade=evaluateAudioZone(profile,state,1.1f,0.f,0.f,0.f,.5f,1u);
    REQUIRE(fade.ok()); CHECK(fade.value().volume>.39f); CHECK(fade.value().volume<.41f);
    auto leave=evaluateAudioZone(profile,state,2.f,20.f,0.f,0.f,1.f,1u);
    REQUIRE(leave.ok()); CHECK(state.phase==AudioZonePhase::BecomingInactive); CHECK(!leave.value().stop);
    REQUIRE(evaluateAudioZone(profile,state,11.9f,20.f,0.f,0.f,1.f,1u).ok());
    CHECK(state.phase==AudioZonePhase::BecomingInactive);
    auto stopped=evaluateAudioZone(profile,state,12.1f,20.f,0.f,0.f,1.f,1u);
    REQUIRE(stopped.ok()); CHECK(stopped.value().stop); CHECK(state.phase==AudioZonePhase::Inactive);
}

TEST_CASE("audio.pcgZone.avoidsImmediateRepeatAndFailsAtomically") {
    AudioZoneProfile profile; profile.global=true; profile.minimumBreakTime=0; profile.maximumBreakTime=0;
    AudioZoneItem item; item.duration=.5f; item.fadeInTime=0; item.fadeOutTime=0;
    REQUIRE(profile.addItem(item).ok()); REQUIRE(profile.addItem(item).ok());
    AudioZoneState state;
    auto first=evaluateAudioZone(profile,state,.1f,0,0,0,1,55u); REQUIRE(first.ok());
    const int selected=state.selectedTrack;
    REQUIRE(evaluateAudioZone(profile,state,.7f,0,0,0,1,55u).ok());
    auto second=evaluateAudioZone(profile,state,.8f,0,0,0,1,55u); REQUIRE(second.ok());
    CHECK(second.value().play); CHECK(state.selectedTrack!=selected);
    const auto before=state;
    CHECK(!evaluateAudioZone(profile,state,std::numeric_limits<float>::quiet_NaN(),0,0,0,1,1u).ok());
    CHECK(state.selectedTrack==before.selectedTrack); CHECK(state.playing==before.playing);
}

TEST_CASE("audio.pcgZone.squirrelContract") {
    ssq::VM vm(2048,ssq::Libs::ALL); auto table=vm.addTable("eve");
    eve::script::exposeResultBindings(table); Audio::expose(table);
    vm.run(vm.compileSource(R"(
        local profile=eve.AudioZoneProfile(); profile.radius=12.0;
        profile.minimumBreakTime=1.0; profile.maximumBreakTime=1.0;
        local item=eve.AudioZoneItem(); item.volume=0.8; item.fadeInTime=2.0;
        item.fadeOutTime=2.0; item.duration=10.0;
        assert(profile.addItem(item).value==0 && profile.getItemCount()==1);
        local state=eve.AudioZoneState();
        local start=eve.evaluateAudioZone(profile,state,0.1,0.0,0.0,0.0,0.5,17);
        assert(start.ok && start.value.play && start.value.trackIndex==0);
        local fade=eve.evaluateAudioZone(profile,state,1.1,0.0,0.0,0.0,0.5,17);
        assert(fade.ok && fade.value.volume>0.39 && fade.value.volume<0.41);
    )"));
}
