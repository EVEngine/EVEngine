// Independent pinned-source createAct clock goldens, including partial initial FX steps.
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include "stylize/pixelfx/ActRuntime.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_act_clock_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
eve::Result<std::unique_ptr<ActRuntime>> build(PixelFxKind kind, const PixelFxSettings& settings) {
    auto prepared = ActRuntime::prepare(kind, settings);
    if (!prepared) return eve::Result<std::unique_ptr<ActRuntime>>::failure(prepared.status());
    return ActRuntime::publish(std::move(prepared).takeValue());
}
struct Frame {
    size_t tick;
    double time, fx;
    bool   frozen, done;
    int    sx, sy;
    size_t events;
    double x, y, crouch;
    bool   foot;
};
void check(PixelFxKind kind, std::span<const Frame> frames, zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
    auto ready = build(kind, PixelFx::defaults(kind));
    if (!ready) throw std::runtime_error(ready.status().describe());
    auto   act  = std::move(ready).takeValue();
    size_t tick = 0;
    for (const auto& frame : frames) {
        while (tick < frame.tick) {
            auto stepped = act->step(1.0 / 120);
            if (!stepped) throw std::runtime_error(stepped.status().describe());
            ++tick;
        }
        const auto state = act->snapshot();
        REQUIRE(state.actTime == frame.time);
        REQUIRE(state.effectTime == frame.fx);
        REQUIRE(state.frozen == frame.frozen);
        REQUIRE(state.done == frame.done);
        REQUIRE(state.shake[0] == frame.sx);
        REQUIRE(state.shake[1] == frame.sy);
        REQUIRE(act->hitEvents().size() == frame.events);
        REQUIRE(state.pose.pose.x == frame.x);
        REQUIRE(state.pose.pose.y == frame.y);
        REQUIRE(state.pose.pose.crouch == frame.crouch);
        REQUIRE(state.pose.contains(rig::PoseField::Ffx) == frame.foot);
    }
}
TEST_CASE("stylize.pixelfx.actClock.ArrowShot.source") {
    const Frame frames[] = {
        {0, -0.44, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {1, -0.43166666666666664, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {2, -0.4233333333333333, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {52, -0.006666666666666151, 0, false, false, 0, 0, 0, 0, 0, 3.1886145404663924, true},
        {53, 0.0016666666666671822, 0.0016666666666671822, false, false, 0, 0, 0, 0, 0, 3.1886145404663924, true},
        {54, 0.010000000000000515, 0.010000000000000515, false, false, 0, 0, 0, 0, 0, 3.1886145404663924, true},
        {55, 0.018333333333333847, 0.018333333333333847, false, false, 0, 0, 0, 0, 0, 3.1886145404663924, true},
        {120, 0.560000000000001, 0.560000000000001, false, false, 0, 0, 0, 0, 0, 4, true},
        {148, 0.7933333333333336, 0.7933333333333336, false, false, 0, 0, 0, 0, 0, 3, true},
        {149, 0.8016666666666669, 0.8016666666666669, true, false, -2, 1, 1, 0, 0, 3, true},
        {150, 0.8016666666666669, 0.8016666666666669, true, false, 2, -1, 0, 0, 0, 3, true},
        {151, 0.8016666666666669, 0.8016666666666669, true, false, 2, -1, 0, 0, 0, 3, true},
        {157, 0.8016666666666669, 0.8016666666666669, true, false, 1, 0, 0, 0, 0, 3, true},
        {158, 0.8016666666666669, 0.8016666666666669, false, false, -1, 0, 0, 0, 0, 3, true},
        {159, 0.8100000000000002, 0.8100000000000002, false, false, -1, 0, 0, 0, 0, 3, true},
        {160, 0.8183333333333335, 0.8183333333333335, false, false, 0, 0, 0, 0, 0, 3, true},
        {240, 1.4849999999999979, 1.4849999999999979, false, false, 0, 0, 0, 0, 0, 3, true},
        {289, 1.8933333333333298, 1.8933333333333298, false, false, 0, 0, 0, 0, 0, 3, true},
        {290, 1.901666666666663, 1.901666666666663, false, true, 0, 0, 0, 0, 0, 4, true},
        {291, 1.9099999999999964, 1.9099999999999964, false, true, 0, 0, 0, 0, 0, 4, true},
        {292, 1.9183333333333297, 1.9183333333333297, false, true, 0, 0, 0, 0, 0, 4, true},
        {360, 2.4849999999999945, 2.4849999999999945, false, true, 0, 0, 0, 0, 0, 4, true},
        {410, 2.9016666666666597, 2.9016666666666597, false, true, 0, 0, 0, 0, 0, 3, true},
    };
    check(PixelFxKind::ArrowShot, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.FireballThrow.source") {
    const Frame frames[] = {
        {0, -0.42, 0, false, false, 0, 0, 0, -3, 0, 3, true},
        {1, -0.4116666666666666, 0, false, false, 0, 0, 0, -3, 0, 3, true},
        {2, -0.40333333333333327, 0, false, false, 0, 0, 0, -3, 0, 3, true},
        {50, -0.0033333333333328517, 0, false, false, 0, 0, 0, -3, 0, 3.925925925925926, true},
        {51, 0.0050000000000004815, 0.0050000000000004815, false, false, 0, 0, 0, -3, 0, 3.925925925925926, true},
        {52, 0.013333333333333815, 0.013333333333333815, false, false, 0, 0, 0, -3, 0, 3.925925925925926, true},
        {53, 0.021666666666667146, 0.021666666666667146, false, false, 0, 0, 0, -3, 0, 3.925925925925926, true},
        {120, 0.580000000000001, 0.580000000000001, false, false, 0, 0, 0, -3, 0, 4, true},
        {219, 1.404999999999998, 1.404999999999998, false, false, 0, 0, 0, 9, 0, 3, true},
        {220, 1.4133333333333313, 1.4133333333333313, true, false, 2, 2, 1, 7, 0, 5, true},
        {221, 1.4133333333333313, 1.4133333333333313, true, false, 2, 2, 0, 7, 0, 5, true},
        {222, 1.4133333333333313, 1.4133333333333313, true, false, 0, -1, 0, 7, 0, 5, true},
        {240, 1.4133333333333313, 1.4133333333333313, true, false, -1, 1, 0, 7, 0, 5, true},
        {241, 1.4133333333333313, 1.4133333333333313, true, false, -1, 1, 0, 7, 0, 5, true},
        {242, 1.4133333333333313, 1.4133333333333313, false, false, 1, -1, 0, 7, 0, 5, true},
        {243, 1.4216666666666646, 1.4216666666666646, false, false, 1, -1, 0, 7, 0, 5, true},
        {244, 1.429999999999998, 1.429999999999998, false, false, -1, 1, 0, 7, 0, 5, true},
        {360, 2.396666666666661, 2.396666666666661, false, false, 0, 0, 0, 5.902699060576644, 0, 3, true},
        {480, 3.3966666666666576, 3.3966666666666576, false, false, 0, 0, 0, -2.50894028722593, 0, 3, true},
        {600, 4.396666666666675, 4.396666666666675, false, false, 0, 0, 0, -3, 0, 4, true},
        {720, 5.396666666666725, 5.396666666666725, false, false, 0, 0, 0, -3, 0, 3, true},
        {817, 6.205000000000099, 6.205000000000099, false, false, 0, 0, 0, -3, 0, 3, true},
        {818, 6.213333333333432, 6.213333333333432, false, false, 0, 0, 1, -3, 0, 3, true},
        {819, 6.221666666666766, 6.221666666666766, false, false, 0, 0, 0, -3, 0, 3, true},
        {820, 6.2300000000001, 6.2300000000001, false, false, 0, 0, 0, -3, 0, 3, true},
        {840, 6.396666666666775, 6.396666666666775, false, false, 0, 0, 0, -3, 0, 4, true},
        {889, 6.8050000000001285, 6.8050000000001285, false, false, 0, 0, 0, -3, 0, 3, true},
        {890, 6.813333333333462, 6.813333333333462, false, true, 0, 0, 0, -3, 0, 3, true},
        {891, 6.821666666666796, 6.821666666666796, false, true, 0, 0, 0, -3, 0, 3, true},
        {892, 6.83000000000013, 6.83000000000013, false, true, 0, 0, 0, -3, 0, 3, true},
        {960, 7.396666666666825, 7.396666666666825, false, true, 0, 0, 0, -3, 0, 3, true},
        {1010, 7.813333333333512, 7.813333333333512, false, true, 0, 0, 0, -3, 0, 4, true},
    };
    check(PixelFxKind::FireballThrow, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.PistolShots.source") {
    const Frame frames[] = {
        {0, -0.36, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {1, -0.3516666666666666, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {2, -0.34333333333333327, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {43, -0.0016666666666663686, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {44, 0.006666666666666965, 0.006666666666666965, false, false, 0, 0, 0, 0, 0, 3, true},
        {45, 0.015000000000000298, 0.015000000000000298, false, false, 0, 0, 0, 0, 0, 3, true},
        {46, 0.02333333333333363, 0.02333333333333363, false, false, 0, 0, 0, 0, 0, 3, true},
        {74, 0.25666666666666693, 0.25666666666666693, false, false, 0, 0, 0, 0, 0, 3, true},
        {75, 0.2650000000000003, 0.2650000000000003, true, false, 1, -1, 1, 0, 0, 3, true},
        {76, 0.2650000000000003, 0.2650000000000003, true, false, 0, 1, 0, 0, 0, 3, true},
        {77, 0.2650000000000003, 0.2650000000000003, true, false, 0, 1, 0, 0, 0, 3, true},
        {79, 0.2650000000000003, 0.2650000000000003, true, false, 0, 0, 0, 0, 0, 3, true},
        {80, 0.2650000000000003, 0.2650000000000003, false, false, 0, 0, 0, 0, 0, 3, true},
        {81, 0.27333333333333365, 0.27333333333333365, false, false, 0, 0, 0, 0, 0, 3, true},
        {82, 0.281666666666667, 0.281666666666667, false, false, 0, 0, 0, 0, 0, 3, true},
        {112, 0.5316666666666676, 0.5316666666666676, false, false, 0, 0, 0, 0, 0, 3, true},
        {113, 0.5400000000000009, 0.5400000000000009, true, false, 1, -1, 1, 0, 0, 3, true},
        {114, 0.5400000000000009, 0.5400000000000009, true, false, 0, 1, 0, 0, 0, 3, true},
        {115, 0.5400000000000009, 0.5400000000000009, true, false, 0, 1, 0, 0, 0, 3, true},
        {117, 0.5400000000000009, 0.5400000000000009, true, false, 0, 0, 0, 0, 0, 3, true},
        {118, 0.5400000000000009, 0.5400000000000009, false, false, 0, 0, 0, 0, 0, 3, true},
        {119, 0.5483333333333342, 0.5483333333333342, false, false, 0, 0, 0, 0, 0, 3, true},
        {120, 0.5566666666666675, 0.5566666666666675, false, false, 0, 0, 0, 0, 0, 3, true},
        {151, 0.815, 0.815, false, false, 0, 0, 0, 0, 0, 3, true},
        {152, 0.8233333333333333, 0.8233333333333333, true, false, 2, -1, 1, 0, 0, 3, true},
        {153, 0.8233333333333333, 0.8233333333333333, true, false, -1, 1, 0, 0, 0, 3, true},
        {154, 0.8233333333333333, 0.8233333333333333, true, false, -1, 1, 0, 0, 0, 3, true},
        {161, 0.8233333333333333, 0.8233333333333333, true, false, 0, 0, 0, 0, 0, 3, true},
        {162, 0.8233333333333333, 0.8233333333333333, false, false, 0, 0, 0, 0, 0, 3, true},
        {163, 0.8316666666666666, 0.8316666666666666, false, false, 0, 0, 0, 0, 0, 3, true},
        {164, 0.8399999999999999, 0.8399999999999999, false, false, 0, 0, 0, 0, 0, 3, true},
        {240, 1.473333333333331, 1.473333333333331, false, false, 0, 0, 0, 0, 0, 2, true},
        {360, 2.4733333333333274, 2.4733333333333274, false, false, 0, 0, 0, 0, 0, 4, true},
        {375, 2.598333333333327, 2.598333333333327, false, false, 0, 0, 0, 0, 0, 4, true},
        {376, 2.6066666666666602, 2.6066666666666602, false, true, 0, 0, 0, 0, 0, 4, true},
        {377, 2.6149999999999936, 2.6149999999999936, false, true, 0, 0, 0, 0, 0, 4, true},
        {378, 2.623333333333327, 2.623333333333327, false, true, 0, 0, 0, 0, 0, 4, true},
        {480, 3.473333333333324, 3.473333333333324, false, true, 0, 0, 0, 0, 0, 3, true},
        {495, 3.5983333333333234, 3.5983333333333234, false, true, 0, 0, 0, 0, 0, 3, true},
    };
    check(PixelFxKind::PistolShots, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.SwordCombo.source") {
    const Frame frames[] = {
        {0, -0.55, 0, false, false, 0, 0, 0, -8, 0, 3, true},
        {1, -0.5416666666666667, 0, false, false, 0, 0, 0, -8, 0, 3, true},
        {2, -0.5333333333333334, 0, false, false, 0, 0, 0, -8, 0, 3, true},
        {65, -0.008333333333332858, 0, false, false, 0, 0, 0, -8, 0, 5, true},
        {66, 4.753142324176451e-16, 4.753142324176451e-16, false, false, 0, 0, 0, 0, 0, 2, true},
        {67, 0.008333333333333809, 0.008333333333333809, false, false, 0, 0, 0, 0, 0, 2, true},
        {68, 0.016666666666667142, 0.016666666666667142, false, false, 0, 0, 0, 0, 0, 2, true},
        {73, 0.0583333333333338, 0.0583333333333338, false, false, 0, 0, 0, 0, 0, 2, true},
        {74, 0.06666666666666714, 0.06666666666666714, true, false, -2, 1, 1, 2.0022719478738003, 0, 5, true},
        {75, 0.06666666666666714, 0.06666666666666714, true, false, -2, 1, 0, 2.0022719478738003, 0, 5, true},
        {76, 0.06666666666666714, 0.06666666666666714, true, false, 1, 0, 0, 2.0022719478738003, 0, 5, true},
        {90, 0.06666666666666714, 0.06666666666666714, true, false, -1, 0, 0, 2.0022719478738003, 0, 5, true},
        {91, 0.06666666666666714, 0.06666666666666714, false, false, 0, 0, 0, 2.0022719478738003, 0, 5, true},
        {92, 0.07500000000000047, 0.07500000000000047, false, false, 0, 0, 0, 2.011230468750001, 0, 5, true},
        {93, 0.0833333333333338, 0.0833333333333338, false, false, 0, 0, 0, 2.0265185613854606, 0, 5, true},
        {120, 0.30833333333333396, 0.30833333333333396, false, false, 0, 0, 0, 3, 0, 6, true},
        {150, 0.5583333333333343, 0.5583333333333343, false, false, 0, 0, 0, 10, 0, 2, true},
        {151, 0.5666666666666677, 0.5666666666666677, true, false, 0, 0, 1, 13.019675925925931, 0, 6, true},
        {152, 0.5666666666666677, 0.5666666666666677, true, false, 0, 0, 0, 13.019675925925931, 0, 6, true},
        {153, 0.5666666666666677, 0.5666666666666677, true, false, 1, 0, 0, 13.019675925925931, 0, 6, true},
        {157, 0.5666666666666677, 0.5666666666666677, true, false, 0, 0, 0, 13.019675925925931, 0, 6, true},
        {158, 0.5666666666666677, 0.5666666666666677, false, false, 0, 0, 0, 13.019675925925931, 0, 6, true},
        {159, 0.575000000000001, 0.575000000000001, false, false, 0, 0, 0, 13.09228515625001, 0, 6, true},
        {160, 0.5833333333333343, 0.5833333333333343, false, false, 0, 0, 0, 13.205584490740755, 0, 6, true},
        {169, 0.658333333333334, 0.658333333333334, false, false, 0, 0, 0, 14, 0, 6, true},
        {170, 0.6666666666666673, 0.6666666666666673, true, false, -1, 1, 1, 14, 0, 6, true},
        {171, 0.6666666666666673, 0.6666666666666673, true, false, 2, -1, 0, 14, 0, 6, true},
        {172, 0.6666666666666673, 0.6666666666666673, true, false, 2, -1, 0, 14, 0, 6, true},
        {186, 0.6666666666666673, 0.6666666666666673, true, false, 0, 0, 0, 14, 0, 6, true},
        {187, 0.6666666666666673, 0.6666666666666673, false, false, 0, 0, 0, 14, 0, 6, true},
        {188, 0.6750000000000006, 0.6750000000000006, false, false, 0, 0, 0, 15, 0, 3, true},
        {189, 0.6833333333333339, 0.6833333333333339, false, false, 0, 0, 0, 15, 0, 3, true},
        {240, 1.1083333333333325, 1.1083333333333325, false, false, 0, 0, 0, 16, 0, 3.468768084490741, true},
        {272, 1.3749999999999982, 1.3749999999999982, false, false, 0, 0, 0, 20, -1, 3, true},
        {273, 1.3833333333333315, 1.3833333333333315, true, false, -2, 0, 1, 20, 0, 7, true},
        {274, 1.3833333333333315, 1.3833333333333315, true, false, 3, 1, 0, 20, 0, 7, true},
        {275, 1.3833333333333315, 1.3833333333333315, true, false, 3, 1, 0, 20, 0, 7, true},
        {297, 1.3833333333333315, 1.3833333333333315, true, false, 0, 0, 0, 20, 0, 7, true},
        {298, 1.3833333333333315, 1.3833333333333315, false, false, -1, 1, 0, 20, 0, 7, true},
        {299, 1.3916666666666648, 1.3916666666666648, false, false, -1, 1, 0, 20, 0, 7, true},
        {300, 1.3999999999999981, 1.3999999999999981, false, false, 1, -1, 0, 20, 0, 7, true},
        {360, 1.8999999999999964, 1.8999999999999964, false, false, 0, 0, 0, 19.997080932784637, 0, 6, true},
        {480, 2.899999999999993, 2.899999999999993, false, false, 0, 0, 0, 16, 0, 3, true},
        {489, 2.9749999999999925, 2.9749999999999925, false, false, 0, 0, 0, 16, 0, 3, true},
        {490, 2.983333333333326, 2.983333333333326, false, true, 0, 0, 0, 16, 0, 3, true},
        {491, 2.991666666666659, 2.991666666666659, false, true, 0, 0, 0, 16, 0, 3, true},
        {492, 2.9999999999999925, 2.9999999999999925, false, true, 0, 0, 0, 16, 0, 3, true},
        {600, 3.8999999999999893, 3.8999999999999893, false, true, 0, 0, 0, 16, 0, 4, true},
        {607, 3.9583333333333224, 3.9583333333333224, false, true, 0, 0, 0, 16, 0, 4, true},
    };
    check(PixelFxKind::SwordCombo, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.Uppercut.source") {
    const Frame frames[] = {
        {0, -0.42, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {1, -0.4116666666666666, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {2, -0.40333333333333327, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {50, -0.0033333333333328517, 0, false, false, 0, 0, 0, 0, 0, 7.5312319155092595, true},
        {51, 0.0050000000000004815, 0.0050000000000004815, false, false, 0, 0, 0, 0, 0, 8, true},
        {52, 0.013333333333333815, 0.013333333333333815, false, false, 0, 0, 0, 0, 0, 8, true},
        {53, 0.021666666666667146, 0.021666666666667146, false, false, 0, 0, 0, 0, 0, 8, true},
        {59, 0.07166666666666714, 0.07166666666666714, false, false, 0, 0, 0, 0, -4, 1, true},
        {60, 0.08000000000000047, 0.08000000000000047, true, false, -2, 1, 1, 1, -11.001890488338192, -1, true},
        {61, 0.08000000000000047, 0.08000000000000047, true, false, -2, 1, 0, 1, -11.001890488338192, -1, true},
        {62, 0.08000000000000047, 0.08000000000000047, true, false, 1, 0, 0, 1, -11.001890488338192, -1, true},
        {75, 0.08000000000000047, 0.08000000000000047, true, false, 0, 0, 0, 1, -11.001890488338192, -1, true},
        {76, 0.08000000000000047, 0.08000000000000047, false, false, -1, 0, 0, 1, -11.001890488338192, -1, true},
        {77, 0.0883333333333338, 0.0883333333333338, false, false, 0, 0, 0, 1, -11.017521643919226, -1, true},
        {78, 0.09666666666666714, 0.09666666666666714, false, false, 0, 0, 0, 1, -11.047932125479162, -1, true},
        {120, 0.4466666666666677, 0.4466666666666677, false, false, 0, 0, 0, 1, -8.863247863247807, -0.6918145956607498,
         true},
        {184, 0.9799999999999995, 0.9799999999999995, false, false, 0, 0, 0, 0, 0, 3.1661617879725474, true},
        {185, 0.9883333333333328, 0.9883333333333328, false, true, 0, 0, 0, 0, 0, 3, true},
        {186, 0.9966666666666661, 0.9966666666666661, false, true, 0, 0, 0, 0, 0, 3, true},
        {187, 1.0049999999999994, 1.0049999999999994, false, true, 0, 0, 0, 0, 0, 4, true},
        {240, 1.4466666666666645, 1.4466666666666645, false, true, 0, 0, 0, 0, 0, 3, true},
        {304, 1.979999999999996, 1.979999999999996, false, true, 0, 0, 0, 0, 0, 4, true},
    };
    check(PixelFxKind::Uppercut, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.Stunned.source") {
    const Frame frames[] = {
        {0, 0, 0, false, false, 0, 0, 0, -1, 0, 0, true},
        {1, 0.008333333333333333, 0.008333333333333333, false, false, 0, 0, 0, -0.999175925925926, 0, 0, true},
        {2, 0.016666666666666666, 0.016666666666666666, false, false, 0, 0, 0, -0.9967407407407407, 0, 0, true},
        {120, 0.9999999999999989, 0.9999999999999989, false, false, 0, 0, 0, 1, 0, 0, true},
        {240, 1.9999999999999953, 1.9999999999999953, false, false, 0, 0, 0, -1, 0, 0, true},
        {360, 2.999999999999992, 2.999999999999992, false, false, 0, 0, 0, 1, 0, 0, true},
        {480, 3.9999999999999885, 3.9999999999999885, false, false, 0, 0, 0, -1, 0, 0, true},
        {600, 5.000000000000038, 5.000000000000038, false, false, 0, 0, 0, 1, 0, 0, true},
    };
    check(PixelFxKind::Stunned, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.Electrified.source") {
    const Frame frames[] = {
        {0, 0, 0, false, false, 0, 0, 0, 0, 0, 3, true},
        {1, 0.008333333333333333, 0.008333333333333333, false, false, 0, 0, 0, 0, 0, 3, true},
        {2, 0.016666666666666666, 0.016666666666666666, false, false, 0, 0, 0, 0, 0, 3, true},
        {120, 0.9999999999999989, 0.9999999999999989, false, false, 0, 0, 0, 0, 0, 3, true},
        {240, 1.9999999999999953, 1.9999999999999953, false, false, 0, 0, 0, 0, 0, 3, true},
        {360, 2.999999999999992, 2.999999999999992, false, false, 0, 0, 0, 0, 0, 3, true},
        {480, 3.9999999999999885, 3.9999999999999885, false, false, 0, 0, 0, 0, 0, 3, true},
        {600, 5.000000000000038, 5.000000000000038, false, false, 0, 0, 0, 0, 0, 3, true},
    };
    check(PixelFxKind::Electrified, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.Torch.source") {
    const Frame frames[] = {
        {0, 0, 0, false, false, 0, 0, 0, 0, 0, 0, true},
        {1, 0.008333333333333333, 0.008333333333333333, false, false, 0, 0, 0, 0, 0, 0, true},
        {2, 0.016666666666666666, 0.016666666666666666, false, false, 0, 0, 0, 0, 0, 0, true},
        {120, 0.9999999999999989, 0.9999999999999989, false, false, 0, 0, 0, 0, 0, 0, true},
        {240, 1.9999999999999953, 1.9999999999999953, false, false, 0, 0, 0, 0, 0, 0, true},
        {360, 2.999999999999992, 2.999999999999992, false, false, 0, 0, 0, 0, 0, 0, true},
        {480, 3.9999999999999885, 3.9999999999999885, false, false, 0, 0, 0, 0, 0, 0, true},
        {600, 5.000000000000038, 5.000000000000038, false, false, 0, 0, 0, 0, 0, 0, true},
    };
    check(PixelFxKind::Torch, frames, _ZEROERR_TEST_CONTEXT);
}
TEST_CASE("stylize.pixelfx.actClock.validationDoesNotMutateOrPublish") {
    auto settings = PixelFx::defaults(PixelFxKind::ArrowShot);
    auto ready    = build(PixelFxKind::ArrowShot, settings);
    REQUIRE(ready);
    auto       act    = std::move(ready).takeValue();
    const auto before = act->snapshot();
    for (double delta :
         {-1., 1.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto rejected = act->step(delta);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().path() == "/act/delta");
        REQUIRE(act->snapshot().actTime == before.actTime);
        REQUIRE(act->snapshot().effectTime == 0);
    }
    settings.playbackSpeed = 0;
    REQUIRE(!build(PixelFxKind::ArrowShot, settings));
    REQUIRE(!build(static_cast<PixelFxKind>(10000), PixelFx::defaults(PixelFxKind::ArrowShot)));
}
TEST_CASE("stylize.pixelfx.actClock.sourceChainOwnsFadeAfterParentStop") {
    PixelFx fx(PixelFxKind::ArrowShot);
    fx.stop();
    REQUIRE(fx.advance(.6));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    REQUIRE(fx.advance(2));
    fx.draw();
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c == 0; }));
}
uint32_t checksum(std::span<const uint32_t> values) {
    uint32_t hash = 2166136261u;
    for (auto color : values)
        for (int shift = 0; shift < 32; shift += 8) hash = (hash ^ ((color >> shift) & 255u)) * 16777619u;
    return hash;
}
TEST_CASE("stylize.pixelfx.actClock.render.ArrowShot.fixed") {
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1256395589u, 1256395589u},   {48, 1256395589u, 1256395589u},  {96, 1256395589u, 1256395589u},
        {144, 685881972u, 1256395589u},  {192, 1798420535u, 777618866u},  {240, 1256395589u, 1256395589u},
        {288, 1256395589u, 1256395589u}, {336, 1256395589u, 1256395589u}, {384, 1256395589u, 1256395589u},
    };
    auto prepared = build(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(prepared);
    auto act = std::move(prepared).takeValue();

    const auto            view = act->info();
    std::vector<uint32_t> color(size_t(view.width) * view.height), light(color.size());
    size_t                tick = 0;
    for (const auto& frame : frames) {
        while (tick < frame.tick) {
            const double deltas[] = {0, 1.0 / 60, 1.0 / 240, .013, 1.0 / 120};
            const double delta    = false ? deltas[tick % 5] : 1.0 / 120;
            REQUIRE(act->step(delta));
            ++tick;
        }
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        PixelFxCanvas canvas{view.width, view.height, view.originX, view.originY, color, light};
        REQUIRE(act->drawEffectLayers(canvas, PixelFxLayer::Both, {}));
        REQUIRE(checksum(color) == frame.color);
        REQUIRE(checksum(light) == frame.light);
    }
}
TEST_CASE("stylize.pixelfx.actClock.render.ArrowShot.stopped") {
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1256395589u, 1256395589u},   {48, 1256395589u, 1256395589u},  {96, 1256395589u, 1256395589u},
        {144, 685881972u, 1256395589u},  {192, 1798420535u, 777618866u},  {240, 1256395589u, 1256395589u},
        {288, 1256395589u, 1256395589u}, {336, 1256395589u, 1256395589u}, {384, 1256395589u, 1256395589u},
    };
    auto prepared = build(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(prepared);
    auto act = std::move(prepared).takeValue();
    act->stop();
    act->stop();
    const auto            view = act->info();
    std::vector<uint32_t> color(size_t(view.width) * view.height), light(color.size());
    size_t                tick = 0;
    for (const auto& frame : frames) {
        while (tick < frame.tick) {
            const double deltas[] = {0, 1.0 / 60, 1.0 / 240, .013, 1.0 / 120};
            const double delta    = false ? deltas[tick % 5] : 1.0 / 120;
            REQUIRE(act->step(delta));
            ++tick;
        }
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        PixelFxCanvas canvas{view.width, view.height, view.originX, view.originY, color, light};
        REQUIRE(act->drawEffectLayers(canvas, PixelFxLayer::Both, {}));
        REQUIRE(checksum(color) == frame.color);
        REQUIRE(checksum(light) == frame.light);
    }
}
TEST_CASE("stylize.pixelfx.actClock.render.FireballThrow.variable") {
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1385631429u, 1385631429u},   {48, 1385631429u, 1385631429u},  {96, 904887561u, 3865941897u},
        {144, 4158305289u, 3364332055u}, {192, 4268012348u, 1888718176u}, {240, 1346797037u, 2612005848u},
        {288, 1810985818u, 1615520922u}, {336, 1613217908u, 2235861117u}, {384, 2984881383u, 3716051615u},
        {432, 3507763174u, 1510153758u}, {480, 3299467433u, 1051251826u}, {528, 733214365u, 3897177115u},
        {576, 156285677u, 1385631429u},  {624, 1017241752u, 1385631429u}, {672, 581988649u, 1385631429u},
        {720, 581988649u, 1385631429u},  {768, 2327629920u, 1385631429u}, {816, 1238569612u, 1385631429u},
        {864, 1385631429u, 1385631429u}, {912, 1385631429u, 1385631429u}, {960, 1385631429u, 1385631429u},
        {998, 1385631429u, 1385631429u},
    };
    auto prepared = build(PixelFxKind::FireballThrow, PixelFx::defaults(PixelFxKind::FireballThrow));
    REQUIRE(prepared);
    auto act = std::move(prepared).takeValue();

    const auto            view = act->info();
    std::vector<uint32_t> color(size_t(view.width) * view.height), light(color.size());
    size_t                tick = 0;
    for (const auto& frame : frames) {
        while (tick < frame.tick) {
            const double deltas[] = {0, 1.0 / 60, 1.0 / 240, .013, 1.0 / 120};
            const double delta    = true ? deltas[tick % 5] : 1.0 / 120;
            REQUIRE(act->step(delta));
            ++tick;
        }
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        PixelFxCanvas canvas{view.width, view.height, view.originX, view.originY, color, light};
        REQUIRE(act->drawEffectLayers(canvas, PixelFxLayer::Both, {}));
        REQUIRE(checksum(color) == frame.color);
        REQUIRE(checksum(light) == frame.light);
    }
}
TEST_CASE("stylize.pixelfx.actClock.render.FireballThrow.stopped") {
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1385631429u, 1385631429u},    {48, 1385631429u, 1385631429u},  {96, 1154594385u, 3953658418u},
        {144, 3444065036u, 2579337520u},  {192, 989070759u, 391270119u},   {240, 2582441173u, 4166450948u},
        {288, 1592575700u, 1148139948u},  {336, 2891372201u, 1649150236u}, {384, 792111417u, 3259508640u},
        {432, 401655907u, 2711587808u},   {480, 2570817203u, 2839303830u}, {528, 3517961760u, 3762861685u},
        {576, 2887431292u, 1385631429u},  {624, 3009365873u, 1385631429u}, {672, 581988649u, 1385631429u},
        {720, 581988649u, 1385631429u},   {768, 581988649u, 1385631429u},  {816, 3176496109u, 1385631429u},
        {864, 1385631429u, 1385631429u},  {912, 1385631429u, 1385631429u}, {960, 1385631429u, 1385631429u},
        {1008, 1385631429u, 1385631429u},
    };
    auto prepared = build(PixelFxKind::FireballThrow, PixelFx::defaults(PixelFxKind::FireballThrow));
    REQUIRE(prepared);
    auto act = std::move(prepared).takeValue();
    act->stop();
    act->stop();
    const auto            view = act->info();
    std::vector<uint32_t> color(size_t(view.width) * view.height), light(color.size());
    size_t                tick = 0;
    for (const auto& frame : frames) {
        while (tick < frame.tick) {
            const double deltas[] = {0, 1.0 / 60, 1.0 / 240, .013, 1.0 / 120};
            const double delta    = false ? deltas[tick % 5] : 1.0 / 120;
            REQUIRE(act->step(delta));
            ++tick;
        }
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        PixelFxCanvas canvas{view.width, view.height, view.originX, view.originY, color, light};
        REQUIRE(act->drawEffectLayers(canvas, PixelFxLayer::Both, {}));
        REQUIRE(checksum(color) == frame.color);
        REQUIRE(checksum(light) == frame.light);
    }
}
TEST_CASE("stylize.pixelfx.actClock.preparedOwnershipPublishesOnce") {
    auto result = ActRuntime::prepare(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(result);
    auto token     = std::move(result).takeValue();
    auto published = ActRuntime::publish(std::move(token));
    REQUIRE(published);
    auto consumed = ActRuntime::publish(std::move(token));
    REQUIRE(!consumed);
    REQUIRE(consumed.status().diagnostics().front().path() == "/act/candidate");
    REQUIRE(published.value()->step(1.0 / 120));
}
// Pristine pinned createAct goldens: source target/straw snapshots, never native-generated expected values.
struct TargetFrame {
    size_t        tick, index, count;
    uint32_t      random;
    ActTargetSkin skin;
    double        theta, omega, flash, shiver, clock, x, y, vx, vy, age, life;
    bool          big;
};
TEST_CASE("stylize.pixelfx.actClock.targetSourceFixtures") {
    {
        auto settings = PixelFx::defaults(PixelFxKind::ArrowShot);
        settings.seed = 1u;
        auto ready    = build(PixelFxKind::ArrowShot, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 5u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {75, 0, 14, 3399790543u, ActTargetSkin::None, 0.03458888888888889, 2.0753333333333335, 0.03333333333333334,
             0, 1.2500000000000004, 183.04948395087072, -31.459908969308405, 84.78145866654813, -106.70922185736708,
             0.016666666666666666, 1.7921649402007462, false},
            {78, 0, 14, 3399790543u, ActTargetSkin::None, 0.03458888888888889, 2.0753333333333335, 0.03333333333333334,
             0, 1.2500000000000004, 183.04948395087072, -31.459908969308405, 84.78145866654813, -106.70922185736708,
             0.016666666666666666, 1.7921649402007462, false},
            {201, 0, 2, 3399790543u, ActTargetSkin::None, 0.0009296565848298727, -0.06341485962056825,
             -0.01666666666666666, 0, 3.26666666666666, 136.87283720172127, 0, -3.140644270206437e-45, 0,
             2.033333333333331, 2.185737391281873, true},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step((void(pattern), 1.0 / 60)));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 1);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
    {
        auto settings = PixelFx::defaults(PixelFxKind::ArrowShot);
        settings.seed = 4294967292u;
        auto ready    = build(PixelFxKind::ArrowShot, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {48, 0, 14, 3399790539u, ActTargetSkin::None, 0.1452, 1.452, -0.05, 0, 1.2530000000000001,
             189.78902593674138, -26.59897193077486, 127.72611609660089, 12.363458294654265, 0.1, 1.481103502959013,
             false},
            {51, 0, 14, 3399790539u, ActTargetSkin::None, 0.1452, 1.452, -0.05, 0, 1.2530000000000001,
             189.78902593674138, -26.59897193077486, 127.72611609660089, 12.363458294654265, 0.1, 1.481103502959013,
             false},
            {133, 0, 0, 3399790539u, ActTargetSkin::None, -0.0018309092830390231, 0.024539228032451368, -0.05, 0,
             3.295666666666668, 0, 0, 0, 0, 0, 0, false},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step(pattern[tick % 5]));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 1);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
    {
        auto settings = PixelFx::defaults(PixelFxKind::FireballThrow);
        settings.seed = 1u;
        auto ready    = build(PixelFxKind::FireballThrow, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 5u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {110, 0, 34, 2120966603u, ActTargetSkin::Char, 0.053455555555555555, 3.207333333333333, 0.03333333333333334,
             0, 1.8333333333333317, 193.04948395087072, -29.459908969308405, 84.78145866654813, -106.70922185736708,
             0.016666666666666666, 1.7921649402007462, false},
            {113, 0, 34, 2120966603u, ActTargetSkin::Char, 0.053455555555555555, 3.207333333333333, 0.03333333333333334,
             0, 1.8333333333333317, 193.04948395087072, -29.459908969308405, 84.78145866654813, -106.70922185736708,
             0.016666666666666666, 1.7921649402007462, false},
            {409, 0, 0, 2120966603u, ActTargetSkin::None, -6.540561280663796e-05, 0.0006178840596795129, 0, 0,
             6.633333333333315, 0, 0, 0, 0, 0, 0, false},
            {412, 0, 0, 2120966603u, ActTargetSkin::None, -3.230044510129525e-05, 0.0006718922224173947, 0, 0,
             6.683333333333315, 0, 0, 0, 0, 0, 0, false},
            {494, 0, 0, 2120966603u, ActTargetSkin::None, -4.913837093958998e-06, -4.246792642400953e-05, 0, 0,
             8.04999999999998, 0, 0, 0, 0, 0, 0, false},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step((void(pattern), 1.0 / 60)));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 1);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
    {
        auto settings = PixelFx::defaults(PixelFxKind::FireballThrow);
        settings.seed = 4294967292u;
        auto ready    = build(PixelFxKind::FireballThrow, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {73, 0, 34, 2120966599u, ActTargetSkin::Char, 0.2244, 2.2439999999999998, -0.05, 0, 1.891333333333333,
             199.78902593674138, -24.59897193077486, 127.72611609660089, 12.363458294654265, 0.1, 1.481103502959013,
             false},
            {76, 0, 34, 2120966599u, ActTargetSkin::Char, 0.2244, 2.2439999999999998, -0.05, 0, 1.891333333333333,
             199.78902593674138, -24.59897193077486, 127.72611609660089, 12.363458294654265, 0.1, 1.481103502959013,
             false},
            {270, 0, 0, 2120966599u, ActTargetSkin::None, -1.1065510567349265e-05, -0.00020140503320556694, 0, 0,
             6.638666666666644, 0, 0, 0, 0, 0, 0, false},
            {273, 0, 0, 2120966599u, ActTargetSkin::None, -1.767649389686349e-05, -5.8269425206429915e-05, 0, 0,
             6.7426666666666435, 0, 0, 0, 0, 0, 0, false},
            {323, 0, 0, 2120966599u, ActTargetSkin::None, 4.517691656668393e-07, -1.2598595523003398e-05, 0, 0,
             8.019333333333298, 0, 0, 0, 0, 0, 0, false},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step(pattern[tick % 5]));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 1);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
    {
        auto settings = PixelFx::defaults(PixelFxKind::ChainLightning);
        settings.seed = 1u;
        auto ready    = build(PixelFxKind::ChainLightning, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 5u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {0, 1, 0, 6u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {0, 2, 0, 7u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {31, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.5166666666666666, 61.04948395087073, -25.459908969308405, 84.78145866654813,
             -106.70922185736708, 0.016666666666666666, 1.7921649402007462, false},
            {31, 1, 0, 6u, ActTargetSkin::None, 0, 0, 0, 0, 0.5166666666666666, 0, 0, 0, 0, 0, 0, false},
            {31, 2, 0, 7u, ActTargetSkin::None, 0, 0, 0, 0, 0.5166666666666666, 0, 0, 0, 0, 0, 0, false},
            {34, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.03634977777777777, 1.0489866666666665, 0.016666666666666673,
             0.6666666666666665, 0.5333333333333333, 62.46250826197986, -27.18006266693119, 84.78145866654813,
             -103.20922185736708, 0.03333333333333333, 1.7921649402007462, false},
            {34, 1, 0, 6u, ActTargetSkin::None, 0, 0, 0, 0, 0.5333333333333333, 0, 0, 0, 0, 0, 0, false},
            {34, 2, 0, 7u, ActTargetSkin::None, 0, 0, 0, 0, 0.5333333333333333, 0, 0, 0, 0, 0, 0, false},
            {39, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.09692144329462964, 0.4890417997641263, -0.01666666666666666,
             0.5833333333333329, 0.6166666666666669, 69.52762981752555, -34.905831155045114, 84.78145866654813,
             -85.70922185736708, 0.11666666666666665, 1.7921649402007462, false},
            {39, 1, 6, 3911320120u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.6166666666666669, 101.58505334813769, -21.923575588168266, 34.259524112567306,
             -54.15624703140929, 0.016666666666666666, 1.2707638685125857, true},
            {39, 2, 0, 7u, ActTargetSkin::None, 0, 0, 0, 0, 0.6166666666666669, 0, 0, 0, 0, 0, 0, false},
            {42, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.1029949097582329, 0.3644079878161962, -0.01666666666666666,
             0.5666666666666662, 0.6333333333333336, 70.94065412863469, -36.2759848526679, 84.78145866654813,
             -82.20922185736708, 0.13333333333333333, 1.7921649402007462, false},
            {42, 1, 6, 3911320120u, ActTargetSkin::Shock, 0.03634977777777777, 1.0489866666666665, 0.016666666666666673,
             0.6666666666666665, 0.6333333333333336, 102.15604541668048, -22.767846372025087, 34.259524112567306,
             -50.65624703140929, 0.03333333333333333, 1.2707638685125857, true},
            {42, 2, 0, 7u, ActTargetSkin::None, 0, 0, 0, 0, 0.6333333333333336, 0, 0, 0, 0, 0, 0, false},
            {46, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.10733064612358922, -0.10502805078718534,
             -0.01666666666666666, 0.4999999999999994, 0.7000000000000005, 76.59275137307125, -41.1732663098257,
             84.78145866654813, -68.20922185736708, 0.19999999999999998, 1.7921649402007462, false},
            {46, 1, 6, 3911320120u, ActTargetSkin::Shock, 0.0887707466318942, 0.6125221339887144, -0.01666666666666666,
             0.5999999999999996, 0.7000000000000005, 104.44001369085166, -25.561596174119035, 34.259524112567306,
             -36.65624703140929, 0.09999999999999999, 1.2707638685125857, true},
            {46, 2, 6, 3911320121u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.7000000000000005, 147.01252585901725, -20.080784105303852, -21.55342118581757,
             -56.533794118789956, 0.016666666666666666, 1.605521688051522, true},
            {49, 0, 6, 3911320119u, ActTargetSkin::Shock, 0.10733064612358922, -0.10502805078718534,
             -0.01666666666666666, 0.4999999999999994, 0.7000000000000005, 76.59275137307125, -41.1732663098257,
             84.78145866654813, -68.20922185736708, 0.19999999999999998, 1.7921649402007462, false},
            {49, 1, 6, 3911320120u, ActTargetSkin::Shock, 0.0887707466318942, 0.6125221339887144, -0.01666666666666666,
             0.5999999999999996, 0.7000000000000005, 104.44001369085166, -25.561596174119035, 34.259524112567306,
             -36.65624703140929, 0.09999999999999999, 1.2707638685125857, true},
            {49, 2, 6, 3911320121u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.7000000000000005, 147.01252585901725, -20.080784105303852, -21.55342118581757,
             -56.533794118789956, 0.016666666666666666, 1.605521688051522, true},
            {118, 0, 6, 3911320119u, ActTargetSkin::None, -0.009460567410091792, -0.08179543166064587, 0,
             -5.065392549852277e-16, 1.816666666666665, 164.55340169766995, 0, 0.005426013354659082, 0,
             1.3166666666666669, 1.7921649402007462, false},
            {118, 1, 6, 3911320120u, ActTargetSkin::None, 0.0017339282926607546, -0.14426322621589507, 0,
             -5.065392549852277e-16, 1.816666666666665, 128.5644285867847, 0, 2.2991177443750023e-17, 0,
             1.2166666666666672, 1.2707638685125857, true},
            {118, 2, 6, 3911320121u, ActTargetSkin::None, 0.014060388916562751, -0.14040141738332776, 0,
             -5.065392549852277e-16, 1.816666666666665, 130.03920667518634, 0, -4.520080034667973e-14, 0,
             1.1333333333333342, 1.605521688051522, true},
            {121, 0, 6, 3911320119u, ActTargetSkin::None, -0.012123121292158347, -0.038778872005767615, 0,
             -5.065392549852277e-16, 1.866666666666665, 164.55351383527926, 0, 4.340810683727266e-05, 0,
             1.3666666666666667, 1.7921649402007462, false},
            {121, 1, 5, 3911320120u, ActTargetSkin::None, -0.004710481587870453, -0.11939315197269848, 0,
             -5.065392549852277e-16, 1.866666666666665, 128.5644285867847, 0, 1.8392941955000023e-19, 0,
             1.266666666666667, 1.2707638685125857, true},
            {121, 2, 6, 3911320121u, ActTargetSkin::None, 0.00661402053441512, -0.1504540152879347, 0,
             -5.065392549852277e-16, 1.866666666666665, 130.03920667518634, 0, -3.616064027734379e-16, 0,
             1.183333333333334, 1.605521688051522, true},
            {196, 0, 0, 3911320119u, ActTargetSkin::None, 0.0014656184193750131, 0.0024350765695656156, 0,
             -5.065392549852277e-16, 3.1166666666666605, 0, 0, 0, 0, 0, 0, false},
            {196, 1, 0, 3911320120u, ActTargetSkin::None, 0.0008059941008939108, 0.012200199887927027, 0,
             -5.065392549852277e-16, 3.1166666666666605, 0, 0, 0, 0, 0, 0, false},
            {196, 2, 0, 3911320121u, ActTargetSkin::None, -0.0004105292620686649, 0.01718387651997124, 0,
             -5.065392549852277e-16, 3.1166666666666605, 0, 0, 0, 0, 0, 0, false},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step((void(pattern), 1.0 / 60)));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 3);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
    {
        auto settings = PixelFx::defaults(PixelFxKind::ChainLightning);
        settings.seed = 4294967292u;
        auto ready    = build(PixelFxKind::ChainLightning, settings);
        REQUIRE(ready);
        auto              act        = std::move(ready).takeValue();
        const TargetFrame fixtures[] = {
            {0, 0, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {0, 1, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {0, 2, 0, 2u, ActTargetSkin::None, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, false},
            {20, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.5106666666666668, 57.145182928691305, -21.920926788662715, 127.72611609660089,
             -5.136541705345735, 0.016666666666666666, 1.481103502959013, false},
            {20, 1, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0.5106666666666668, 0, 0, 0, 0, 0, 0, false},
            {20, 2, 0, 2u, ActTargetSkin::None, 0, 0, 0, 0, 0.5106666666666668, 0, 0, 0, 0, 0, 0, false},
            {23, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.018866666666666664, 1.132, 0.03333333333333334,
             0.6833333333333332, 0.5106666666666668, 57.145182928691305, -21.920926788662715, 127.72611609660089,
             -5.136541705345735, 0.016666666666666666, 1.481103502959013, false},
            {23, 1, 0, 1u, ActTargetSkin::None, 0, 0, 0, 0, 0.5106666666666668, 0, 0, 0, 0, 0, 0, false},
            {23, 2, 0, 2u, ActTargetSkin::None, 0, 0, 0, 0, 0.5106666666666668, 0, 0, 0, 0, 0, 0, false},
            {28, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.08387993435874848, 0.3658918942154443, -0.09433333333333332,
             0.5556666666666665, 0.6383333333333335, 73.45155041702401, -19.779328613045188, 127.72611609660089,
             21.673458294654264, 0.14433333333333334, 1.481103502959013, false},
            {28, 1, 6, 3911320115u, ActTargetSkin::Shock, 0.07919999999999999, 0.7919999999999999, -0.05, 0.6,
             0.6383333333333335, 113.78902593674138, -20.59897193077486, 127.72611609660089, 12.363458294654265, 0.1,
             1.481103502959013, false},
            {28, 2, 0, 2u, ActTargetSkin::None, 0, 0, 0, 0, 0.6383333333333335, 0, 0, 0, 0, 0, 0, false},
            {31, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.08387993435874848, 0.3658918942154443, -0.09433333333333332,
             0.5556666666666665, 0.6383333333333335, 73.45155041702401, -19.779328613045188, 127.72611609660089,
             21.673458294654264, 0.14433333333333334, 1.481103502959013, false},
            {31, 1, 6, 3911320115u, ActTargetSkin::Shock, 0.07919999999999999, 0.7919999999999999, -0.05, 0.6,
             0.6383333333333335, 113.78902593674138, -20.59897193077486, 127.72611609660089, 12.363458294654265, 0.1,
             1.481103502959013, false},
            {31, 2, 0, 2u, ActTargetSkin::None, 0, 0, 0, 0, 0.6383333333333335, 0, 0, 0, 0, 0, 0, false},
            {38, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.04895582493887101, -0.41576361705080866,
             -0.09433333333333332, 0.4279999999999998, 0.7660000000000002, 89.75791790535672, -14.214987104094327,
             127.72611609660089, 48.48345829465426, 0.272, 1.481103502959013, false},
            {38, 1, 6, 3911320115u, ActTargetSkin::Shock, 0.07722126395846861, -0.19714400268659715, -0.05,
             0.47233333333333327, 0.7660000000000002, 130.0953934250741, -16.22320708849066, 127.72611609660089,
             39.17345829465427, 0.22766666666666668, 1.481103502959013, false},
            {38, 2, 6, 3911320116u, ActTargetSkin::Shock, 0.07919999999999999, 0.7919999999999999, -0.05, 0.6,
             0.7660000000000002, 156.40545248333365, -23.12241227782797, 74.55461889505386, 1.6581139063928276, 0.1,
             1.8308333419729026, true},
            {41, 0, 6, 3911320115u, ActTargetSkin::Shock, 0.04895582493887101, -0.41576361705080866,
             -0.09433333333333332, 0.4279999999999998, 0.7660000000000002, 89.75791790535672, -14.214987104094327,
             127.72611609660089, 48.48345829465426, 0.272, 1.481103502959013, false},
            {41, 1, 6, 3911320115u, ActTargetSkin::Shock, 0.07722126395846861, -0.19714400268659715, -0.05,
             0.47233333333333327, 0.7660000000000002, 130.0953934250741, -16.22320708849066, 127.72611609660089,
             39.17345829465427, 0.22766666666666668, 1.481103502959013, false},
            {41, 2, 6, 3911320116u, ActTargetSkin::Shock, 0.07919999999999999, 0.7919999999999999, -0.05, 0.6,
             0.7660000000000002, 156.40545248333365, -23.12241227782797, 74.55461889505386, 1.6581139063928276, 0.1,
             1.8308333419729026, true},
            {85, 0, 6, 3911320115u, ActTargetSkin::None, -0.008700021865017575, 0.007481424413612251, 0,
             -0.08266666666666692, 1.8109999999999997, 122.63961093805781, 0, 1.0971589829600017e-21, 0, 1.317,
             1.481103502959013, false},
            {85, 1, 6, 3911320115u, ActTargetSkin::None, -0.010766693689914371, -0.03040513394761981, 0,
             -0.03833333333333347, 1.8109999999999997, 162.97708645777524, 0, 1.0971589829600017e-21, 0,
             1.2726666666666666, 1.481103502959013, false},
            {85, 2, 6, 3911320116u, ActTargetSkin::None, -0.004634308323304491, -0.12266904111696371, 0,
             -0.03833333333333347, 1.8109999999999997, 194.6350038798143, 0, 6.254094725000008e-15, 0, 1.145,
             1.8308333419729026, true},
            {88, 0, 5, 3911320115u, ActTargetSkin::None, -0.0028399934232576944, 0.058221577126031154, 0,
             -0.08266666666666692, 1.9149999999999998, 122.63961093805781, 0, 8.777271863680014e-24, 0, 1.421,
             1.481103502959013, false},
            {88, 1, 5, 3911320115u, ActTargetSkin::None, -0.006159432428453592, 0.04716891732022189, 0,
             -0.03833333333333347, 1.9149999999999998, 162.97708645777524, 0, 8.777271863680014e-24, 0,
             1.3766666666666667, 1.481103502959013, false},
            {88, 2, 5, 3911320116u, ActTargetSkin::None, -0.00995818648094996, -0.04844324125004748, 0,
             -0.03833333333333347, 1.9149999999999998, 194.6350038798143, 0, 5.003275780000007e-17, 0, 1.249,
             1.8308333419729026, true},
            {128, 0, 0, 3911320115u, ActTargetSkin::None, 0.0008047379149293592, -0.002681344847238273, 0,
             -0.08266666666666692, 2.936333333333334, 0, 0, 0, 0, 0, 0, false},
            {128, 1, 0, 3911320115u, ActTargetSkin::None, 0.0010648105343766018, 0.0011108048681861115, 0,
             -0.03833333333333347, 2.936333333333334, 0, 0, 0, 0, 0, 0, false},
            {128, 2, 0, 3911320116u, ActTargetSkin::None, 0.0006487055449437808, 0.012714693284272797, 0,
             -0.03833333333333347, 2.936333333333334, 0, 0, 0, 0, 0, 0, false},
        };
        size_t tick = 0;
        for (const auto& f : fixtures) {
            while (tick < f.tick) {
                const double pattern[] = {0, .004, .1, .007, 1.0 / 60};
                REQUIRE(act->step(pattern[tick % 5]));
                ++tick;
            }
            REQUIRE(act->targetStates().size() == 3);
            const auto& target = act->targetStates()[f.index];
            REQUIRE(target.randomState == f.random);
            REQUIRE(target.skin == f.skin);
            REQUIRE(target.straw.size() == f.count);
            REQUIRE(std::abs(target.theta - f.theta) < 1e-11);
            REQUIRE(std::abs(target.omega - f.omega) < 1e-11);
            REQUIRE(std::abs(target.flashTime - f.flash) < 1e-11);
            REQUIRE(std::abs(target.shiver - f.shiver) < 1e-11);
            REQUIRE(std::abs(target.clock - f.clock) < 1e-11);
            if (!target.straw.empty()) {
                const auto& p = target.straw.front();
                REQUIRE(std::abs(p.x - f.x) < 1e-11);
                REQUIRE(std::abs(p.y - f.y) < 1e-11);
                REQUIRE(std::abs(p.vx - f.vx) < 1e-11);
                REQUIRE(std::abs(p.vy - f.vy) < 1e-11);
                REQUIRE(std::abs(p.age - f.age) < 1e-11);
                REQUIRE(std::abs(p.life - f.life) < 1e-11);
                REQUIRE(p.big == f.big);
            }
        }
    }
}


// Independent pristine-source whole-plane target composition fingerprints.
TEST_CASE("stylize.pixelfx.actClock.targetRender.ArrowClear") {
    auto settings = PixelFx::defaults(PixelFxKind::ArrowShot);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::ArrowShot, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2482363508u, 2282978757u}, {75, 2063266094u, 2766745515u},  {77, 2063266094u, 2766745515u},
        {90, 71108848u, 2657500701u},  {201, 2556851793u, 2282978757u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 300, 160, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        REQUIRE(act->drawWithoutBody(canvas, {0, 0, 0, false}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        const auto oldClock = act->targetStates().front().clock;
        const auto oldRng   = act->targetStates().front().randomState;
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        REQUIRE(act->drawWithoutBody(canvas, {0, 0, 0, false}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->targetStates().front().clock == oldClock);
        REQUIRE(act->targetStates().front().randomState == oldRng);
    }
}
TEST_CASE("stylize.pixelfx.actClock.targetRender.FireballMirror") {
    auto settings = PixelFx::defaults(PixelFxKind::FireballThrow);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::FireballThrow, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2507540053u, 2439515589u},   {110, 2932754444u, 77088345u},   {112, 2932754444u, 77088345u},
        {125, 1637875340u, 1641770826u}, {409, 3482145920u, 2439515589u}, {411, 2377220632u, 2439515589u},
        {424, 2462347613u, 2439515589u}, {494, 2507540053u, 2439515589u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 300, 160, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 4280299584u);
        std::fill(lights.begin(), lights.end(), 4286595136u);
        REQUIRE(act->drawWithoutBody(canvas, {3, -2, 0, true}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        const auto oldClock = act->targetStates().front().clock;
        const auto oldRng   = act->targetStates().front().randomState;
        std::fill(colors.begin(), colors.end(), 4280299584u);
        std::fill(lights.begin(), lights.end(), 4286595136u);
        REQUIRE(act->drawWithoutBody(canvas, {3, -2, 0, true}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->targetStates().front().clock == oldClock);
        REQUIRE(act->targetStates().front().randomState == oldRng);
    }
}
TEST_CASE("stylize.pixelfx.actClock.targetRender.ChainTargets") {
    auto settings = PixelFx::defaults(PixelFxKind::ChainLightning);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::ChainLightning, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 866304648u, 2282978757u},   {31, 2333713573u, 2289663388u},  {33, 2333713573u, 2289663388u},
        {39, 3477200677u, 1675133875u}, {41, 3477200677u, 1675133875u},  {46, 834225957u, 2785359814u},
        {48, 834225957u, 2785359814u},  {54, 3247110283u, 3000533604u},  {61, 4285660990u, 1916706041u},
        {118, 622683504u, 236416969u},  {120, 2799084054u, 1018770117u}, {133, 700098601u, 2282978757u},
        {196, 866304648u, 2282978757u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 300, 160, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        REQUIRE(act->drawWithoutBody(canvas, {0, 0, 0, false}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        const auto oldClock = act->targetStates().front().clock;
        const auto oldRng   = act->targetStates().front().randomState;
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        REQUIRE(act->drawWithoutBody(canvas, {0, 0, 0, false}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->targetStates().front().clock == oldClock);
        REQUIRE(act->targetStates().front().randomState == oldRng);
    }
}
TEST_CASE("stylize.pixelfx.actClock.targetRender.FractionalEdge") {
    auto settings = PixelFx::defaults(PixelFxKind::ChainLightning);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::ChainLightning, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1779067025u, 1851994904u},   {31, 3441246468u, 4153414711u},  {33, 3441246468u, 4153414711u},
        {39, 1950576403u, 872300274u},   {41, 1950576403u, 872300274u},   {46, 2827725483u, 3401720351u},
        {48, 2827725483u, 3401720351u},  {54, 1426088897u, 155094384u},   {61, 4077193356u, 1965442575u},
        {118, 3441009601u, 1851994904u}, {120, 2772499834u, 1851994904u}, {133, 1396255637u, 1851994904u},
        {196, 1779067025u, 1851994904u},
    };
    std::vector<uint32_t> colors(131 * 83), lights(colors.size());
    PixelFxCanvas         canvas{131, 83, 24, 84, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 4280299584u);
        std::fill(lights.begin(), lights.end(), 4286595136u);
        REQUIRE(act->drawWithoutBody(canvas, {120.5, -0.5, 0, true}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        const auto oldClock = act->targetStates().front().clock;
        const auto oldRng   = act->targetStates().front().randomState;
        std::fill(colors.begin(), colors.end(), 4280299584u);
        std::fill(lights.begin(), lights.end(), 4286595136u);
        REQUIRE(act->drawWithoutBody(canvas, {120.5, -0.5, 0, true}, true));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->targetStates().front().clock == oldClock);
        REQUIRE(act->targetStates().front().randomState == oldRng);
    }
}
TEST_CASE("stylize.pixelfx.actClock.targetRender.validationAndTargetToggle") {
    auto ready = build(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(ready);
    auto                  act = std::move(ready).takeValue();
    std::vector<uint32_t> color(96 * 72, 0xff123456u), light(color.size(), 0xff654321u);
    const auto            beforeColor = color, beforeLight = light;
    PixelFxCanvas         invalid{96, 72, 0, 0, color, color};
    auto                  rejected = act->drawWithoutBody(invalid, {}, true);
    REQUIRE(!rejected);
    REQUIRE(color == beforeColor);
    REQUIRE(light == beforeLight);
    PixelFxCanvas    canvas{96, 72, 20, 64, color, light};
    PixelFxPlacement invalidPlacement;
    invalidPlacement.x = std::numeric_limits<double>::infinity();
    REQUIRE(!act->drawWithoutBody(canvas, invalidPlacement, true));
    REQUIRE(color == beforeColor);
    REQUIRE(light == beforeLight);
    for (int frame = 0; frame < 2; ++frame) {
        if (frame)
            for (int tick = 0; tick < 90; ++tick) REQUIRE(act->step(1.0 / 60));
        color = beforeColor;
        light = beforeLight;
        REQUIRE(act->drawWithoutBody(canvas, {}, false));
        const auto noTargets = color, noTargetLight = light;
        color = beforeColor;
        light = beforeLight;
        REQUIRE(act->drawEffectLayers(canvas, PixelFxLayer::Both, {}));
        REQUIRE(color == noTargets);
        REQUIRE(light == noTargetLight);
    }
}
// Pinned createAct body:true Arrow shot whole-plane fingerprints; independent of native rendering.
TEST_CASE("stylize.pixelfx.actClock.preparedFrame.sourceArrow") {
    auto settings = PixelFx::defaults(PixelFxKind::ArrowShot);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::ArrowShot, settings);
    REQUIRE(ready);
    auto                  act = std::move(ready).takeValue();
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 300, 160, colors, lights};
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 154264139u, 2282978757u},    {24, 191924983u, 2282978757u},  {60, 293104841u, 2897964677u},
        {75, 1586980944u, 2766745515u},  {90, 2442417474u, 2657500701u}, {160, 4273446906u, 2282978757u},
        {201, 3742548538u, 2282978757u},
    };
    size_t tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 0);
        std::fill(lights.begin(), lights.end(), 0);
        const auto clock = act->snapshot().actTime, rng = double(act->targetStates().front().randomState);
        auto       prepared = act->prepareFrame(canvas, {}, true);
        REQUIRE(prepared);
        REQUIRE(std::all_of(colors.begin(), colors.end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(lights.begin(), lights.end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(prepared.value().copyTo(canvas));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->snapshot().actTime == clock);
        REQUIRE(double(act->targetStates().front().randomState) == rng);
    }
}
TEST_CASE("stylize.pixelfx.actClock.preparedFrame.ownershipAndValidation") {
    auto ready = build(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(ready);
    auto                  act = std::move(ready).takeValue();
    std::vector<uint32_t> colors(320 * 160, 0xff123456u), lights(colors.size(), 0xff654321u);
    PixelFxCanvas         canvas{320, 160, 80, 100, colors, lights};
    const auto            initial = colors;
    PixelFxPlacement      bad;
    bad.x         = std::numeric_limits<double>::infinity();
    auto rejected = act->prepareFrame(canvas, bad, true);
    REQUIRE(!rejected);
    REQUIRE(colors == initial);
    auto result = act->prepareFrame(canvas, {}, true);
    REQUIRE(result);
    auto token = std::move(result).takeValue();
    REQUIRE(act->step(.25));
    act.reset();
    auto wrong = canvas;
    wrong.originX += 1;
    auto mismatch = token.copyTo(wrong);
    REQUIRE(!mismatch);
    REQUIRE(colors == initial);
    REQUIRE(token.copyTo(canvas));
    const auto expected = colors, expectedLight = lights;
    auto       moved    = std::move(token);
    auto       consumed = token.copyTo(canvas);
    REQUIRE(!consumed);
    std::fill(colors.begin(), colors.end(), 0);
    std::fill(lights.begin(), lights.end(), 0);
    REQUIRE(moved.copyTo(canvas));
    REQUIRE(colors == expected);
    REQUIRE(lights == expectedLight);
}
TEST_CASE("stylize.pixelfx.actClock.rasterCacheSourceIdentity") {
    auto a = rig::standPose(), b = a;
    a.pose.crouch = 3.1;
    b.pose.crouch = 3.2;
    REQUIRE(rig::rasterCacheKey(a) == rig::rasterCacheKey(b));
    b.pose.crouch = 3.6;
    REQUIRE(!(rig::rasterCacheKey(a) == rig::rasterCacheKey(b)));
    a.presence |= uint64_t(1) << size_t(rig::PoseField::Sa);
    b         = a;
    b.pose.sa = .021;
    REQUIRE(rig::rasterCacheKey(a) == rig::rasterCacheKey(b));
    b.pose.sa = .026;
    REQUIRE(!(rig::rasterCacheKey(a) == rig::rasterCacheKey(b)));
    b = a;
    b.presence |= uint64_t(1) << size_t(rig::PoseField::Y);
    b.pose.x     = 300;
    b.pose.y     = -.5;
    b.pose.ghost = 1;
    REQUIRE(rig::rasterCacheKey(a) == rig::rasterCacheKey(b));
    b.pose.y = -.500001;
    REQUIRE(!(rig::rasterCacheKey(a) == rig::rasterCacheKey(b)));
    b = a;
    b.presence |= uint64_t(1) << size_t(rig::PoseField::ToeF);
    REQUIRE(!(rig::rasterCacheKey(a) == rig::rasterCacheKey(b)));
}
// Pristine createAct body:true, origin-zero onBody composition fingerprints.
TEST_CASE("stylize.pixelfx.actClock.onBody.Burning") {
    auto settings = PixelFx::defaults(PixelFxKind::Burning);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::Burning, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1841406608u, 2245569186u},   {12, 3809733766u, 3886239245u},  {24, 414435596u, 4265420919u},
        {36, 1282897141u, 3636082879u},  {48, 3972565253u, 3729623656u},  {60, 687360395u, 810396535u},
        {72, 1841406608u, 2245569186u},  {84, 3809733766u, 3886239245u},  {96, 414435596u, 4265420919u},
        {108, 325415442u, 2434138006u},  {120, 2295124826u, 1301307830u}, {132, 2712440116u, 810396535u},
        {144, 889001554u, 3079657471u},  {156, 3809733766u, 3886239245u}, {168, 414435596u, 4265420919u},
        {180, 325415442u, 2434138006u},  {192, 2295124826u, 1301307830u}, {204, 2712440116u, 810396535u},
        {205, 2012707343u, 2111208553u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 0, 0, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        const auto clock = act->snapshot().actTime;
        auto       frame = act->prepareFrame(canvas, {300, 160, 0, false}, true);
        REQUIRE(frame);
        REQUIRE(std::all_of(colors.begin(), colors.end(), [](uint32_t c) { return c == 0u; }));
        REQUIRE(std::all_of(lights.begin(), lights.end(), [](uint32_t c) { return c == 0u; }));
        REQUIRE(frame.value().copyTo(canvas));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->snapshot().actTime == clock);
    }
}

TEST_CASE("stylize.pixelfx.actClock.onBody.Thunderclap") {
    auto settings = PixelFx::defaults(PixelFxKind::Thunderclap);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::Thunderclap, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 3945320987u, 2439515589u},   {12, 2507139101u, 2439515589u},  {24, 481346969u, 2439515589u},
        {35, 2896234125u, 996563872u},   {36, 2896234125u, 996563872u},   {38, 2896234125u, 996563872u},
        {45, 3168104195u, 1707810970u},  {48, 3925116251u, 3912981788u},  {60, 632433339u, 1945046915u},
        {61, 3247479184u, 809655582u},   {62, 21503244u, 692219931u},     {64, 1633535621u, 3815886790u},
        {71, 2311372610u, 2188552542u},  {72, 1201054929u, 1980596754u},  {84, 1987146396u, 1928583442u},
        {96, 3560512894u, 518208061u},   {108, 2688942684u, 2439515589u}, {109, 284798887u, 2439515589u},
        {111, 1051955016u, 2439515589u}, {118, 2740832677u, 2439515589u}, {120, 3016418120u, 2439515589u},
        {132, 2633168576u, 2439515589u}, {144, 2024152699u, 2439515589u}, {156, 3768538523u, 2439515589u},
        {168, 3103268635u, 2439515589u}, {180, 792832751u, 2439515589u},  {192, 4200540767u, 2439515589u},
        {202, 3945320987u, 2439515589u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 0, 0, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 4280299584u);
        std::fill(lights.begin(), lights.end(), 4286595136u);
        const auto clock = act->snapshot().actTime;
        auto       frame = act->prepareFrame(canvas, {303, 158, 0, true}, true);
        REQUIRE(frame);
        REQUIRE(std::all_of(colors.begin(), colors.end(), [](uint32_t c) { return c == 4280299584u; }));
        REQUIRE(std::all_of(lights.begin(), lights.end(), [](uint32_t c) { return c == 4286595136u; }));
        REQUIRE(frame.value().copyTo(canvas));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->snapshot().actTime == clock);
    }
}

TEST_CASE("stylize.pixelfx.actClock.onBody.RespawnBeam") {
    auto settings = PixelFx::defaults(PixelFxKind::RespawnBeam);
    settings.seed = 1;
    auto ready    = build(PixelFxKind::RespawnBeam, settings);
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    struct Frame {
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 1489662853u, 2282978757u},   {12, 2043675737u, 726731195u},   {24, 1822310782u, 3278411483u},
        {36, 3342893366u, 187729772u},   {48, 1478995152u, 4288179271u},  {60, 1465386256u, 3258359866u},
        {72, 2309004162u, 410913981u},   {84, 3263754524u, 1244617356u},  {96, 1624190772u, 4110047017u},
        {108, 2118440341u, 60653265u},   {120, 952836329u, 1256308956u},  {132, 1217569079u, 922689387u},
        {144, 3897805455u, 2282978757u}, {156, 1595817970u, 2282978757u}, {168, 3897805455u, 2282978757u},
        {180, 3897805455u, 2282978757u}, {192, 1595817970u, 2282978757u}, {204, 3897805455u, 2282978757u},
        {211, 3897805455u, 2282978757u},
    };
    std::vector<uint32_t> colors(640 * 220), lights(colors.size());
    PixelFxCanvas         canvas{640, 220, 0, 0, colors, lights};
    size_t                tick = 0;
    for (const auto& f : frames) {
        while (tick < f.tick) {
            REQUIRE(act->step(1.0 / 60));
            ++tick;
        }
        std::fill(colors.begin(), colors.end(), 0u);
        std::fill(lights.begin(), lights.end(), 0u);
        const auto clock = act->snapshot().actTime;
        auto       frame = act->prepareFrame(canvas, {300, 160, 0, false}, true);
        REQUIRE(frame);
        REQUIRE(std::all_of(colors.begin(), colors.end(), [](uint32_t c) { return c == 0u; }));
        REQUIRE(std::all_of(lights.begin(), lights.end(), [](uint32_t c) { return c == 0u; }));
        REQUIRE(frame.value().copyTo(canvas));
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(act->snapshot().actTime == clock);
    }
}
// Pristine cross-Act source cache collision fingerprints and owner-lifetime contracts.
TEST_CASE("stylize.pixelfx.actClock.renderSession.sourceOrderAndIsolation") {
    struct Scenario {
        PixelFxKind warm, main;
        int         warmTicks, mainTicks;
        uint32_t    coldColor, warmColor, light;
    };
    const Scenario scenarios[] = {
        {PixelFxKind::BlinkOut, PixelFxKind::FireRing, 1, 167, 4198098185u, 3249075270u, 2221522201u},
        {PixelFxKind::FireRing, PixelFxKind::BlinkOut, 167, 1, 4072680020u, 2037293004u, 2282978757u},
    };
    for (const auto& scenario : scenarios)
        for (bool sharing : {false, true}) {
            auto session = std::make_shared<ActRenderSession>();
            auto create  = [&](PixelFxKind kind) -> eve::Result<std::unique_ptr<ActRuntime>> {
                auto settings  = PixelFx::defaults(kind);
                settings.seed  = 1;
                auto candidate = ActRuntime::prepareInSession(kind, settings, session);
                if (!candidate) return eve::Result<std::unique_ptr<ActRuntime>>::failure(candidate.status());
                return ActRuntime::publish(std::move(candidate).takeValue());
            };
            auto mainResult = create(scenario.main);
            REQUIRE(mainResult);
            auto                  main = std::move(mainResult).takeValue();
            std::vector<uint32_t> colors(640 * 220), lights(colors.size());
            PixelFxCanvas         canvas{640, 220, 0, 0, colors, lights};
            if (sharing) {
                auto warmResult = create(scenario.warm);
                REQUIRE(warmResult);
                auto warm = std::move(warmResult).takeValue();
                for (int i = 0; i < scenario.warmTicks; ++i) REQUIRE(warm->step(1.0 / 60));
                auto warmFrame = warm->prepareFrame(canvas, {300, 160, 0, false}, true);
                REQUIRE(warmFrame);
                REQUIRE(main->snapshot().wallTime == 0);
                REQUIRE(session->cacheStats().entries == 1);
            }
            for (int i = 0; i < scenario.mainTicks; ++i) REQUIRE(main->step(1.0 / 60));
            auto frame = main->prepareFrame(canvas, {300, 160, 0, false}, true);
            REQUIRE(frame);
            REQUIRE(frame.value().copyTo(canvas));
            REQUIRE(checksum(colors) == (sharing ? scenario.warmColor : scenario.coldColor));
            REQUIRE(checksum(lights) == scenario.light);
            REQUIRE(session->cacheStats().entries == 1);
            REQUIRE(session->cacheStats().rasterEvaluations == 1);
            REQUIRE(session->cacheStats().hits == (sharing ? 1u : 0u));
            REQUIRE(session->cacheStats().clears == 0);
        }
}
TEST_CASE("stylize.pixelfx.actClock.renderSession.ownershipAndRejectedPublication") {
    auto                            session = std::make_shared<ActRenderSession>();
    std::weak_ptr<ActRenderSession> weak    = session;
    auto                            candidate =
        ActRuntime::prepareInSession(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot), session);
    REQUIRE(candidate);
    session.reset();
    REQUIRE(!weak.expired());
    auto published = ActRuntime::publish(std::move(candidate).takeValue());
    REQUIRE(published);
    auto                  act = std::move(published).takeValue();
    std::vector<uint32_t> colors(96 * 72), lights(colors.size());
    PixelFxCanvas         canvas{96, 72, 20, 64, colors, lights};
    auto                  frame = act->prepareFrame(canvas, {}, true);
    REQUIRE(frame);
    REQUIRE(!weak.expired());
    act.reset();
    REQUIRE(weak.expired());
    REQUIRE(frame.value().copyTo(canvas));
    auto retained = std::make_shared<ActRenderSession>();
    {
        auto item =
            ActRuntime::prepareInSession(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot), retained);
        REQUIRE(item);
    }
    REQUIRE(retained->cacheStats().entries == 0);
    auto rejected = ActRuntime::prepareInSession(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot), {});
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/act/session");
    auto badSettings          = PixelFx::defaults(PixelFxKind::ArrowShot);
    badSettings.playbackSpeed = std::numeric_limits<double>::infinity();
    auto invalid              = ActRuntime::prepareInSession(PixelFxKind::ArrowShot, badSettings, retained);
    REQUIRE(!invalid);
    REQUIRE(retained->cacheStats().entries == 0);
    REQUIRE(retained->cacheStats().rasterEvaluations == 0);
}
// Independent pristine actTiming/actBounds records at the approved upstream pin.
TEST_CASE("stylize.pixelfx.actClock.sourceContract.all244") {
    struct Expected {
        const char* id;
        bool        hasAct;
        size_t      poses, hits, targets;
        double      windup, recovery;
        bool        onBody;
        ActBounds   bounds;
    };
    const Expected rows[] = {
        {"moves/arrow-shot", true, 8, 1, 1, 0.44, 0, false, {-38, -66, 262, 8}},
        {"moves/dash-strike", true, 15, 1, 1, 0.5, 0, false, {-66, -136, 306, 20}},
        {"moves/earth-stomp", true, 7, 2, 1, 0.5, 0, false, {-50, -128, 240, 14}},
        {"moves/fireball-throw", true, 11, 2, 1, 0.42, 0, false, {-64, -198, 292, 46}},
        {"moves/ground-pound", true, 12, 3, 1, 0.5, 0, false, {-152, -168, 152, 36}},
        {"moves/holy-smite", true, 7, 2, 1, 0.42, 0, false, {-42, -151, 210, 31}},
        {"moves/ice-lance-throw", true, 8, 2, 1, 0.4, 0, false, {-86, -134, 224, 2}},
        {"moves/lightning-call", true, 7, 4, 1, 0, 0, false, {-42, -231, 248, 35}},
        {"moves/meteor-call", true, 11, 3, 1, 0.34, 0, false, {-92, -212, 288, 28}},
        {"moves/pistol-shots", true, 14, 3, 1, 0.36, 0, false, {-111, -72, 267, 4}},
        {"moves/rocket-launcher", true, 9, 3, 1, 0.52, 0, false, {-178, -198, 302, 46}},
        {"moves/sword-combo", true, 22, 4, 1, 0.55, 0, false, {-72, -130, 192, 20}},
        {"fire/brazier", false, 0, 0, 0, 0, 0, false, {-56, -136, 56, 12}},
        {"fire/campfire", false, 0, 0, 0, 0, 0, false, {-64, -112, 64, 16}},
        {"fire/candle", false, 0, 0, 0, 0, 0, false, {-28, -64, 28, 12}},
        {"fire/fire-breath", false, 0, 0, 0, 0, 0, false, {-12, -72, 148, 44}},
        {"fire/fire-ring", true, 8, 3, 1, 0.32, 0, false, {-112, -92, 112, 36}},
        {"fire/fire-tornado", false, 0, 0, 0, 0, 0, false, {-60, -136, 60, 16}},
        {"fire/fireball", false, 0, 0, 0, 0, 0, false, {-112, -39, 24, 28}},
        {"fire/flame-pillar", false, 0, 0, 0, 0, 0, false, {-92, -232, 92, 24}},
        {"fire/flamethrower", false, 0, 0, 0, 0, 0, false, {-30, -60, 154, 44}},
        {"fire/meteor", false, 0, 0, 0, 0, 0, false, {-150, -208, 122, 24}},
        {"fire/spirit-flame", false, 0, 0, 0, 0, 0, false, {-40, -94, 40, 30}},
        {"fire/torch", false, 0, 0, 0, 0, 0, false, {-36, -94, 36, 26}},
        {"explosions/barrel-explosion", false, 0, 0, 0, 0, 0, false, {-150, -204, 150, 36}},
        {"explosions/big-explosion", false, 0, 0, 0, 0, 0, false, {-180, -262, 180, 38}},
        {"explosions/cartoon-pow", false, 0, 0, 0, 0, 0, false, {-92, -72, 92, 72}},
        {"explosions/dirt-explosion", false, 0, 0, 0, 0, 0, false, {-130, -206, 130, 34}},
        {"explosions/firework-burst", false, 0, 0, 0, 0, 0, false, {-120, -100, 120, 148}},
        {"explosions/firework-rocket", false, 0, 0, 0, 0, 0, false, {-150, -288, 150, 12}},
        {"explosions/grenade", false, 0, 0, 0, 0, 0, false, {-160, -170, 140, 30}},
        {"explosions/implosion", false, 0, 0, 0, 0, 0, false, {-104, -92, 104, 92}},
        {"explosions/nuke", false, 0, 0, 0, 0, 0, false, {-200, -284, 200, 36}},
        {"explosions/plasma-explosion", false, 0, 0, 0, 0, 0, false, {-112, -100, 112, 100}},
        {"explosions/small-explosion", false, 0, 0, 0, 0, 0, false, {-150, -196, 150, 44}},
        {"explosions/smoke-bomb", false, 0, 0, 0, 0, 0, false, {-130, -156, 130, 20}},
        {"water/bubbles", false, 0, 0, 0, 0, 0, false, {-52, -104, 52, 8}},
        {"water/drip", false, 0, 0, 0, 0, 0, false, {-36, -78, 36, 10}},
        {"water/fountain", false, 0, 0, 0, 0, 0, false, {-60, -96, 60, 8}},
        {"water/geyser", false, 0, 0, 0, 0, 0, false, {-92, -146, 92, 22}},
        {"water/rain-splash", false, 0, 0, 0, 0, 0, false, {-88, -50, 88, 30}},
        {"water/ripples", false, 0, 0, 0, 0, 0, false, {-68, -39, 68, 22}},
        {"water/splash", false, 0, 0, 0, 0, 0, false, {-88, -78, 88, 22}},
        {"water/water-jet", false, 0, 0, 0, 0, 0, false, {-14, -39, 174, 48}},
        {"water/water-orb", false, 0, 0, 0, 0, 0, false, {-50, -100, 50, 8}},
        {"water/waterfall", false, 0, 0, 0, 0, 0, false, {-72, -118, 72, 18}},
        {"water/wave-crash", false, 0, 0, 0, 0, 0, false, {-112, -86, 112, 18}},
        {"water/whirlpool", false, 0, 0, 0, 0, 0, false, {-64, -42, 64, 42}},
        {"ice/blizzard-vortex", false, 0, 0, 0, 0, 0, false, {-64, -102, 64, 14}},
        {"ice/freeze", true, 12, 2, 0, 0.2, 0, true, {-88, -108, 88, 20}},
        {"ice/frost-breath", false, 0, 0, 0, 0, 0, false, {-12, -50, 172, 54}},
        {"ice/frost-nova", true, 8, 3, 1, 0.34, 0, false, {-144, -150, 144, 42}},
        {"ice/ice-crystal", false, 0, 0, 0, 0, 0, false, {-48, -92, 48, 8}},
        {"ice/ice-lance", false, 0, 0, 0, 0, 0, false, {-146, -39, 30, 32}},
        {"ice/ice-shatter", false, 0, 0, 0, 0, 0, false, {-68, -90, 156, 22}},
        {"ice/ice-spikes", false, 0, 0, 0, 0, 0, false, {-24, -96, 240, 16}},
        {"ice/icicle-fall", false, 0, 0, 0, 0, 0, false, {-76, -100, 76, 20}},
        {"ice/snowflake-burst", false, 0, 0, 0, 0, 0, false, {-70, -106, 70, 14}},
        {"lightning/ball-lightning", false, 0, 0, 0, 0, 0, false, {-68, -92, 68, 12}},
        {"lightning/chain-lightning", true, 7, 4, 3, 0.46, 0, false, {-40, -82, 208, 22}},
        {"lightning/charge-up", true, 8, 1, 0, 0.36, 0, true, {-124, -148, 148, 36}},
        {"lightning/electrified", true, 13, 0, 0, 0, 0, true, {-54, -76, 54, 16}},
        {"lightning/lightning-strike", false, 0, 0, 0, 0, 0, false, {-120, -228, 120, 36}},
        {"lightning/plasma-ball", false, 0, 0, 0, 0, 0, false, {-56, -96, 56, 8}},
        {"lightning/spark-arc", false, 0, 0, 0, 0, 0, false, {-62, -108, 62, 12}},
        {"lightning/storm-cloud", false, 0, 0, 0, 0, 0, false, {-84, -164, 84, 12}},
        {"lightning/tesla-coil", false, 0, 0, 0, 0, 0, false, {-112, -120, 112, 16}},
        {"lightning/thunderclap", true, 7, 3, 1, 0.46, 0, true, {-144, -124, 144, 32}},
        {"lightning/zap-hit", false, 0, 0, 0, 0, 0, false, {-76, -56, 68, 56}},
        {"scifi/emp-blast", false, 0, 0, 0, 0, 0, false, {-76, -84, 76, 32}},
        {"scifi/energy-core", false, 0, 0, 0, 0, 0, false, {-56, -88, 56, 12}},
        {"scifi/force-field", true, 9, 2, 0, 0, 0, false, {-76, -84, 108, 20}},
        {"scifi/glitch", true, 13, 0, 0, 0, 0, true, {-44, -54, 44, 10}},
        {"scifi/hologram", false, 0, 0, 0, 0, 0, false, {-56, -92, 56, 12}},
        {"scifi/jetpack", false, 0, 0, 0, 0, 0, false, {-48, -39, 48, 86}},
        {"scifi/laser-grid", false, 0, 0, 0, 0, 0, false, {-64, -76, 64, 8}},
        {"scifi/scanner", true, 7, 0, 0, 0, 0, true, {-42, -58, 70, 20}},
        {"scifi/teleporter-pad", false, 0, 0, 0, 0, 0, false, {-48, -92, 48, 12}},
        {"scifi/tractor-beam", false, 0, 0, 0, 0, 0, false, {-60, -114, 60, 18}},
        {"scifi/warp-in", true, 5, 0, 0, 0, 0, true, {-96, -128, 96, 40}},
        {"scifi/warp-out", true, 4, 0, 0, 0.3, 0, true, {-64, -128, 64, 40}},
        {"melee/claw-swipe", true, 8, 3, 1, 0.46, 0, false, {-36, -84, 94, 12}},
        {"melee/dagger-flurry", true, 14, 7, 1, 0.4, 0.10000000000000009, false, {-36, -86, 112, 18}},
        {"melee/double-slash", true, 10, 2, 1, 0.42, 0.12, false, {-44, -92, 120, 16}},
        {"melee/hammer-smash", true, 10, 1, 1, 0.72, 0, false, {-80, -92, 128, 20}},
        {"melee/heavy-cleave", true, 10, 1, 1, 0.42, 0, false, {-68, -96, 140, 20}},
        {"melee/scythe-reap", true, 9, 1, 1, 0.56, 0, false, {-84, -98, 102, 18}},
        {"melee/spear-sweep", true, 10, 1, 1, 0.56, 0, false, {-104, -70, 112, 14}},
        {"melee/spin-slash", true, 10, 1, 1, 0.46, 0.12, false, {-104, -84, 104, 20}},
        {"melee/sword-slash", true, 8, 1, 1, 0.55, 0.10999999999999999, false, {-44, -92, 94, 12}},
        {"melee/thrust", true, 7, 1, 1, 0.46, 0, false, {-34, -62, 162, 14}},
        {"melee/uppercut", true, 9, 1, 1, 0.42, 0.13, false, {-48, -110, 84, 18}},
        {"melee/whip-crack", true, 9, 1, 1, 0.46, 0.07000000000000006, false, {-30, -68, 162, 12}},
        {"impacts/blunt-hit", false, 0, 0, 0, 0, 0, false, {-64, -72, 64, 16}},
        {"impacts/bullet-impact", false, 0, 0, 0, 0, 0, false, {-116, -84, 36, 16}},
        {"impacts/crit-hit", false, 0, 0, 0, 0, 0, false, {-84, -106, 132, 30}},
        {"impacts/ground-slam", false, 0, 0, 0, 0, 0, false, {-144, -164, 144, 44}},
        {"impacts/heavy-landing", true, 7, 1, 0, 0.26, 0, false, {-88, -91, 88, 20}},
        {"impacts/hit-flash", true, 5, 1, 0, 0.26, 0.36, true, {-40, -54, 40, 10}},
        {"impacts/hit-spark", false, 0, 0, 0, 0, 0, false, {-44, -64, 92, 16}},
        {"impacts/impact-frame", false, 0, 0, 0, 0, 0, false, {-160, -90, 160, 90}},
        {"impacts/parry", false, 0, 0, 0, 0, 0, false, {-100, -96, 100, 16}},
        {"impacts/ricochet", false, 0, 0, 0, 0, 0, false, {-64, -60, 168, 24}},
        {"impacts/shield-block", true, 6, 1, 0, 0.36, 0, false, {-40, -88, 108, 16}},
        {"projectiles/arrow", false, 0, 0, 0, 0, 0, false, {-92, -39, 20, 16}},
        {"projectiles/bullet-tracer", false, 0, 0, 0, 0, 0, false, {-184, -39, 16, 22}},
        {"projectiles/cannonball", false, 0, 0, 0, 0, 0, false, {-48, -102, 160, 14}},
        {"projectiles/energy-disc", false, 0, 0, 0, 0, 0, false, {-92, -39, 28, 26}},
        {"projectiles/flare", false, 0, 0, 0, 0, 0, false, {-76, -226, 76, 6}},
        {"projectiles/homing-missile", false, 0, 0, 0, 0, 0, false, {-160, -120, 88, 20}},
        {"projectiles/laser-bolt", false, 0, 0, 0, 0, 0, false, {-108, -39, 20, 16}},
        {"projectiles/magic-missile", false, 0, 0, 0, 0, 0, false, {-24, -104, 184, 8}},
        {"projectiles/muzzle-pistol", false, 0, 0, 0, 0, 0, false, {-44, -66, 52, 6}},
        {"projectiles/muzzle-shotgun", false, 0, 0, 0, 0, 0, false, {-44, -82, 116, 6}},
        {"projectiles/muzzle-smg", false, 0, 0, 0, 0, 0, false, {-56, -66, 56, 6}},
        {"projectiles/plasma-shot", false, 0, 0, 0, 0, 0, false, {-108, -39, 36, 30}},
        {"projectiles/rocket", false, 0, 0, 0, 0, 0, false, {-252, -42, 36, 34}},
        {"spawn/coin-drop", false, 0, 0, 0, 0, 0, false, {-80, -78, 80, 10}},
        {"spawn/disintegrate", true, 4, 0, 0, 0.2, 0, true, {-48, -86, 128, 14}},
        {"spawn/dissolve-burn", true, 7, 0, 0, 0.16, 0, true, {-48, -100, 48, 12}},
        {"spawn/egg-hatch", false, 0, 0, 0, 0, 0, false, {-60, -84, 60, 12}},
        {"spawn/poof", false, 0, 0, 0, 0, 0, false, {-56, -80, 56, 12}},
        {"spawn/respawn-beam", true, 5, 0, 0, 0, 0, true, {-64, -136, 64, 14}},
        {"spawn/shatter-death", true, 5, 2, 0, 0.2, 0, true, {-84, -84, 88, 12}},
        {"spawn/soul-rise", true, 6, 0, 0, 0.2, 0, true, {-48, -126, 48, 14}},
        {"spawn/spawn-portal", false, 0, 0, 0, 0, 0, false, {-104, -122, 104, 28}},
        {"spawn/summon-rise", false, 0, 0, 0, 0, 0, false, {-88, -94, 88, 22}},
        {"arcane/arcane-beam", true, 8, 2, 1, 0.62, 0, false, {-26, -84, 238, 16}},
        {"arcane/arcane-missiles", true, 15, 5, 1, 0.52, 0, false, {-26, -88, 214, 28}},
        {"arcane/arcane-orb", false, 0, 0, 0, 0, 0, false, {-68, -104, 68, 8}},
        {"arcane/blink-in", true, 5, 1, 0, 0, 0, true, {-68, -80, 68, 16}},
        {"arcane/blink-out", true, 5, 0, 0, 0.36, 0, true, {-68, -80, 68, 16}},
        {"arcane/magic-circle-cast", false, 0, 0, 0, 0, 0, false, {-100, -192, 100, 28}},
        {"arcane/mana-burst", false, 0, 0, 0, 0, 0, false, {-104, -124, 104, 26}},
        {"arcane/portal", false, 0, 0, 0, 0, 0, false, {-76, -106, 76, 10}},
        {"arcane/rune-circle", false, 0, 0, 0, 0, 0, false, {-68, -80, 68, 20}},
        {"arcane/spell-charge", true, 7, 0, 0, 0.46, 0, false, {-44, -80, 76, 6}},
        {"arcane/star-fall", false, 0, 0, 0, 0, 0, false, {-124, -150, 108, 22}},
        {"arcane/time-stop", false, 0, 0, 0, 0, 0, false, {-98, -118, 98, 18}},
        {"holy/blessing", true, 5, 0, 0, 0, 0, true, {-40, -108, 40, 20}},
        {"holy/divine-shield", true, 5, 0, 0, 0, 0, true, {-44, -68, 44, 16}},
        {"holy/feather-burst", false, 0, 0, 0, 0, 0, false, {-80, -60, 80, 100}},
        {"holy/halo", true, 5, 0, 0, 0, 0, true, {-28, -70, 28, 10}},
        {"holy/heal", true, 6, 0, 0, 0.3, 0, true, {-40, -92, 40, 12}},
        {"holy/heal-aoe", false, 0, 0, 0, 0, 0, false, {-128, -104, 128, 32}},
        {"holy/holy-pillar", false, 0, 0, 0, 0, 0, false, {-96, -204, 96, 28}},
        {"holy/resurrection", true, 12, 0, 0, 0, 0, true, {-96, -140, 96, 20}},
        {"holy/smite", false, 0, 0, 0, 0, 0, false, {-112, -148, 112, 28}},
        {"holy/sunburst", false, 0, 0, 0, 0, 0, false, {-64, -64, 64, 64}},
        {"shadow/black-hole", false, 0, 0, 0, 0, 0, false, {-92, -124, 92, 8}},
        {"shadow/curse", true, 9, 1, 0, 0.2, 0, true, {-56, -108, 56, 16}},
        {"shadow/dark-explosion", false, 0, 0, 0, 0, 0, false, {-140, -206, 140, 42}},
        {"shadow/eclipse", false, 0, 0, 0, 0, 0, false, {-80, -72, 80, 72}},
        {"shadow/grasping-hands", false, 0, 0, 0, 0, 0, false, {-100, -100, 100, 16}},
        {"shadow/hex-sigil", false, 0, 0, 0, 0, 0, false, {-84, -112, 84, 16}},
        {"shadow/life-steal", true, 9, 2, 1, 0.46, 0, true, {-36, -92, 148, 8}},
        {"shadow/necro-skulls", false, 0, 0, 0, 0, 0, false, {-68, -108, 68, 8}},
        {"shadow/shadow-flames", false, 0, 0, 0, 0, 0, false, {-52, -102, 52, 10}},
        {"shadow/shadow-tendrils", false, 0, 0, 0, 0, 0, false, {-104, -106, 104, 24}},
        {"shadow/soul-drain", false, 0, 0, 0, 0, 0, false, {-40, -110, 168, 10}},
        {"shadow/void-orb", false, 0, 0, 0, 0, 0, false, {-84, -150, 84, 10}},
        {"status/bleeding", true, 7, 0, 0, 0, 0, true, {-30, -46, 30, 10}},
        {"status/burning", true, 9, 0, 0, 0, 0, true, {-36, -70, 36, 10}},
        {"status/charmed", true, 5, 0, 0, 0, 0, true, {-30, -68, 30, 8}},
        {"status/confused", true, 5, 0, 0, 0, 0, true, {-30, -70, 30, 8}},
        {"status/cursed", true, 5, 0, 0, 0, 0, true, {-32, -76, 32, 8}},
        {"status/frozen", true, 6, 0, 0, 0, 0, true, {-36, -54, 36, 10}},
        {"status/haste", true, 13, 0, 0, 0, 0, true, {-64, -48, 36, 8}},
        {"status/petrified", true, 5, 1, 0, 0.16, 0, true, {-60, -64, 60, 10}},
        {"status/poisoned", true, 7, 0, 0, 0, 0, true, {-36, -54, 36, 10}},
        {"status/rage", true, 6, 0, 0, 0, 0, true, {-40, -70, 40, 10}},
        {"status/sleep", true, 3, 0, 0, 0, 0, true, {-26, -74, 34, 8}},
        {"status/slowed", true, 3, 0, 0, 0, 0, true, {-32, -68, 32, 8}},
        {"status/stunned", true, 5, 0, 0, 0, 0, true, {-30, -56, 30, 8}},
        {"nature/acid-splash", false, 0, 0, 0, 0, 0, false, {-92, -100, 92, 16}},
        {"nature/boulder", false, 0, 0, 0, 0, 0, false, {-40, -104, 224, 16}},
        {"nature/earth-spike", false, 0, 0, 0, 0, 0, false, {-136, -132, 136, 18}},
        {"nature/healing-bloom", false, 0, 0, 0, 0, 0, false, {-62, -108, 62, 12}},
        {"nature/leaf-storm", true, 5, 0, 0, 0, 0, false, {-56, -92, 56, 12}},
        {"nature/poison-cloud", false, 0, 0, 0, 0, 0, false, {-128, -86, 128, 14}},
        {"nature/quake", false, 0, 0, 0, 0, 0, false, {-144, -88, 144, 16}},
        {"nature/root-snare", true, 11, 2, 0, 0.24, 0, false, {-64, -78, 64, 18}},
        {"nature/spore-burst", false, 0, 0, 0, 0, 0, false, {-106, -118, 106, 14}},
        {"nature/thorns", false, 0, 0, 0, 0, 0, false, {-48, -78, 184, 14}},
        {"nature/tornado", false, 0, 0, 0, 0, 0, false, {-98, -152, 98, 16}},
        {"nature/toxic-puddle", false, 0, 0, 0, 0, 0, false, {-88, -86, 88, 14}},
        {"nature/vine-whip", false, 0, 0, 0, 0, 0, false, {-60, -114, 156, 14}},
        {"weather/aurora", false, 0, 0, 0, 0, 0, false, {-160, -180, 160, 0}},
        {"weather/blizzard", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/cherry-blossoms", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/clouds", false, 0, 0, 0, 0, 0, false, {-160, -180, 160, 0}},
        {"weather/falling-leaves", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/fireflies", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/fog", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/god-rays", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/rain", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/shooting-stars", false, 0, 0, 0, 0, 0, false, {-160, -180, 160, 0}},
        {"weather/snow", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"weather/storm", false, 0, 0, 0, 0, 0, false, {-160, -170, 160, 10}},
        {"ui/achievement", false, 0, 0, 0, 0, 0, false, {-110, -39, 110, 36}},
        {"ui/coin-collect", false, 0, 0, 0, 0, 0, false, {-60, -102, 116, 14}},
        {"ui/combo-counter", false, 0, 0, 0, 0, 0, false, {-104, -50, 104, 50}},
        {"ui/confetti", false, 0, 0, 0, 0, 0, false, {-100, -126, 100, 14}},
        {"ui/countdown", false, 0, 0, 0, 0, 0, false, {-76, -68, 76, 68}},
        {"ui/crit-number", false, 0, 0, 0, 0, 0, false, {-76, -60, 76, 60}},
        {"ui/damage-number", false, 0, 0, 0, 0, 0, false, {-30, -52, 46, 16}},
        {"ui/exclamation", true, 6, 0, 0, 0.08, 0, false, {-42, -92, 42, 12}},
        {"ui/health-bar-hit", false, 0, 0, 0, 0, 0, false, {-80, -39, 80, 34}},
        {"ui/heart-lost", false, 0, 0, 0, 0, 0, false, {-48, -39, 48, 54}},
        {"ui/level-up", true, 12, 1, 0, 0.3, 0, true, {-88, -114, 88, 24}},
        {"ui/quest-complete", false, 0, 0, 0, 0, 0, false, {-102, -46, 102, 46}},
        {"pickups/chest-open", false, 0, 0, 0, 0, 0, false, {-88, -96, 88, 16}},
        {"pickups/coin-spin", false, 0, 0, 0, 0, 0, false, {-28, -48, 28, 8}},
        {"pickups/collect-burst", false, 0, 0, 0, 0, 0, false, {-56, -94, 56, 14}},
        {"pickups/crystal-shard", false, 0, 0, 0, 0, 0, false, {-32, -68, 32, 8}},
        {"pickups/gem-spin", false, 0, 0, 0, 0, 0, false, {-32, -56, 32, 8}},
        {"pickups/heart-pickup", false, 0, 0, 0, 0, 0, false, {-28, -52, 28, 8}},
        {"pickups/key-glint", false, 0, 0, 0, 0, 0, false, {-36, -54, 36, 6}},
        {"pickups/loot-beam", false, 0, 0, 0, 0, 0, false, {-48, -162, 48, 14}},
        {"pickups/potion", false, 0, 0, 0, 0, 0, false, {-28, -64, 28, 8}},
        {"pickups/power-star", false, 0, 0, 0, 0, 0, false, {-44, -76, 44, 8}},
        {"pickups/xp-orb", false, 0, 0, 0, 0, 0, false, {-36, -48, 36, 8}},
        {"transitions/blinds", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/burn-away", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/crt-off", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/diamond-wipe", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/glitch-wipe", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/iris", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/pixel-dissolve", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/shatter-wipe", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/slash-wipe", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"transitions/star-iris", false, 0, 0, 0, 0, 0, false, {-9, -39, 320, 180}},
        {"critters/bat", false, 0, 0, 0, 0, 0, false, {-56, -40, 56, 40}},
        {"critters/bird", false, 0, 0, 0, 0, 0, false, {-56, -40, 56, 40}},
        {"critters/butterfly", false, 0, 0, 0, 0, 0, false, {-64, -44, 64, 44}},
        {"critters/eyeball", false, 0, 0, 0, 0, 0, false, {-48, -100, 48, 8}},
        {"critters/fish-school", false, 0, 0, 0, 0, 0, false, {-68, -40, 68, 40}},
        {"critters/flying-skull", false, 0, 0, 0, 0, 0, false, {-48, -120, 48, 12}},
        {"critters/frog", false, 0, 0, 0, 0, 0, false, {-56, -80, 56, 8}},
        {"critters/ghost", false, 0, 0, 0, 0, 0, false, {-48, -90, 48, 10}},
        {"critters/jellyfish", false, 0, 0, 0, 0, 0, false, {-48, -39, 48, 82}},
        {"critters/slime", false, 0, 0, 0, 0, 0, false, {-56, -72, 56, 16}},
        {"critters/snake", false, 0, 0, 0, 0, 0, false, {-68, -48, 68, 8}},
        {"critters/wisp", false, 0, 0, 0, 0, 0, false, {-64, -96, 64, 8}},
    };
    REQUIRE(std::size(rows) == PixelFx::catalogue().size());
    for (const auto& row : rows) {
        const auto catalogue = PixelFx::catalogue();
        const auto found     = std::find_if(catalogue.begin(), catalogue.end(),
                                            [&](const PixelFxInfo& item) { return item.id == row.id; });
        REQUIRE(found != catalogue.end());
        auto session   = std::make_shared<ActRenderSession>();
        auto candidate = ActRuntime::prepareInSession(found->kind, PixelFx::defaults(found->kind), session);
        REQUIRE(candidate);
        auto published = ActRuntime::publish(std::move(candidate).takeValue());
        REQUIRE(published);
        auto       act    = std::move(published).takeValue();
        const auto before = act->snapshot();
        const auto timing = act->timing();
        REQUIRE(timing.has_value() == row.hasAct);
        REQUIRE(session->cacheStats().rasterEvaluations == 0);
        if (timing) {
            REQUIRE(timing->poses.size() == row.poses);
            REQUIRE(timing->hits.size() == row.hits);
            REQUIRE(timing->targets.size() == row.targets);
            REQUIRE(timing->windup == row.windup);
            REQUIRE(timing->recovery == row.recovery);
            REQUIRE(timing->onBody == row.onBody);
        }
        auto prepared = act->prepareBounds();
        REQUIRE(prepared);
        const auto bounds = prepared.value().bounds();
        REQUIRE(bounds.x0 == row.bounds.x0);
        REQUIRE(bounds.y0 == row.bounds.y0);
        REQUIRE(bounds.x1 == row.bounds.x1);
        REQUIRE(bounds.y1 == row.bounds.y1);
        REQUIRE(act->snapshot().wallTime == before.wallTime);
        REQUIRE(act->snapshot().actTime == before.actTime);
        REQUIRE(act->snapshot().effectTime == before.effectTime);
    }
}
TEST_CASE("stylize.pixelfx.actClock.sourceContract.authoredDetailsAndBorrow") {
    auto fire = build(PixelFxKind::FireballThrow, PixelFx::defaults(PixelFxKind::FireballThrow));
    REQUIRE(fire);
    const auto timing = fire.value()->timing();
    REQUIRE(timing);
    REQUIRE(timing->hits.size() == 2);
    REQUIRE(timing->hits[0].skin == ActTargetSkin::Char);
    REQUIRE(timing->hits[1].skin == ActTargetSkin::None);
    REQUIRE(timing->hits[1].amp == 0);
    REQUIRE(timing->hits[1].push == 0);
    REQUIRE(timing->poses[0].how == "smooth");
    const auto* poses = timing->poses.data();
    const auto* hits  = timing->hits.data();
    REQUIRE(fire.value()->step(.5));
    fire.value()->stop();
    auto bounds = fire.value()->prepareBounds();
    REQUIRE(bounds);
    REQUIRE(fire.value()->timing()->poses.data() == poses);
    REQUIRE(fire.value()->timing()->hits.data() == hits);
    auto chain = build(PixelFxKind::ChainLightning, PixelFx::defaults(PixelFxKind::ChainLightning));
    REQUIRE(chain);
    const auto c = chain.value()->timing();
    REQUIRE(c);
    REQUIRE(c->targets.size() == 3);
    REQUIRE(c->hits[0].at == 0);
    REQUIRE(c->hits[1].at == 1);
    REQUIRE(c->hits[2].at == 2);
    REQUIRE(c->hits[0].amp == 0);
    REQUIRE(c->hits[0].push == 1.2);
    auto stomp = build(PixelFxKind::EarthStomp, PixelFx::defaults(PixelFxKind::EarthStomp));
    REQUIRE(stomp);
    REQUIRE(stomp.value()->timing()->hits[0].push == 0);
    REQUIRE(!stomp.value()->timing()->hits[0].skin);
    auto upper = build(PixelFxKind::Uppercut, PixelFx::defaults(PixelFxKind::Uppercut));
    REQUIRE(upper);
    const auto upperTiming = upper.value()->timing();
    REQUIRE(upperTiming);
    for (const auto& key : upperTiming->poses)
        if (key.time == .3 || key.time == .56) REQUIRE(!key.pose.contains(rig::PoseField::Ghost));
}
TEST_CASE("stylize.pixelfx.actClock.sourceContract.boundsTokenAndCacheHistory") {
    auto session = std::make_shared<ActRenderSession>();
    auto candidate =
        ActRuntime::prepareInSession(PixelFxKind::SwordCombo, PixelFx::defaults(PixelFxKind::SwordCombo), session);
    REQUIRE(candidate);
    auto published = ActRuntime::publish(std::move(candidate).takeValue());
    REQUIRE(published);
    auto act   = std::move(published).takeValue();
    auto first = act->prepareBounds();
    REQUIRE(first);
    const auto after = session->cacheStats();
    REQUIRE(after.entries > 0);
    REQUIRE(after.rasterEvaluations > 0);
    auto second = act->prepareBounds();
    REQUIRE(second);
    REQUIRE(session->cacheStats().rasterEvaluations == after.rasterEvaluations);
    REQUIRE(session->cacheStats().hits > after.hits);
    const auto expected = first.value().bounds();
    act.reset();
    session.reset();
    const auto captured = second.value().bounds();
    REQUIRE(captured.x0 == expected.x0);
    REQUIRE(captured.y0 == expected.y0);
    REQUIRE(captured.x1 == expected.x1);
    REQUIRE(captured.y1 == expected.y1);
}

TEST_CASE("stylize.pixelfx.actClock.callback.phaseAndReentrancy") {
    auto ready = build(PixelFxKind::FireballThrow, PixelFx::defaults(PixelFxKind::FireballThrow));
    REQUIRE(ready);
    auto       act   = std::move(ready).takeValue();
    size_t     calls = 0;
    eve::Value retained;
    for (int tick = 0; tick < 900; ++tick) {
        const auto before   = act->snapshot();
        auto       advanced = act->step(1.0 / 120, [&](const eve::Value& authored, const ActHitEvent& event) {
            ++calls;
            retained          = authored;
            const auto during = act->snapshot();
            REQUIRE(during.pose == before.pose);
            REQUIRE(during.actTime == event.actTime);
            REQUIRE(during.effectTime == event.effectTime);
            REQUIRE(event.index == calls - 1);
            REQUIRE(authored.find("t"));
            if (event.index == 0) REQUIRE(act->targetStates()[0].flashTime > 0);
            auto nested = act->step(0);
            REQUIRE(!nested);
            REQUIRE(!act->prepareReplay({}));
            REQUIRE(act->snapshot().wallTime == during.wallTime);
            act->stop();
            REQUIRE(act->snapshot().stopped);
        });
        REQUIRE(advanced);
    }
    REQUIRE(calls == 2);
    act.reset();
    REQUIRE(retained.find("t"));
}
TEST_CASE("stylize.pixelfx.actClock.callback.exceptionRetry") {
    auto ready = build(PixelFxKind::FireballThrow, PixelFx::defaults(PixelFxKind::FireballThrow));
    REQUIRE(ready);
    auto act   = std::move(ready).takeValue();
    bool threw = false;
    for (int tick = 0; tick < 300 && !threw; ++tick) {
        try {
            auto advanced = act->step(
                1.0 / 120, [](const eve::Value&, const ActHitEvent&) { throw std::runtime_error("observer failure"); });
            REQUIRE(advanced);
        } catch (const std::runtime_error&) {
            threw = true;
        }
    }
    REQUIRE(threw);
    const auto interrupted = act->snapshot();
    REQUIRE(act->hitEvents().size() == 1);
    size_t calls = 0;
    // Source hitstop is already installed. Drain it before the pending hit is retried.
    for (int tick = 0; tick < 100 && calls == 0; ++tick) {
        auto advanced = act->step(1.0 / 120, [&](const eve::Value&, const ActHitEvent& event) {
            REQUIRE(event.index == 0);
            ++calls;
        });
        REQUIRE(advanced);
    }
    REQUIRE(calls == 1);
    REQUIRE(act->snapshot().wallTime > interrupted.wallTime);
}

// Independent pristine drawActor custom k/position fingerprints; source draws its stock sprite.
TEST_CASE("stylize.pixelfx.actClock.onBody.customSourceScaleAndPosition") {
    struct Frame {
        int      scale;
        size_t   tick;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {1, 0, 724009407u, 3450381293u},   {1, 1, 1914187608u, 1850253919u},   {1, 20, 2812507544u, 628809846u},
        {1, 60, 2944644803u, 2307626669u}, {1, 120, 4252479372u, 2471864627u}, {1, 180, 631714450u, 959881790u},
        {2, 0, 1679273347u, 3586360613u},  {2, 1, 1087848800u, 1181853907u},   {2, 20, 1571183972u, 4163627850u},
        {2, 60, 3435142311u, 136848113u},  {2, 120, 3586038671u, 2738679051u}, {2, 180, 3726119450u, 3323054750u}};
    for (int scale : {1, 2}) {
        auto  settings = PixelFx::defaults(PixelFxKind::Burning);
        auto& actor    = settings.actor;
        actor.source   = PixelFxActorSource::Custom;
        actor.width    = 7;
        actor.height   = 11;
        actor.scale    = scale;
        actor.x        = -4;
        actor.y        = -13;
        actor.head     = {0, -13};
        actor.chest    = {0, -6};
        actor.eyes.clear();
        actor.mask.resize(77);
        actor.pixels.resize(77);
        for (size_t i = 0; i < 77; ++i) {
            actor.mask[i]   = uint8_t(i % 7 != 0);
            actor.pixels[i] = i % 7 ? 0xff195beau : 0;
        }
        auto ready = build(PixelFxKind::Burning, settings);
        REQUIRE(ready);
        auto act = std::move(ready).takeValue();
        actor.pixels.clear();
        actor.mask.clear();
        actor.x = 999;
        std::vector<uint32_t> color(600 * 320), light(color.size());
        PixelFxCanvas         canvas{600, 320, 0, 0, color, light};
        size_t                tick = 0;
        for (const auto& expected : frames) {
            if (expected.scale != scale) continue;
            while (tick < expected.tick) {
                REQUIRE(act->step(1.0 / 60));
                ++tick;
            }
            std::fill(color.begin(), color.end(), 0u);
            std::fill(light.begin(), light.end(), 0u);
            auto prepared = act->prepareFrame(canvas, {300, 160, 0, scale == 2}, false);
            REQUIRE(prepared);
            REQUIRE(prepared.value().copyTo(canvas));
            REQUIRE(checksum(color) == expected.color);
            REQUIRE(checksum(light) == expected.light);
        }
    }
}

TEST_CASE("stylize.pixelfx.actClock.inputBody.ownedPixelsAndOrigin") {
    for (auto kind : {PixelFxKind::ArrowShot, PixelFxKind::Freeze})
        for (bool flip : {false, true}) {
            auto  settings = PixelFx::defaults(kind);
            auto& actor    = settings.actor;
            actor.source   = PixelFxActorSource::Custom;
            actor.width    = 3;
            actor.height   = 2;
            actor.scale    = 1;
            actor.x        = -1.3;
            actor.y        = -4.3;
            actor.head     = {0, -4};
            actor.chest    = {0, -3};
            actor.eyes.clear();
            actor.mask     = {0, 1, 1, 0, 1, 0};
            actor.pixels   = {0xff123456u, 0, 0xff987654u, 0x80abcdefu, 0xfffedcbau, 0};
            auto session   = std::make_shared<ActRenderSession>();
            auto candidate = ActRuntime::prepareInSession(kind, settings, session);
            REQUIRE(candidate);
            auto ready = ActRuntime::publish(std::move(candidate).takeValue());
            REQUIRE(ready);
            auto                  act  = std::move(ready).takeValue();
            const auto            pose = act->snapshot().pose.pose;
            std::vector<uint32_t> color(32 * 16), light(color.size());
            PixelFxCanvas         canvas{32, 16, 8, 10, color, light};
            auto frame = act->prepareFrame(canvas, {.25, .25, 0, flip}, false, ActBodyPresentation::InputActor);
            REQUIRE(frame);
            REQUIRE(session->cacheStats().rasterEvaluations == 0);
            REQUIRE(std::all_of(color.begin(), color.end(), [](uint32_t c) { return c == 0; }));
            const auto saved = actor.pixels;
            actor.pixels.clear();
            actor.mask.clear();
            act.reset();
            session.reset();
            REQUIRE(frame.value().copyTo(canvas));
            size_t opaque = 0;
            for (auto c : color)
                if (c >> 24 == 255) ++opaque;
            REQUIRE(opaque == 3);
            for (size_t j = 0; j < saved.size(); ++j)
                if (saved[j]) {
                    const int x           = -2 + int(j % 3) + int(std::floor(pose.x + .5));
                    const int y           = -5 + int(j / 3) + int(std::floor(pose.y + .5));
                    const int destination = (10 + y) * 32 + 8 + (flip ? -x - 1 : x);
                    REQUIRE(color[size_t(destination)] == saved[j]);
                }
            REQUIRE(std::all_of(light.begin(), light.end(), [](uint32_t c) { return c == 0; }));
        }
}
TEST_CASE("stylize.pixelfx.actClock.inputBody.absentAndInvalidMode") {
    auto settings         = PixelFx::defaults(PixelFxKind::ArrowShot);
    settings.actor.source = PixelFxActorSource::None;
    auto ready            = build(PixelFxKind::ArrowShot, settings);
    REQUIRE(ready);
    auto                  act = std::move(ready).takeValue();
    std::vector<uint32_t> color(32 * 16), light(color.size());
    PixelFxCanvas         canvas{32, 16, 8, 10, color, light};
    auto                  invalid = act->prepareFrame(canvas, {}, false, ActBodyPresentation(99));
    REQUIRE(!invalid);
    auto frame = act->prepareFrame(canvas, {}, false, ActBodyPresentation::InputActor);
    REQUIRE(frame);
    REQUIRE(frame.value().copyTo(canvas));
    REQUIRE(std::none_of(color.begin(), color.end(), [](uint32_t c) { return c >> 24 == 255; }));
}

// Independent input-image oracle uses unmodified source clocks/poses/effect kernels.
// These custom presentation fingerprints are not the pristine stock-rig body path.
TEST_CASE("stylize.pixelfx.actClock.inputBody.activeCarryIndependentOracle") {
    struct Frame {
        PixelFxKind kind;
        bool        flip;
        size_t      tick;
        uint32_t    color, light;
    };
    const Frame frames[] = {{PixelFxKind::Thunderclap, false, 0, 318692018u, 3411410373u},
                            {PixelFxKind::Thunderclap, false, 36, 1600798702u, 2356375279u},
                            {PixelFxKind::Thunderclap, false, 72, 3600294999u, 1235609003u},
                            {PixelFxKind::Thunderclap, false, 120, 1257195676u, 3411410373u},
                            {PixelFxKind::Thunderclap, false, 180, 4269874041u, 3411410373u},
                            {PixelFxKind::Thunderclap, false, 240, 318692018u, 3411410373u},
                            {PixelFxKind::Thunderclap, true, 0, 2704906594u, 3411410373u},
                            {PixelFxKind::Thunderclap, true, 36, 2070454179u, 2744365238u},
                            {PixelFxKind::Thunderclap, true, 72, 3501982539u, 939950897u},
                            {PixelFxKind::Thunderclap, true, 120, 1213384545u, 3411410373u},
                            {PixelFxKind::Thunderclap, true, 180, 2332773273u, 3411410373u},
                            {PixelFxKind::Thunderclap, true, 240, 2704906594u, 3411410373u},
                            {PixelFxKind::Freeze, false, 0, 318692018u, 3411410373u},
                            {PixelFxKind::Freeze, false, 36, 1743685831u, 1717969245u},
                            {PixelFxKind::Freeze, false, 72, 962636988u, 3114484452u},
                            {PixelFxKind::Freeze, false, 120, 3355411576u, 2120525951u},
                            {PixelFxKind::Freeze, false, 180, 3444122976u, 3404354913u},
                            {PixelFxKind::Freeze, false, 240, 1325988256u, 3583380381u},
                            {PixelFxKind::Freeze, true, 0, 2704906594u, 3411410373u},
                            {PixelFxKind::Freeze, true, 36, 1606745982u, 3883200783u},
                            {PixelFxKind::Freeze, true, 72, 1403799094u, 4001934057u},
                            {PixelFxKind::Freeze, true, 120, 846138555u, 3474199898u},
                            {PixelFxKind::Freeze, true, 180, 3430707721u, 771686083u},
                            {PixelFxKind::Freeze, true, 240, 2516423679u, 735235361u},
                            {PixelFxKind::Burning, false, 0, 2049744463u, 3563651240u},
                            {PixelFxKind::Burning, false, 36, 1626508544u, 3801987411u},
                            {PixelFxKind::Burning, false, 72, 2049744463u, 3563651240u},
                            {PixelFxKind::Burning, false, 120, 3441366441u, 2771381623u},
                            {PixelFxKind::Burning, false, 180, 3457039652u, 1825342545u},
                            {PixelFxKind::Burning, false, 240, 3081963417u, 1946764062u},
                            {PixelFxKind::Burning, true, 0, 2660791279u, 471105945u},
                            {PixelFxKind::Burning, true, 36, 3318332485u, 3162798309u},
                            {PixelFxKind::Burning, true, 72, 2660791279u, 471105945u},
                            {PixelFxKind::Burning, true, 120, 2863058006u, 726733345u},
                            {PixelFxKind::Burning, true, 180, 3565255041u, 1496840529u},
                            {PixelFxKind::Burning, true, 240, 1120790624u, 2530643513u},
                            {PixelFxKind::RespawnBeam, false, 0, 516413701u, 3411410373u},
                            {PixelFxKind::RespawnBeam, false, 36, 3018447065u, 2152925294u},
                            {PixelFxKind::RespawnBeam, false, 72, 3332691957u, 977656023u},
                            {PixelFxKind::RespawnBeam, false, 120, 1635932573u, 3177148818u},
                            {PixelFxKind::RespawnBeam, false, 180, 318692018u, 3411410373u},
                            {PixelFxKind::RespawnBeam, false, 240, 4269874041u, 3411410373u},
                            {PixelFxKind::RespawnBeam, true, 0, 516413701u, 3411410373u},
                            {PixelFxKind::RespawnBeam, true, 36, 1050157963u, 935482105u},
                            {PixelFxKind::RespawnBeam, true, 72, 3433072410u, 2248269465u},
                            {PixelFxKind::RespawnBeam, true, 120, 801296917u, 253074137u},
                            {PixelFxKind::RespawnBeam, true, 180, 2704906594u, 3411410373u},
                            {PixelFxKind::RespawnBeam, true, 240, 2332773273u, 3411410373u}};
    for (auto kind : {PixelFxKind::Thunderclap, PixelFxKind::Freeze, PixelFxKind::Burning, PixelFxKind::RespawnBeam})
        for (bool flip : {false, true}) {
            auto  settings = PixelFx::defaults(kind);
            auto& a        = settings.actor;
            a.source       = PixelFxActorSource::Custom;
            a.width        = 7;
            a.height       = 29;
            a.scale        = 1;
            a.x            = -4.3;
            a.y            = -29.3;
            a.head         = {-1, -30};
            a.chest        = {-1, -19};
            a.eyes         = {{3, 3}};
            a.mask.resize(203);
            a.pixels.resize(203);
            for (size_t i = 0; i < 203; ++i) {
                a.mask[i]   = uint8_t(i % 7 != 0 && i % 7 != 6);
                a.pixels[i] = i % 7 ? (i % 5 == 0 ? 0x80195beau : 0xffb64925u) : 0;
            }
            auto ready = build(kind, settings);
            REQUIRE(ready);
            auto                  act = std::move(ready).takeValue();
            std::vector<uint32_t> color(600 * 320), light(color.size());
            PixelFxCanvas         canvas{600, 320, 7, 9, color, light};
            size_t                tick = 0;
            for (const auto& expected : frames) {
                if (expected.kind != kind || expected.flip != flip) continue;
                while (tick < expected.tick) {
                    REQUIRE(act->step(1.0 / 60));
                    ++tick;
                }
                std::fill(color.begin(), color.end(), 0u);
                std::fill(light.begin(), light.end(), 0u);
                const auto time = act->snapshot().actTime;
                auto       frame =
                    act->prepareFrame(canvas, {300.25, 160.25, 0, flip}, false, ActBodyPresentation::InputActor);
                REQUIRE(frame);
                REQUIRE(frame.value().copyTo(canvas));
                REQUIRE(checksum(color) == expected.color);
                REQUIRE(checksum(light) == expected.light);
                REQUIRE(act->snapshot().actTime == time);
            }
        }
}

TEST_CASE("stylize.pixelfx.actClock.effectsOnly.matchesDirectSourceComposition") {
    auto session  = std::make_shared<ActRenderSession>();
    auto prepared = ActRuntime::prepareInSession(PixelFxKind::FireballThrow,
                                                 PixelFx::defaults(PixelFxKind::FireballThrow), session);
    REQUIRE(prepared);
    auto ready = ActRuntime::publish(std::move(prepared).takeValue());
    REQUIRE(ready);
    auto                  act = std::move(ready).takeValue();
    std::vector<uint32_t> color(512 * 200, 0x80201144u), light(color.size(), 0xff102030u), expectedColor = color,
                                                                                           expectedLight = light;
    PixelFxCanvas canvas{512, 200, 64, 150, color, light}, expected{512, 200, 64, 150, expectedColor, expectedLight};
    auto          frame = act->prepareFrame(canvas, {}, true, ActBodyPresentation::EffectsOnly);
    REQUIRE(frame);
    REQUIRE(act->drawWithoutBody(expected, {}, true));
    REQUIRE(frame.value().copyTo(canvas));
    REQUIRE(color == expectedColor);
    REQUIRE(light == expectedLight);
    REQUIRE(session->cacheStats().rasterEvaluations == 0);
}
TEST_CASE("stylize.pixelfx.actClock.replay.orderedHistoryAll244") {
    std::vector<ActReplayCommand> commands{{ActReplayOperation::Step, 0}};
    for (int i = 0; i < 360; ++i) {
        constexpr double deltas[]{1. / 60, 1. / 120, .001, .027, 0};
        commands.push_back({ActReplayOperation::Step, deltas[i % 5]});
        if (i == 90 || i == 180) commands.push_back({ActReplayOperation::Stop, 0});
    }
    size_t count = 0;
    for (const auto& info : PixelFx::catalogue()) {
        auto a = build(info.kind, PixelFx::defaults(info.kind)), b = build(info.kind, PixelFx::defaults(info.kind));
        REQUIRE(a);
        REQUIRE(b);
        auto expected = std::move(a).takeValue(), origin = std::move(b).takeValue();
        for (const auto& command : commands) {
            if (command.operation == ActReplayOperation::Stop)
                expected->stop();
            else
                REQUIRE(expected->step(command.delta));
        }
        REQUIRE(origin->step(.37));
        origin->stop();
        const auto before   = origin->snapshot();
        auto       prepared = origin->prepareReplay(commands);
        REQUIRE(prepared);
        REQUIRE(origin->snapshot().actTime == before.actTime);
        REQUIRE(origin->snapshot().effectTime == before.effectTime);
        REQUIRE(origin->snapshot().stopped == before.stopped);
        origin.reset();
        auto published = ActRuntime::publish(std::move(prepared).takeValue());
        REQUIRE(published);
        auto       replayed = std::move(published).takeValue();
        const auto e = expected->snapshot(), r = replayed->snapshot();
        REQUIRE(e.wallTime == r.wallTime);
        REQUIRE(e.actTime == r.actTime);
        REQUIRE(e.effectTime == r.effectTime);
        REQUIRE(e.pose == r.pose);
        REQUIRE(e.freezeRemaining == r.freezeRemaining);
        REQUIRE(e.shakeRemaining == r.shakeRemaining);
        REQUIRE(e.shake == r.shake);
        REQUIRE(e.loops == r.loops);
        REQUIRE(e.done == r.done);
        REQUIRE(e.stopped == r.stopped);
        REQUIRE(expected->targetStates().size() == replayed->targetStates().size());
        for (size_t i = 0; i < expected->targetStates().size(); ++i) {
            const auto& x = expected->targetStates()[i];
            const auto& y = replayed->targetStates()[i];
            REQUIRE(x.clock == y.clock);
            REQUIRE(x.randomState == y.randomState);
        }
        std::vector<uint32_t> ec(512 * 256), el(ec.size()), rc(ec.size()), rl(ec.size());
        REQUIRE(expected->drawWithoutBody({512, 256, 128, 220, ec, el}, {}, true));
        REQUIRE(replayed->drawWithoutBody({512, 256, 128, 220, rc, rl}, {}, true));
        REQUIRE(ec == rc);
        REQUIRE(el == rl);
        ++count;
    }
    REQUIRE(count == 244);
}
TEST_CASE("stylize.pixelfx.actClock.replay.validationAndLifetime") {
    auto ready = build(PixelFxKind::ArrowShot, PixelFx::defaults(PixelFxKind::ArrowShot));
    REQUIRE(ready);
    auto act = std::move(ready).takeValue();
    REQUIRE(act->step(.3));
    const auto before = act->snapshot();
    for (const auto command :
         {ActReplayCommand{ActReplayOperation::Step, -1}, ActReplayCommand{ActReplayOperation::Step, 1.01},
          ActReplayCommand{ActReplayOperation::Step, std::numeric_limits<double>::quiet_NaN()},
          ActReplayCommand{ActReplayOperation::Stop, .1}, ActReplayCommand{ActReplayOperation(99), 0}}) {
        const std::array<ActReplayCommand, 2> history{{{ActReplayOperation::Step, .1}, command}};
        REQUIRE(!act->prepareReplay(history));
        REQUIRE(act->snapshot().wallTime == before.wallTime);
        REQUIRE(act->snapshot().pose == before.pose);
    }
    std::vector<ActReplayCommand> tooMany(1000001);
    REQUIRE(!act->prepareReplay(tooMany));
    std::vector<ActReplayCommand> tooExpensive(8334, {ActReplayOperation::Step, 1});
    REQUIRE(!act->prepareReplay(tooExpensive));
    auto empty = act->prepareReplay({});
    REQUIRE(empty);
    auto token = std::move(empty).takeValue();
    act.reset();
    auto reset = ActRuntime::publish(std::move(token));
    REQUIRE(reset);
    REQUIRE(reset.value()->snapshot().wallTime == 0);
    REQUIRE(!reset.value()->snapshot().stopped);
    REQUIRE(!ActRuntime::publish(std::move(token)));
}
}  // namespace test_pixelfx_act_clock_unit
