#include "weather/ThunderStrike.h"
#include "weather/Weather.h"

#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>

#include <limits>
#include <zeroerr/unittest.h>

using namespace eve::weather;


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
