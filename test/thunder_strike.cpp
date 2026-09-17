#include "weather/ThunderStrike.h"
#include "weather/Weather.h"

#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>
#include <zeroerr/unittest.h>

using namespace eve::weather;

TEST_CASE("weather.thunderStrike.matchesPcgLocationAudioAndDecay") {
    ThunderStrikeState state;
    ThunderStrikeSettings settings;
    settings.intensity = 2.f; settings.radius = 420.f; settings.volume = 0.7f; settings.audioClipCount = 4;
    auto strike = triggerThunderStrike(state, settings, 10.f, 20.f, 30.f, 1234u);
    REQUIRE(strike.ok());
    CHECK(state.playing);
    CHECK(state.intensity == 2.f);
    CHECK(strike.value().y >= 70.f);
    CHECK(strike.value().y < 270.f);
    CHECK(strike.value().audioClipIndex >= 1);
    CHECK(strike.value().audioClipIndex < 4);
    CHECK(!triggerThunderStrike(state, settings, 0.f, 0.f, 0.f, 1u).ok());
    REQUIRE(advanceThunderStrike(state, 0.25f).ok());
    CHECK(state.intensity == 1.f);
    REQUIRE(advanceThunderStrike(state, 1.f).ok());
    CHECK(!state.playing);
    CHECK(state.intensity == 0.f);
}

TEST_CASE("weather.thunderStrike.isDeterministicAndFailureAtomic") {
    ThunderStrikeSettings settings;
    ThunderStrikeState a, b;
    auto first = triggerThunderStrike(a, settings, 1.f, 2.f, 3.f, 99u);
    auto second = triggerThunderStrike(b, settings, 1.f, 2.f, 3.f, 99u);
    REQUIRE(first.ok()); REQUIRE(second.ok());
    CHECK(first.value().x == second.value().x);
    CHECK(first.value().y == second.value().y);
    CHECK(first.value().z == second.value().z);
    CHECK(first.value().audioClipIndex == -1);
    ThunderStrikeState untouched;
    settings.radius = std::numeric_limits<float>::quiet_NaN();
    CHECK(!triggerThunderStrike(untouched, settings, 0.f, 0.f, 0.f, 7u).ok());
    CHECK(!untouched.playing);
    CHECK(!advanceThunderStrike(untouched, -0.1f).ok());
}

TEST_CASE("weather.thunderStrike.squirrelContract") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto table = vm.addTable("eve");
    eve::script::exposeResultBindings(table);
    Weather::expose(table);
    vm.run(vm.compileSource(R"(
        local state = eve.ThunderStrikeState();
        local settings = eve.ThunderStrikeSettings();
        settings.intensity = 1.5; settings.radius = 350.0;
        settings.volume = 0.8; settings.audioClipCount = 3;
        local strike = eve.triggerThunderStrike(state, settings, 10.0, 20.0, 30.0, 42);
        assert(strike.ok && strike.value.y >= 70.0 && strike.value.y < 270.0);
        assert(strike.value.audioClipIndex >= 1 && strike.value.audioClipIndex < 3);
        assert(state.playing && state.intensity == 1.5);
        assert(eve.advanceThunderStrike(state, 1.0).ok && !state.playing);
    )"));
}
