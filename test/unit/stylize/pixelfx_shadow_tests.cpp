// Independent pinned shadow/black-hole.js and shadow/_lib.js fixtures.
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Shadow.h"
#include "stylize/pixelfx/ShadowGeometry.h"
#include "stylize/pixelfx/ShadowSimulation.h"
#include "stylize/pixelfx/Simulation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_shadow_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
uint32_t fingerprint(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto b : std::as_bytes(pixels)) h = (h ^ uint8_t(b)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.shadow.sourceHaloAndTransformedUnlight") {
    const PixelFxInfo  fxInfo{PixelFxKind::BlackHole, "shadow/helper-fixture", 96, 80, 48, 64, 4, true};
    PixelFx            fx(PixelFxKind::BlackHole);
    const auto         C             = fx.settings().palette;
    constexpr uint32_t expected[][2] = {
        {2100873167u, 3971699876u}, {2100873167u, 718574324u}, {2250954511u, 2999340898u}};
    for (int i = 0; i < 3; ++i) {
        std::vector<uint32_t> c(96 * 80), l(c.size());
        Paint                 p{fxInfo, c, l, 1};
        if (i == 2) p.place(3.25, -1.5, true, .2);
        for (int y = -30; y <= 0; ++y)
            for (int x = -20; x <= 20; ++x)
                if ((x + y) & 1) p.px(x, y, C[1]);
        p.glow(-8, -12, 22, C[5], .8);
        p.glow(7, -8, 17, C[6], .5);
        shadow::halo(p, .25, -10.3, 7.2, 12.1, WHITE, .8);
        shadow::unlight(
            p, -14, -25, 14, 4,
            [](double x, double y) { return std::pow(x - .25, 2) + std::pow(y + 10.3, 2) < std::pow(8.5, 2) && x < 1; },
            i == 1 ? .35 : 0);
        REQUIRE(fingerprint(c) == expected[i][0]);
        REQUIRE(fingerprint(l) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.blackHolePinnedLensingDebrisAndTwoLoops") {
    constexpr int      ticks[]    = {0,   1,   24,  60,  120, 180, 239, 240, 241, 300,
                                     360, 420, 479, 480, 481, 600, 720, 840, 959, 960};
    constexpr uint32_t sums[][40] = {
        {1326805998u, 2014181891u, 3857619354u, 3148242624u, 1274714578u, 2979209852u, 1616914612u, 2840416576u,
         1107843365u, 3573591925u, 741165112u,  2856756987u, 2260534888u, 283396865u,  597266295u,  1029444187u,
         1165654493u, 2442512511u, 526624370u,  3871189856u, 3924264575u, 2133339235u, 2454556791u, 1975526279u,
         2013563163u, 1126252077u, 1326805998u, 2014181891u, 3857619354u, 3148242624u, 1107843365u, 3573591925u,
         597266295u,  1029444187u, 3924264575u, 2133339235u, 2013563163u, 1126252077u, 1326805998u, 2014181891u},
        {556382597u,  1729379855u, 53024269u,   2960027252u, 1829021111u, 3281605651u, 245753694u,  1828621355u,
         3263504994u, 2926754936u, 3944820794u, 2107050125u, 456221505u,  3373966846u, 1789112972u, 3522225094u,
         1003335941u, 3809134274u, 2895098644u, 214536772u,  2426712283u, 1176867149u, 3210886268u, 1409741390u,
         982601237u,  3788025567u, 556382597u,  1729379855u, 53024269u,   2960027252u, 3263504994u, 2926754936u,
         1789112972u, 3522225094u, 2426712283u, 1176867149u, 982601237u,  3788025567u, 556382597u,  1729379855u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::BlackHole);
        REQUIRE(!fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.blackHoleAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::BlackHole), reference(PixelFxKind::BlackHole);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.999, 4., 4.001, 8., 8.113}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            REQUIRE(std::abs(live.effectTime() - target) < 1e-12);
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
    PixelFx fx(PixelFxKind::BlackHole);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::BlackHole, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.blackHoleRockOwnershipBoundsAndRoundtrip") {
    PixelFx fx(PixelFxKind::BlackHole);
    auto    settings     = fx.settings();
    settings.shadow.rock = {0xff102030u};
    auto prepared        = PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings);
    REQUIRE(prepared);
    settings.shadow.rock[0] = 0xffffffffu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().shadow.rock == std::vector<uint32_t>(4, 0xff102030u));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::BlackHole, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.7);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    for (size_t size : {size_t(0), size_t(65)}) {
        settings = fx.settings();
        settings.shadow.rock.assign(size, WHITE);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings));
        REQUIRE(fx.configurationValue() == value);
        REQUIRE(fx.effectTime() == .7);
        REQUIRE(fingerprint(fx.color()) == color);
        REQUIRE(fingerprint(fx.light()) == light);
    }
    settings = fx.settings();
    settings.shadow.rock.assign(64, WHITE);
    auto max = PixelFx::prepareConfiguration(PixelFxKind::BlackHole, settings);
    REQUIRE(max);
    REQUIRE(fx.commitConfiguration(std::move(max).takeValue()));
    REQUIRE(fx.settings().shadow.rock == std::vector<uint32_t>(4, WHITE));
}
TEST_CASE("stylize.pixelfx.shadow.blackHoleLastUnlightLayerCompositionAndReverse") {
    PixelFx    fx(PixelFxKind::BlackHole);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.3, 1., 2., 3.999999, 4., 8., .3}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.light()[size_t(54) * 184 + 92] == 0);
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_shadow_unit


namespace shadow_puffs_fixture {
using namespace test_pixelfx_shadow_unit;
TEST_CASE("stylize.pixelfx.shadow.sourcePuffsAttractionDelayedDeathAndLitRims") {
    PixelFx       fx(PixelFxKind::BlackHole);
    const auto    C = fx.settings().palette;
    Rng           R(9);
    shadow::Puffs puffs;
    puffs.flame = {WHITE};
    puffs.flame.insert(puffs.flame.end(), C.rbegin(), C.rend());
    puffs.soot = {C[3], C[1]};
    puffs.cap  = C[4];
    puffs.rim  = {C[6], C[5], C[4]};
    puffs.spawn(R, shadow::PuffKind::Flame, -8, -14, 30, -4, .15, 5, 0, true);
    puffs.spawn(R, shadow::PuffKind::Flame, 8, -12, -8, -2, .9, 4, .1, false);
    puffs.spawn(R, shadow::PuffKind::Soot, 0, -1, 12, 7, 1.5, 6);
    puffs.spawn(R, shadow::PuffKind::Soot, 4, -20, -10, 4, 1.2, 4, .025);
    const PixelFxInfo info{PixelFxKind::BlackHole, "shadow/puff-fixture", 96, 80, 48, 64, 4, true};
    for (int tick = 0; tick <= 200; ++tick) {
        switch (tick) {
            case 0: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 3031295965u);
                REQUIRE(std::abs(puffs.list[0].x - (-8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-14)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (30)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (19.872891809791327)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-12)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (-8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.90000000000000002)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0.10000000000000001)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-1)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (12)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (7)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.5)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-20)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (-10)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (0.025000000000000001)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (82.16717173345387)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 2958095730u);
                REQUIRE(fingerprint(l) == 865102293u);
                constexpr uint32_t expected[][2] = {{1000667946u, 865102293u},
                                                    {1000667946u, 865102293u},
                                                    {1000667946u, 865102293u},
                                                    {1794901481u, 2249872327u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 1: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 3031295965u);
                REQUIRE(std::abs(puffs.list[0].x - (-7.7606620099872048)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-14.038413740969343)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (28.720558801535468)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-4.6096489163211007)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.14166666666666666)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (19.872891809791327)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-12)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (-8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.90000000000000002)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0.091666666666666674)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (0.098342111086110393)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-3.5999999999999996)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (11.604369108161025)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.4916666666666667)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-20)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (-10)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (0.01666666666666667)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (82.16717173345387)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 25661976u);
                REQUIRE(fingerprint(l) == 3627686056u);
                constexpr uint32_t expected[][2] = {{2926863557u, 3627686056u},
                                                    {4065888433u, 3627686056u},
                                                    {305457474u, 3627686056u},
                                                    {4136682355u, 2424450136u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 4: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 3031295965u);
                REQUIRE(std::abs(puffs.list[0].x - (-7.0995884941261433)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-14.182337640736007)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (25.373051904106369)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-6.3089089371288081)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.11666666666666667)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (19.872891809791327)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-12)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (-8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.90000000000000002)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0.06666666666666668)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (0.37909209744229722)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-3.6090191234053117)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (11.047005707616343)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (-0.53742729810507628)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.4666666666666668)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-20)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (-10)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (4)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (-0.0083333333333333297)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (82.16717173345387)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 1997382674u);
                REQUIRE(fingerprint(l) == 3342496554u);
                constexpr uint32_t expected[][2] = {{3990523952u, 3342496554u},
                                                    {2893165988u, 3342496554u},
                                                    {2410972118u, 3342496554u},
                                                    {1999362154u, 451297550u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 12: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 3031295965u);
                REQUIRE(std::abs(puffs.list[0].x - (-5.6774002931284695)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-14.747056642328086)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (18.487403279279071)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-9.9853949299336442)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.050000000000000017)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (19.872891809791327)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-12)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (-8)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-2)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.90000000000000002)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (2.4286128663675299e-17)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (1.064939248281658)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-3.6940097783670232)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (9.7312123816982989)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (-1.8175934589686011)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.4000000000000004)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (3.3172747246795069)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-19.808781313630071)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (-10.511084999675882)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (2.0484098469239278)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.1333333333333335)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (-0.0083333333333333297)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (82.16717173345387)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 586484430u);
                REQUIRE(fingerprint(l) == 2999778757u);
                constexpr uint32_t expected[][2] = {{1079746946u, 2999778757u},
                                                    {3852632602u, 2999778757u},
                                                    {2077821350u, 2999778757u},
                                                    {1479451364u, 2999778757u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 18: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 3031295965u);
                REQUIRE(std::abs(puffs.list[0].x - (-4.8803662782634127)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-15.309064568356185)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (14.188864070861817)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-12.063495345427295)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (2.4286128663675299e-17)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (19.872891809791327)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (7.6952975548967961)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-12.174824680069236)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (-6.8022578000768146)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-5.5712608728419406)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.8583333333333335)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (-0.0083333333333333089)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (1.5263859067157186)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-3.8095403883722208)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (8.8854154018831046)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (-2.6486080759645585)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.3500000000000005)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (2.7703501237639245)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-19.741285235529102)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (-11.295604832129662)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (0.89467914317703723)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.0833333333333337)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (-0.0083333333333333297)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (82.16717173345387)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 3131835682u);
                REQUIRE(fingerprint(l) == 3191650525u);
                constexpr uint32_t expected[][2] = {{1678781644u, 3191650525u},
                                                    {29553935u, 3191650525u},
                                                    {1270354916u, 3191650525u},
                                                    {580750255u, 1317624429u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 19: {
                REQUIRE(puffs.list.size() == size_t(4));
                REQUIRE(R.state == 2399460295u);
                REQUIRE(std::abs(puffs.list[0].x - (7.6410371930032284)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-12.226570189710024)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (-6.5112434272281021)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-6.2094611568946938)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.8500000000000002)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (-0.0083333333333333089)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (1.599342297674158)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-3.8326860859428948)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (8.7547669150127216)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-2.7774837084808759)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (1.3416666666666672)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (2.6748189448059483)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-19.735153552143032)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (-11.463741474957159)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (0.73580200632815584)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.0750000000000004)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (-0.0083333333333333297)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (82.16717173345387)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].x - (-4.8803662782634127)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].y - (-16.309064568356185)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vx - (7.1230706936287014)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].vy - (-11.953500331721866)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].life - (1.4066630803747104)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[3].seed - (68.886161013506353)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 513781953u);
                REQUIRE(fingerprint(l) == 2139356273u);
                constexpr uint32_t expected[][2] = {{331725095u, 2139356273u},
                                                    {2163064324u, 2139356273u},
                                                    {2225228537u, 2139356273u},
                                                    {2936396794u, 2328006860u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 30: {
                REQUIRE(puffs.list.size() == size_t(3));
                REQUIRE(R.state == 2399460295u);
                REQUIRE(std::abs(puffs.list[0].x - (7.2354472823166596)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-13.096519777726737)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (-2.6006292492817953)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-11.879355754537132)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.75833333333333386)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (-0.0083333333333333089)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (2.3361524653913901)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-4.1523188445771204)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (7.4842600294199837)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-4.0359724344230319)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (1.2500000000000009)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (14.176610670983791)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].x - (-4.1793006683760368)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].y - (-17.302102039008439)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vx - (8.1755629312300915)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].vy - (-9.8536236155585115)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].life - (1.3149964137080441)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[2].seed - (68.886161013506353)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 359208522u);
                REQUIRE(fingerprint(l) == 1788428592u);
                constexpr uint32_t expected[][2] = {{2537756436u, 1788428592u},
                                                    {3414399955u, 1788428592u},
                                                    {3490034313u, 1788428592u},
                                                    {590385504u, 1503258713u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 60: {
                REQUIRE(puffs.list.size() == size_t(2));
                REQUIRE(R.state == 2399460295u);
                REQUIRE(std::abs(puffs.list[0].x - (6.8126480176855129)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-17.216564518370021)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (-3.4866059391077)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-19.401540392112864)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.50833333333333475)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (-0.0083333333333333089)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (3.8744251415832864)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-5.4898983619692663)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (5.1068614850421579)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-6.3690311953091054)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (1.0000000000000018)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (14.176610670983791)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 1085516490u);
                REQUIRE(fingerprint(l) == 842112299u);
                constexpr uint32_t expected[][2] = {{3420381808u, 842112299u},
                                                    {2879270547u, 842112299u},
                                                    {695365910u, 842112299u},
                                                    {2102098963u, 106849384u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 100: {
                REQUIRE(puffs.list.size() == size_t(2));
                REQUIRE(R.state == 2399460295u);
                REQUIRE(std::abs(puffs.list[0].x - (6.0756343401120203)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-24.007444457814387)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (-3.5242799218924388)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-20.275971163207803)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.17500000000000068)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (-0.0083333333333333089)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (85.121079627424479)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].x - (5.214560529240063)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].y - (-7.939342781904525)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vx - (3.0990010560967622)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].vy - (-8.124406637222874)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].life - (0.66666666666666963)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[1].seed - (14.176610670983791)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 418299315u);
                REQUIRE(fingerprint(l) == 2999778757u);
                constexpr uint32_t expected[][2] = {{2363253737u, 2999778757u},
                                                    {46339318u, 2999778757u},
                                                    {4044848408u, 2999778757u},
                                                    {1533140033u, 2999778757u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 160: {
                REQUIRE(puffs.list.size() == size_t(1));
                REQUIRE(R.state == 2399460295u);
                REQUIRE(std::abs(puffs.list[0].x - (6.0872956159505129)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].y - (-12.481724046842643)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vx - (0.32674670777577103)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].vy - (-10.017708686897903)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].life - (0.16666666666666946)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].delay - (0)) < 1e-11);
                REQUIRE(std::abs(puffs.list[0].seed - (14.176610670983791)) < 1e-11);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 2372982574u);
                REQUIRE(fingerprint(l) == 2999778757u);
                constexpr uint32_t expected[][2] = {{446419913u, 2999778757u},
                                                    {185057503u, 2999778757u},
                                                    {387690052u, 2999778757u},
                                                    {1441252473u, 2999778757u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            case 200: {
                REQUIRE(puffs.list.size() == size_t(0));
                REQUIRE(R.state == 2399460295u);
                std::vector<uint32_t>   c(96 * 80), l(c.size());
                Paint                   p{info, c, l, 1};
                const shadow::PuffLight light{-12, -24, .8, 50};
                puffs.draw(p, shadow::PuffKind::Soot, &light);
                puffs.draw(p, shadow::PuffKind::Flame);
                shadow::ringFast(
                    p, .25, -30.5, 18.4, 2.3, [&](double, double a) { return a < 0 ? C[5] : C[7]; },
                    [](double v, double) { return .3 + v * .6; });
                REQUIRE(fingerprint(c) == 3521066581u);
                REQUIRE(fingerprint(l) == 2999778757u);
                constexpr uint32_t expected[][2] = {{2999778757u, 2999778757u},
                                                    {2999778757u, 2999778757u},
                                                    {2999778757u, 2999778757u},
                                                    {2999778757u, 2999778757u}};
                for (int variant = 0; variant < 4; ++variant) {
                    std::vector<uint32_t> cc(96 * 80), ll(cc.size());
                    Paint                 pp{info, cc, ll, 1};
                    if (variant == 3) pp.place(3.25, -1.5, true, .2);
                    const double            strengths[] = {0, .3, .8, 1.2};
                    const shadow::PuffLight lightVariant{-12, -24, strengths[variant], 50};
                    puffs.draw(pp, shadow::PuffKind::Soot, variant ? &lightVariant : nullptr);
                    puffs.draw(pp, shadow::PuffKind::Flame);
                    REQUIRE(fingerprint(cc) == expected[variant][0]);
                    REQUIRE(fingerprint(ll) == expected[variant][1]);
                }
                break;
            }
            default: break;
        }
        if (tick < 200) {
            (void)shadow::attract(puffs.list, 0, -18, 900, 70, .6, 2.5, 1. / 120, [](const auto&) { return 1.; }, 30);
            puffs.update(1. / 120, R, [](double) { return 0.; });
        }
    }
}
}  // namespace shadow_puffs_fixture

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.cursePinnedGatherStampCrackBreakAndTail") {
    constexpr int      ticks[]    = {0,   1,   24,  59,  60,  65,  66,  67,  73,  74,  75,  76,  96,  120, 180,
                                     205, 206, 207, 220, 237, 238, 239, 260, 300, 360, 407, 408, 409, 480};
    constexpr uint32_t sums[][58] = {
        {3066409413u, 1397557933u, 3066409413u, 809801581u,  3656610464u, 1037750310u, 2332885861u, 1625845856u,
         2601301226u, 3492107057u, 3673384929u, 4277333617u, 2814225161u, 855170739u,  82913209u,   313326458u,
         262616537u,  2833026009u, 3877646217u, 4102434864u, 4094312654u, 1169439335u, 2514121401u, 3111189120u,
         2801330240u, 3243086510u, 2710573178u, 3536738364u, 3463798286u, 3149133484u, 2049297271u, 2854165370u,
         2816358665u, 1072156035u, 3661048271u, 440592154u,  2432422399u, 1375671761u, 499708185u,  1642438220u,
         1967908993u, 1224094228u, 1646395074u, 2425070981u, 2489291352u, 1308584977u, 1371972446u, 2406239674u,
         2307796240u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u,
         3066409413u, 3066409413u},
        {3066409413u, 3263830005u, 3066409413u, 574082997u,  1728104062u, 2367608214u, 3192084537u, 3661726717u,
         2947653943u, 1561551429u, 3023998404u, 2775791342u, 1815382792u, 319158894u,  3267917624u, 3499993186u,
         3248745895u, 183916944u,  4088883387u, 1516334415u, 2084178818u, 2477685905u, 437340631u,  3603733413u,
         138466570u,  142076113u,  2730378178u, 1163341910u, 1140765000u, 3828429158u, 73286716u,   2367564270u,
         1396773170u, 3415974146u, 2909148689u, 4000140362u, 1479355417u, 1234961277u, 2484270990u, 908014214u,
         1601811035u, 2844145367u, 1558209796u, 1080251921u, 2118514487u, 2726734755u, 2759445766u, 3664301314u,
         2490251224u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u, 3066409413u,
         3066409413u, 3066409413u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::Curse);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Curse, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.cursePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Curse), reference(PixelFxKind::Curse);
        double  elapsed = 0;
        for (double target :
             {.3, .499, .5, .549, .55, .559, .617, .65, .97, 1.719, 1.72, 1.979, 1.98, 1.989, 2.3, 3.4, 4., 5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.shadow.curseBoneOwnershipBoundsAndRoundtrip") {
    PixelFx fx(PixelFxKind::Curse);
    auto    settings     = fx.settings();
    settings.shadow.bone = {0xff102030u};
    auto prepared        = PixelFx::prepareConfiguration(PixelFxKind::Curse, settings);
    REQUIRE(prepared);
    settings.shadow.bone[0] = 0xffffffffu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().shadow.bone == std::vector<uint32_t>(5, 0xff102030u));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Curse, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.7);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    for (size_t size : {size_t(0), size_t(65)}) {
        settings = fx.settings();
        settings.shadow.bone.assign(size, WHITE);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Curse, settings));
        REQUIRE(fx.configurationValue() == value);
        REQUIRE(fx.effectTime() == .7);
        REQUIRE(fingerprint(fx.color()) == color);
        REQUIRE(fingerprint(fx.light()) == light);
    }
    settings = fx.settings();
    settings.shadow.bone.assign(64, WHITE);
    auto max = PixelFx::prepareConfiguration(PixelFxKind::Curse, settings);
    REQUIRE(max);
    REQUIRE(fx.commitConfiguration(std::move(max).takeValue()));
    REQUIRE(fx.settings().shadow.bone == std::vector<uint32_t>(5, WHITE));
}
TEST_CASE("stylize.pixelfx.shadow.curseOptionalActorOwnershipAndAtomicFailure") {
    PixelFx fx(PixelFxKind::Curse);
    auto    settings      = fx.settings();
    settings.seed         = 123;
    settings.actor.source = PixelFxActorSource::None;
    auto absent           = PixelFx::prepareConfiguration(PixelFxKind::Curse, settings);
    REQUIRE(absent);
    REQUIRE(fx.commitConfiguration(std::move(absent).takeValue()));
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::all_of(body.begin(), body.end(), [](uint32_t c) { return c == 0; }));
    settings              = PixelFx::defaults(PixelFxKind::Curse);
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 1;
    settings.actor.height = 1;
    settings.actor.mask   = {1};
    settings.actor.pixels = {0xffa0b0c0u};
    settings.actor.eyes.clear();
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::Curse, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    settings.actor.pixels[0] = 0;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().actor.pixels[0] == 0xffa0b0c0u);
    const auto config = fx.configurationValue();
    auto       seek   = fx.prepareSeek(.7);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.actor.mask.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Curse, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::Curse, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
}
TEST_CASE("stylize.pixelfx.shadow.curseBodyFloodLayerOrderAndReverse") {
    PixelFx    fx(PixelFxKind::Curse);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.3, .55, .617, .7, 1., 1.72, 1.8, 1.98, 2.3, 3.4, 4., 1.8}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        if (time == 1.)
            REQUIRE(std::any_of(fx.color().begin(), fx.color().end(),
                                [](auto c) { return (c >> 24) > 0 && (c >> 24) < 255; }));
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == 1.8) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_shadow_unit


namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.sourceBlackStarRasterSpinesRimAndTransformedUnlight") {
    struct Fixture {
        double   length, rotation;
        int      variant;
        uint32_t color, light, mask;
    };
    constexpr Fixture fixtures[] = {
        {0, 0, 0, 1071521221u, 824497745u, 1007681701u},        {0, 0, 1, 1071521221u, 923233189u, 1007681701u},
        {0, 0, 2, 1071521221u, 493260163u, 1007681701u},        {0, 0.32, 0, 1071521221u, 824497745u, 1007681701u},
        {0, 0.32, 1, 1071521221u, 923233189u, 1007681701u},     {0, 0.32, 2, 1071521221u, 493260163u, 1007681701u},
        {0, 1.571, 0, 1071521221u, 824497745u, 1007681701u},    {0, 1.571, 1, 1071521221u, 923233189u, 1007681701u},
        {0, 1.571, 2, 1071521221u, 493260163u, 1007681701u},    {0.4, 0, 0, 1071521221u, 824497745u, 1007681701u},
        {0.4, 0, 1, 1071521221u, 923233189u, 1007681701u},      {0.4, 0, 2, 1071521221u, 493260163u, 1007681701u},
        {0.4, 0.32, 0, 1071521221u, 824497745u, 1007681701u},   {0.4, 0.32, 1, 1071521221u, 923233189u, 1007681701u},
        {0.4, 0.32, 2, 1071521221u, 493260163u, 1007681701u},   {0.4, 1.571, 0, 1071521221u, 824497745u, 1007681701u},
        {0.4, 1.571, 1, 1071521221u, 923233189u, 1007681701u},  {0.4, 1.571, 2, 1071521221u, 493260163u, 1007681701u},
        {1, 0, 0, 1071521221u, 824497745u, 1007681701u},        {1, 0, 1, 1071521221u, 923233189u, 1007681701u},
        {1, 0, 2, 1071521221u, 493260163u, 1007681701u},        {1, 0.32, 0, 1071521221u, 824497745u, 1007681701u},
        {1, 0.32, 1, 1071521221u, 923233189u, 1007681701u},     {1, 0.32, 2, 1071521221u, 493260163u, 1007681701u},
        {1, 1.571, 0, 1071521221u, 824497745u, 1007681701u},    {1, 1.571, 1, 1071521221u, 923233189u, 1007681701u},
        {1, 1.571, 2, 1071521221u, 493260163u, 1007681701u},    {2.5, 0, 0, 2652283093u, 3745736585u, 2892564263u},
        {2.5, 0, 1, 2950420181u, 3161139185u, 2892564263u},     {2.5, 0, 2, 1071521221u, 493260163u, 2892564263u},
        {2.5, 0.32, 0, 3620803781u, 230094718u, 438094241u},    {2.5, 0.32, 1, 1372695589u, 1650273442u, 438094241u},
        {2.5, 0.32, 2, 1071521221u, 493260163u, 438094241u},    {2.5, 1.571, 0, 4209727141u, 2511060225u, 4107473793u},
        {2.5, 1.571, 1, 2878038693u, 1179412561u, 4107473793u}, {2.5, 1.571, 2, 1071521221u, 493260163u, 4107473793u},
        {16.3, 0, 0, 3905542709u, 1709895363u, 556266195u},     {16.3, 0, 1, 3548536481u, 1844877620u, 556266195u},
        {16.3, 0, 2, 1071521221u, 493260163u, 556266195u},      {16.3, 0.32, 0, 3713219365u, 423334490u, 2203436801u},
        {16.3, 0.32, 1, 4205206661u, 2950024833u, 2203436801u}, {16.3, 0.32, 2, 74956809u, 1038453987u, 2203436801u},
        {16.3, 1.571, 0, 141340389u, 3322584850u, 2709287985u}, {16.3, 1.571, 1, 98339137u, 4278075014u, 2709287985u},
        {16.3, 1.571, 2, 1071521221u, 493260163u, 2709287985u}, {60, 0, 0, 831593605u, 1956932173u, 1645765053u},
        {60, 0, 1, 684567325u, 3115848569u, 1645765053u},       {60, 0, 2, 2994671937u, 1961988579u, 1645765053u},
        {60, 0.32, 0, 2329674373u, 2348407530u, 613488869u},    {60, 0.32, 1, 542611677u, 153657074u, 613488869u},
        {60, 0.32, 2, 2646453281u, 2102875892u, 613488869u},    {60, 1.571, 0, 4143997957u, 2781706089u, 3100487981u},
        {60, 1.571, 1, 2351297145u, 2246944427u, 3100487981u},  {60, 1.571, 2, 3753473609u, 1365061828u, 3100487981u},
    };
    PixelFx           fx(PixelFxKind::BlackHole);
    const auto        C = fx.settings().palette;
    const PixelFxInfo info{PixelFxKind::BlackHole, "shadow/geometry-fixture", 192, 160, 96, 128, 4, true};
    for (const auto& f : fixtures) {
        const shadow::BlackStar star(-35.25, f.length, f.rotation, .5, .4);
        std::vector<uint32_t>   c(192 * 160), l(c.size());
        Paint                   p{info, c, l, 1};
        if (f.variant == 1) p.place(3.25, -1.5, true, .2);
        if (f.variant == 2) p.place(-43.5, 72.25, false, -.7);
        p.glow(0, -35.25, 80, C[5], .9);
        shadow::ringLight(p, 0, -35.25, 23.4, 4.5, C[6], .7, [](double, double y) { return y < -30; });
        star.draw(p, C[0], C[5], [](double x, double y) { return x * x + (y + 35.25) * (y + 35.25) < 11.3 * 11.3; });
        star.unlight(p);
        uint32_t mask = 2166136261u;
        for (int y = -100; y < 30; ++y)
            for (int x = -70; x < 70; ++x) mask = (mask ^ uint32_t(star.isDarkCell(x + .25, y + .75))) * 16777619u;
        REQUIRE(mask == f.mask);
        REQUIRE(fingerprint(c) == f.color);
        REQUIRE(fingerprint(l) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.shadow.sourceObsidianShardLightThresholdsFadeAndRepeatedRamps") {
    struct Fixture {
        int      palette;
        double   light, life;
        uint32_t color, glow;
    };
    constexpr Fixture fixtures[] = {
        {0, 0, 0.12, 2246865489u, 2999778757u},     {0, 0, 0.7, 2154629419u, 2999778757u},
        {0, 0.04, 0.12, 2246865489u, 2999778757u},  {0, 0.04, 0.7, 2154629419u, 2999778757u},
        {0, 0.041, 0.12, 2246865489u, 2999778757u}, {0, 0.041, 0.7, 2154629419u, 3900204413u},
        {0, 0.08, 0.12, 2246865489u, 2076327100u},  {0, 0.08, 0.7, 2154629419u, 2053982883u},
        {0, 0.081, 0.12, 3073403425u, 2076327100u}, {0, 0.081, 0.7, 2150064604u, 183885477u},
        {0, 0.3, 0.12, 3073403425u, 3119523270u},   {0, 0.3, 0.7, 2150064604u, 3449812022u},
        {0, 0.301, 0.12, 4271671677u, 745983837u},  {0, 0.301, 0.7, 2420190797u, 1151390958u},
        {0, 0.6, 0.12, 4271671677u, 441462527u},    {0, 0.6, 0.7, 2420190797u, 2337517744u},
        {0, 0.601, 0.12, 2473432226u, 3892043751u}, {0, 0.601, 0.7, 3695220895u, 463260342u},
        {0, 1, 0.12, 2473432226u, 3449812022u},     {0, 1, 0.7, 3695220895u, 726215463u},
        {1, 0, 0.12, 3211560577u, 2999778757u},     {1, 0, 0.7, 3575677325u, 2999778757u},
        {1, 0.04, 0.12, 3211560577u, 2999778757u},  {1, 0.04, 0.7, 3575677325u, 2999778757u},
        {1, 0.041, 0.12, 3211560577u, 2999778757u}, {1, 0.041, 0.7, 3575677325u, 522703829u},
        {1, 0.08, 0.12, 3211560577u, 1792237509u},  {1, 0.08, 0.7, 3575677325u, 563394162u},
        {1, 0.081, 0.12, 3211560577u, 1792237509u}, {1, 0.081, 0.7, 3575677325u, 2550005523u},
        {1, 0.3, 0.12, 3211560577u, 3920932004u},   {1, 0.3, 0.7, 3575677325u, 2972033690u},
        {1, 0.301, 0.12, 267822347u, 4116040775u},  {1, 0.301, 0.7, 2343897279u, 3529782485u},
        {1, 0.6, 0.12, 267822347u, 2940958469u},    {1, 0.6, 0.7, 2343897279u, 638073460u},
        {1, 0.601, 0.12, 267822347u, 2522845711u},  {1, 0.601, 0.7, 2343897279u, 4163071916u},
        {1, 1, 0.12, 267822347u, 2972033690u},      {1, 1, 0.7, 2343897279u, 1838241147u},
        {2, 0, 0.12, 3073403425u, 2999778757u},     {2, 0, 0.7, 2390260475u, 2999778757u},
        {2, 0.04, 0.12, 3073403425u, 2999778757u},  {2, 0.04, 0.7, 2390260475u, 2999778757u},
        {2, 0.041, 0.12, 3073403425u, 2999778757u}, {2, 0.041, 0.7, 2390260475u, 522703829u},
        {2, 0.08, 0.12, 3073403425u, 1792237509u},  {2, 0.08, 0.7, 2390260475u, 563394162u},
        {2, 0.081, 0.12, 3073403425u, 1792237509u}, {2, 0.081, 0.7, 2390260475u, 2550005523u},
        {2, 0.3, 0.12, 3073403425u, 3920932004u},   {2, 0.3, 0.7, 2390260475u, 2972033690u},
        {2, 0.301, 0.12, 2320251419u, 4116040775u}, {2, 0.301, 0.7, 874400429u, 3529782485u},
        {2, 0.6, 0.12, 2320251419u, 2940958469u},   {2, 0.6, 0.7, 874400429u, 638073460u},
        {2, 0.601, 0.12, 2473432226u, 2522845711u}, {2, 0.601, 0.7, 3347001401u, 4163071916u},
        {2, 1, 0.12, 2473432226u, 2972033690u},     {2, 1, 0.7, 3347001401u, 1838241147u},
    };
    PixelFx           fx(PixelFxKind::BlackHole);
    const auto        C = fx.settings().palette;
    const PixelFxInfo info{PixelFxKind::BlackHole, "shadow/shard-fixture", 96, 80, 48, 64, 4, true};
    for (const auto& f : fixtures) {
        auto ramp = C;
        if (f.palette == 1) ramp.assign(8, C[4]);
        if (f.palette == 2) ramp = {C[0], C[1], C[2], C[4], C[4], C[4], C[6], C[7]};
        std::vector<uint32_t> c(96 * 80), l(c.size());
        Paint                 p{info, c, l, 1};
        p.place(3.25, -1.5, true, .2);
        Debris b;
        b.x     = 2.25;
        b.y     = -23.4;
        b.angle = .71;
        b.size  = 8.3;
        b.life  = f.life;
        shadow::shard(p, b, ramp, -17, -42, f.light);
        REQUIRE(fingerprint(c) == f.color);
        REQUIRE(fingerprint(l) == f.glow);
    }
}
TEST_CASE("stylize.pixelfx.shadow.blackStarDestinationClipPreservesOutsideLight") {
    const PixelFxInfo       info{PixelFxKind::BlackHole, "shadow/star-clip", 96, 80, 48, 64, 4, true};
    const shadow::BlackStar star(-24, 32, .5, .5, .4);
    for (bool transformed : {false, true}) {
        std::vector<uint32_t> c(96 * 80), full(c.size(), 0xff123456u), clipped = full;
        Paint                 all{info, c, full, 1}, clip{info, c, clipped, 1};
        if (transformed) {
            all.place(3.25, -1.5, true, .2);
            clip.place(3.25, -1.5, true, .2);
        }
        clip.clipRect = Paint::ClipRect{-13, -40, 15, -8};
        star.unlight(all);
        star.unlight(clip);
        size_t changed = 0, protectedCells = 0;
        for (int y = -64; y < 16; ++y)
            for (int x = -48; x < 48; ++x) {
                const auto i = all.index(x, y);
                if (clip.isInsideClip(x, y)) {
                    REQUIRE(clipped[i] == full[i]);
                    if (clipped[i] == 0) ++changed;
                } else {
                    REQUIRE(clipped[i] == 0xff123456u);
                    if (full[i] == 0) ++protectedCells;
                }
            }
        REQUIRE(changed > 0);
        REQUIRE(protectedCells > 0);
    }
}
}  // namespace test_pixelfx_shadow_unit

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.darkExplosionPinnedChargeStarCollapsePopAndTail") {
    constexpr int      ticks[]    = {0,   1,   24,  54,  55,  61,  62,  63,  64,  68,  69,  74,  75,  93,  94,
                                     120, 180, 194, 195, 196, 220, 221, 222, 260, 300, 360, 455, 456, 457, 600};
    constexpr uint32_t sums[][60] = {
        {1329891111u, 3404740393u, 1075145018u, 1755891346u, 1973782546u, 212159589u,  4269281720u, 1314641135u,
         2452681346u, 4276359487u, 532513231u,  1714603013u, 2194342553u, 761259772u,  1662381566u, 607524532u,
         1076609842u, 3105201124u, 1609961159u, 273096345u,  1100888912u, 1725701769u, 1856443114u, 3989008836u,
         944362755u,  209091898u,  3605633466u, 2834136860u, 1604438246u, 2266887743u, 1529093490u, 438506557u,
         1569565862u, 3048621774u, 1591593046u, 3909790416u, 2572659300u, 2367574162u, 1067358673u, 3357955818u,
         3210383868u, 2151803369u, 3456268277u, 2163570925u, 1969021065u, 434329028u,  853734904u,  3055941251u,
         1145860317u, 2976262869u, 1708256557u, 1217648845u, 1217003973u, 1217003973u, 1217003973u, 1217003973u,
         1217003973u, 1217003973u, 1217003973u, 1217003973u},
        {1091532415u, 4029445362u, 700524451u,  994609157u,  25882255u,   48671447u,   4222416087u, 1851933231u,
         848304993u,  2258637618u, 3135892526u, 718580079u,  2490666947u, 1669652699u, 4149487907u, 2687393427u,
         3415909574u, 1689109861u, 532697175u,  632070766u,  2119097202u, 365098660u,  1440179242u, 2078249002u,
         1161113520u, 503508270u,  3019529928u, 1309852059u, 1766275599u, 260789600u,  2316336329u, 4161307630u,
         91449048u,   1383663778u, 3705211278u, 1897512983u, 3105272280u, 2382131460u, 3528707241u, 4232072101u,
         2068651924u, 2686266054u, 2461938314u, 1732729099u, 376014676u,  2936610236u, 1467929237u, 4235759013u,
         2081197504u, 4291177211u, 3623013722u, 3417613327u, 1217003973u, 1217003973u, 1217003973u, 1217003973u,
         1217003973u, 1217003973u, 1217003973u, 1217003973u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::DarkExplosion);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::DarkExplosion, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.darkExplosionPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::DarkExplosion), reference(PixelFxKind::DarkExplosion);
        double  elapsed = 0;
        for (double target : {.3,    .449, .45,   .459,  .46,  .519,  .52, .525, .57, .78, .8,
                              1.619, 1.62, 1.629, 1.839, 1.84, 1.849, 2.2, 3.8,  4.,  5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.shadow.darkExplosionOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::DarkExplosion);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::DarkExplosion, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::DarkExplosion, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.8);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DarkExplosion, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.darkExplosionLastUnlightLayerOrderAndReleaseReverse") {
    PixelFx    fx(PixelFxKind::DarkExplosion);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.3, .519, .525, .57, .65, .78, 1., 1.62, 1.841667, 2.3, 3.8, 5., .57}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        if (time == 1.) REQUIRE(fx.light()[size_t(206 - 54) * 280 + 140] == 0);
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .57) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_shadow_unit

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.eclipsePinnedStreamersCoronaBeadAndTwoLoops") {
    constexpr int      ticks[]    = {0,   1,   24,  60,  120, 180, 239, 240, 241, 300,
                                     360, 420, 479, 480, 481, 600, 720, 840, 959, 960};
    constexpr uint32_t sums[][40] = {
        {3607569789u, 273650525u,  1236376842u, 831400926u,  2420007113u, 3923885124u, 2647839266u, 2733632054u,
         2293579882u, 1224196964u, 377376144u,  2195527822u, 3728128496u, 3332125054u, 182011724u,  3053369930u,
         961535002u,  2308912715u, 3141974230u, 3898584703u, 836059203u,  3393898140u, 2826002158u, 964828971u,
         3675686900u, 1806347457u, 3607569789u, 273650525u,  1236376842u, 831400926u,  2293579882u, 1224196964u,
         182011724u,  3053369930u, 836059203u,  3393898140u, 3675686900u, 1806347457u, 3607569789u, 273650525u},
        {2352165067u, 174276481u,  1961216316u, 1399828139u, 2883759379u, 4098377426u, 556142644u,  3201967688u,
         2026333833u, 1616231367u, 444080285u,  1838402227u, 3382373434u, 3305179140u, 665387662u,  3525788368u,
         1594465021u, 3268906451u, 3325262937u, 895370896u,  343426293u,  1109768270u, 3616469842u, 1901895723u,
         2706837730u, 3400564295u, 2352165067u, 174276481u,  1961216316u, 1399828139u, 2026333833u, 1616231367u,
         665387662u,  3525788368u, 343426293u,  1109768270u, 2706837730u, 3400564295u, 2352165067u, 174276481u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::Eclipse);
        REQUIRE(!fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.eclipseAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Eclipse), reference(PixelFxKind::Eclipse);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.999, 4., 4.001, 8., 8.113}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            REQUIRE(std::abs(live.effectTime() - target) < 1e-12);
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
    PixelFx fx(PixelFxKind::Eclipse);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Eclipse, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.eclipsePearlOwnershipBoundsAndRoundtrip") {
    PixelFx fx(PixelFxKind::Eclipse);
    auto    settings      = fx.settings();
    settings.shadow.pearl = {0xff102030u};
    auto prepared         = PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings);
    REQUIRE(prepared);
    settings.shadow.pearl[0] = 0xffffffffu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().shadow.pearl == std::vector<uint32_t>(4, 0xff102030u));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Eclipse, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.7);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    for (size_t size : {size_t(0), size_t(65)}) {
        settings = fx.settings();
        settings.shadow.pearl.assign(size, WHITE);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings));
        REQUIRE(fx.configurationValue() == value);
        REQUIRE(fx.effectTime() == .7);
        REQUIRE(fingerprint(fx.color()) == color);
        REQUIRE(fingerprint(fx.light()) == light);
    }
    settings = fx.settings();
    settings.shadow.pearl.assign(64, WHITE);
    auto max = PixelFx::prepareConfiguration(PixelFxKind::Eclipse, settings);
    REQUIRE(max);
    REQUIRE(fx.commitConfiguration(std::move(max).takeValue()));
    REQUIRE(fx.settings().shadow.pearl == std::vector<uint32_t>(4, WHITE));
}
TEST_CASE("stylize.pixelfx.shadow.eclipseLastUnlightLayerCompositionAndReverse") {
    PixelFx    fx(PixelFxKind::Eclipse);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.3, 1., 2., 3.999999, 4., 8., .3}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.light()[size_t(72) * 160 + 80] == 0);
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_shadow_unit

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.graspingHandsPinnedEmergenceClenchRetreatPoolAndTail") {
    constexpr int      ticks[]    = {0,   1,   24,  35,  36,  40,  45,  48,  55,  60,  72,  84,  96,  120, 160,
                                     180, 200, 220, 240, 260, 280, 300, 335, 336, 337, 371, 372, 373, 420, 480};
    constexpr uint32_t sums[][60] = {
        {867221445u,  867221445u,  3378897473u, 2722905621u, 825225303u,  3636336069u, 3339735356u, 2581488036u,
         2800986043u, 3257019633u, 1088079277u, 3418468669u, 1011345026u, 4287912473u, 2991264760u, 1801344668u,
         1179798640u, 2567859445u, 3144982354u, 658726385u,  167622243u,  1113601432u, 3545225130u, 1016589904u,
         1021409175u, 3816556778u, 864020614u,  1301842601u, 4057014469u, 2854639037u, 944726372u,  1561583485u,
         994987357u,  102799229u,  3319386432u, 4044051898u, 3285384001u, 3927849406u, 1736383186u, 2401844697u,
         1094619615u, 214827935u,  164564343u,  2677180901u, 3286418053u, 3879749385u, 3101890125u, 867221445u,
         3101890125u, 867221445u,  867221445u,  867221445u,  867221445u,  867221445u,  867221445u,  867221445u,
         867221445u,  867221445u,  867221445u,  867221445u},
        {867221445u,  867221445u,  1247212901u, 1326229229u, 105947934u,  478957172u,  3190152213u, 1895999354u,
         2894758499u, 3225267994u, 3539563334u, 2530357687u, 3775066796u, 2305371388u, 858531427u,  2996143820u,
         110903078u,  2419525106u, 2372381886u, 1481041672u, 4139273758u, 3086143779u, 3444119246u, 2939673696u,
         3838495698u, 3764483450u, 2529374058u, 2601314015u, 2878777627u, 2031517520u, 2981304656u, 3260753718u,
         3158705864u, 263658075u,  2538611479u, 3924264811u, 3472281499u, 2772541456u, 966566644u,  2808822222u,
         833296018u,  1845760665u, 1898644824u, 886342143u,  2145110847u, 182012485u,  4101786975u, 867221445u,
         4101786975u, 867221445u,  867221445u,  867221445u,  867221445u,  867221445u,  867221445u,  867221445u,
         867221445u,  867221445u,  867221445u,  867221445u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::GraspingHands);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::GraspingHands, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.graspingHandsPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::GraspingHands), reference(PixelFxKind::GraspingHands);
        double  elapsed = 0;
        for (double target :
             {.13, .299, .3, .32, .45, .6, .8, 1., 1.45, 1.7, 1.95, 2.1, 2.349, 2.35, 2.8, 3.1, 4., 5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.shadow.graspingHandsOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::GraspingHands);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::GraspingHands, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::GraspingHands, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.8);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::GraspingHands, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.graspingHandsOrderedPoolArmLayersAndReverse") {
    PixelFx    fx(PixelFxKind::GraspingHands);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.13, .32, .45, .6, .8, 1., 1.45, 1.7, 1.95, 2.35, 2.8, 3.1, 4., .8}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();

        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .8) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.sourceInkDropsImpactStateAndReverseOrder") {
    shadow::Drops drops;
    for (const shadow::Drop d : std::array<shadow::Drop, 5>{
             {{0, 0, 1, 1, 1}, {-2, -1, 0, 10, 2}, {2, -.01, 0, 1, 1}, {5, 1, 0, 2, 2}, {20, -40, 20, -90, 2.2}}})
        drops.add(d);
    struct Impact {
        double x, y, vy, size, time;
    };
    std::vector<Impact> impacts;
    double              time = 0;
    for (int tick = 0; tick <= 120; ++tick) {
        switch (tick) {
            case 0: {
                REQUIRE(drops.list.size() == size_t(5));
                REQUIRE(impacts.size() == size_t(0));
                REQUIRE(std::abs(drops.list[0].x - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].y - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vx - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vy - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].size - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].x - (-2)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].y - (-1)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vy - (10)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].size - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].y - (-0.01)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].vy - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[3].x - (5)) < 1e-11);
                REQUIRE(std::abs(drops.list[3].y - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[3].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[3].vy - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[4].x - (20)) < 1e-11);
                REQUIRE(std::abs(drops.list[4].y - (-40)) < 1e-11);
                REQUIRE(std::abs(drops.list[4].vx - (20)) < 1e-11);
                REQUIRE(std::abs(drops.list[4].vy - (-90)) < 1e-11);
                REQUIRE(std::abs(drops.list[4].size - (2.2000000000000002)) < 1e-11);
                break;
            }
            case 1: {
                REQUIRE(drops.list.size() == size_t(3));
                REQUIRE(impacts.size() == size_t(2));
                REQUIRE(std::abs(drops.list[0].x - (-2)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].y - (-0.86388888888888893)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vy - (16.333333333333332)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].x - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].y - (0.051111111111111107)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].x - (20.16611111111111)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].y - (-40.697222222222223)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].vx - (19.933333333333334)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].vy - (-83.666666666666671)) < 1e-11);
                REQUIRE(std::abs(drops.list[2].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                break;
            }
            case 2: {
                REQUIRE(drops.list.size() == size_t(2));
                REQUIRE(impacts.size() == size_t(3));
                REQUIRE(std::abs(drops.list[0].x - (-2)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].y - (-0.67500000000000004)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vx - (0)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vy - (22.666666666666664)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].x - (20.331668518518519)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].y - (-41.341666666666669)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vx - (19.866888888888891)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].vy - (-77.333333333333343)) < 1e-11);
                REQUIRE(std::abs(drops.list[1].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                break;
            }
            case 12: {
                REQUIRE(drops.list.size() == size_t(1));
                REQUIRE(impacts.size() == size_t(4));
                REQUIRE(std::abs(drops.list[0].x - (21.957191909080226)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].y - (-44.883333333333333)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vx - (19.214504919432684)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vy - (-14.000000000000011)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[3].x - (-2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].y - (-0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[3].vy - (41.666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].time - (0.041666666666666664)) < 1e-11);
                break;
            }
            case 36: {
                REQUIRE(drops.list.size() == size_t(1));
                REQUIRE(impacts.size() == size_t(4));
                REQUIRE(std::abs(drops.list[0].x - (25.643990015652246)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].y - (-31.850000000000009)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vx - (17.734853505423839)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].vy - (137.99999999999997)) < 1e-11);
                REQUIRE(std::abs(drops.list[0].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[3].x - (-2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].y - (-0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[3].vy - (41.666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].time - (0.041666666666666664)) < 1e-11);
                break;
            }
            case 60: {
                REQUIRE(drops.list.size() == size_t(0));
                REQUIRE(impacts.size() == size_t(5));
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[3].x - (-2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].y - (-0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[3].vy - (41.666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].time - (0.041666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[4].x - (28.498497243264872)) < 1e-11);
                REQUIRE(std::abs(impacts[4].y - (0.85495491729794615)) < 1e-11);
                REQUIRE(std::abs(impacts[4].vy - (264.6666666666668)) < 1e-11);
                REQUIRE(std::abs(impacts[4].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[4].time - (0.46666666666666728)) < 1e-11);
                break;
            }
            case 90: {
                REQUIRE(drops.list.size() == size_t(0));
                REQUIRE(impacts.size() == size_t(5));
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[3].x - (-2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].y - (-0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[3].vy - (41.666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].time - (0.041666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[4].x - (28.498497243264872)) < 1e-11);
                REQUIRE(std::abs(impacts[4].y - (0.85495491729794615)) < 1e-11);
                REQUIRE(std::abs(impacts[4].vy - (264.6666666666668)) < 1e-11);
                REQUIRE(std::abs(impacts[4].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[4].time - (0.46666666666666728)) < 1e-11);
                break;
            }
            case 120: {
                REQUIRE(drops.list.size() == size_t(0));
                REQUIRE(impacts.size() == size_t(5));
                REQUIRE(std::abs(impacts[0].x - (5)) < 1e-11);
                REQUIRE(std::abs(impacts[0].y - (0.14999999999999999)) < 1e-11);
                REQUIRE(std::abs(impacts[0].vy - (8.3333333333333321)) < 1e-11);
                REQUIRE(std::abs(impacts[0].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[0].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[1].x - (0.0083055555555555556)) < 1e-11);
                REQUIRE(std::abs(impacts[1].y - (0.00024916666666666663)) < 1e-11);
                REQUIRE(std::abs(impacts[1].vy - (7.333333333333333)) < 1e-11);
                REQUIRE(std::abs(impacts[1].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[1].time - (0.0083333333333333332)) < 1e-11);
                REQUIRE(std::abs(impacts[2].x - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[2].y - (0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[2].vy - (13.666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[2].size - (1)) < 1e-11);
                REQUIRE(std::abs(impacts[2].time - (0.016666666666666666)) < 1e-11);
                REQUIRE(std::abs(impacts[3].x - (-2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].y - (-0.059999999999999998)) < 1e-11);
                REQUIRE(std::abs(impacts[3].vy - (41.666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[3].size - (2)) < 1e-11);
                REQUIRE(std::abs(impacts[3].time - (0.041666666666666664)) < 1e-11);
                REQUIRE(std::abs(impacts[4].x - (28.498497243264872)) < 1e-11);
                REQUIRE(std::abs(impacts[4].y - (0.85495491729794615)) < 1e-11);
                REQUIRE(std::abs(impacts[4].vy - (264.6666666666668)) < 1e-11);
                REQUIRE(std::abs(impacts[4].size - (2.2000000000000002)) < 1e-11);
                REQUIRE(std::abs(impacts[4].time - (0.46666666666666728)) < 1e-11);
                break;
            }
            default: break;
        }
        if (tick < 120) {
            time += 1. / 120;
            drops.update(
                1. / 120, [](double x) { return x * .03; },
                [&](double x, double y, double vy, double size) { impacts.push_back({x, y, vy, size, time}); });
        }
    }
}
}  // namespace test_pixelfx_shadow_unit

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.hexSigilPinnedPenGlyphPopWhiteFlareCrumbleAndTail") {
    constexpr int      ticks[]    = {0,   1,   14,  15,  24,  60,  120, 180, 188, 189, 194, 195, 203, 212, 220, 228,
                                     229, 237, 245, 300, 311, 312, 313, 328, 329, 330, 360, 420, 503, 504, 505, 600};
    constexpr uint32_t sums[][64] = {
        {1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 601802919u,  3192343795u,
         2801684585u, 259335382u,  9478280u,    2529318150u, 115048088u,  2022226518u, 3095201263u, 3965434973u,
         1655411373u, 1794710732u, 2581792058u, 1126127513u, 440560411u,  338678891u,  3668089837u, 3524313788u,
         905018938u,  1250214406u, 897183733u,  3862944467u, 1767585471u, 3857427080u, 792149699u,  791122149u,
         1779420686u, 1832363650u, 972290079u,  2263494832u, 361589073u,  2739502198u, 1230483372u, 2121697738u,
         687775775u,  920360586u,  2050586102u, 1442276391u, 317372279u,  1610739629u, 4263638100u, 506390145u,
         1515862832u, 2138134854u, 2514833226u, 1120077645u, 3874395293u, 2289113544u, 1257020312u, 2261994884u,
         1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u},
        {1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 2496188481u, 3116527680u,
         4284506157u, 100536806u,  3810072641u, 833058906u,  1922234519u, 4017924274u, 416111880u,  4214771761u,
         1383779077u, 3921915575u, 2958386527u, 2919907419u, 391850773u,  192762010u,  862645141u,  3996796371u,
         2503102330u, 934534110u,  509488871u,  1303976576u, 3988743064u, 265799507u,  650157783u,  1949811661u,
         3077463109u, 1455325940u, 818950934u,  1219436675u, 3745915080u, 293492007u,  2297020013u, 956616759u,
         4046820112u, 4221664961u, 2145458123u, 4268414878u, 1221361652u, 3409250075u, 560609508u,  1546652748u,
         3968713310u, 1867091868u, 1180404253u, 1610592186u, 3361524626u, 2042454121u, 1763148702u, 475807812u,
         1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u, 1265688005u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::HexSigil);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::HexSigil, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.hexSigilPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::HexSigil), reference(PixelFxKind::HexSigil);
        double  elapsed = 0;
        for (double target : {.119, .12,   .13, .3,    .8,    1.571, 1.572, 1.619, 1.62, 1.9,
                              2.04, 2.599, 2.6, 2.609, 2.739, 2.74,  2.749, 3.1,   4.2,  5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.shadow.hexSigilOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::HexSigil);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::HexSigil, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HexSigil, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.8);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HexSigil, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.hexSigilOrderedGlyphSmokeLayersAndFlareReverse") {
    PixelFx    fx(PixelFxKind::HexSigil);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.13, .3, .8, 1.62, 1.9, 2.04, 2.6, 2.609, 2.74, 2.749, 3.1, 4.2, 5., 2.609}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();

        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == 2.609) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_shadow_unit

namespace test_pixelfx_shadow_unit {
TEST_CASE("stylize.pixelfx.shadow.lifeStealPinnedHitHoverSevenArrivalsFlushAndTail") {
    constexpr int ticks[] = {0,   1,   12,  33,  34,  43,  44,  60,  80,  96,  97,  102, 103, 112, 113, 121, 122, 127,
                             128, 135, 136, 142, 143, 144, 145, 150, 180, 210, 246, 263, 264, 265, 300, 360, 480};
    constexpr uint32_t sums[][70] = {
        {3931427127u, 4201761173u, 1612131056u, 736914388u,  601200790u,  731018995u,  1322585769u, 1621301876u,
         1824161930u, 608141179u,  2240505351u, 645930138u,  1683024465u, 4095766386u, 3675562611u, 236245704u,
         543241745u,  726767156u,  4006534553u, 3853597608u, 3353813392u, 2744636157u, 2230212957u, 734616521u,
         2329428919u, 1209868470u, 692805363u,  2711151795u, 3448612030u, 4201736932u, 1840413080u, 1330668866u,
         1135266670u, 866971240u,  1833618291u, 3995110621u, 1615849408u, 813825689u,  3813498668u, 490843014u,
         229997368u,  2660513493u, 1413676967u, 2952062830u, 3644421061u, 3582998918u, 2769530595u, 2669181843u,
         1275800070u, 3834774470u, 220905569u,  2932273413u, 2307691715u, 2988006785u, 3538286912u, 558453952u,
         1755450739u, 3740422048u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u,
         3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u},
        {233622052u,  914805269u,  4277334264u, 3910881395u, 2147843046u, 2394694328u, 2028615888u, 4113680976u,
         914890996u,  3552256448u, 1905095534u, 3136646313u, 20833046u,   2151881476u, 684038065u,  974451090u,
         213523137u,  1270768580u, 3760120381u, 2321771384u, 728421567u,  3200979101u, 3161857772u, 603550217u,
         1872615776u, 1756217049u, 2509133055u, 541831006u,  2067872650u, 3234841736u, 3290253378u, 2024961682u,
         1535015016u, 4004681123u, 163022754u,  3578702140u, 3798904256u, 3699167957u, 1149880145u, 3975693687u,
         2424456253u, 2886227642u, 4279200870u, 3527622517u, 2853597582u, 1176226142u, 3435185555u, 1878447905u,
         55502943u,   3689993336u, 4292106789u, 4087930857u, 3724406695u, 2870190394u, 3346849660u, 259322156u,
         1848570869u, 3442692056u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u,
         3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u, 3870458821u}};
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::LifeSteal);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings);
            REQUIRE(prepared);
            REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.lifeStealPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::LifeSteal), reference(PixelFxKind::LifeSteal);
        double  elapsed = 0;
        for (double target : {.13, .359, .36, .4, .6, .8, .809, .9, 1., 1.2, 1.209, 1.5, 1.9, 2.05, 2.2, 3., 4., 5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.shadow.lifeStealOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::LifeSteal);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::LifeSteal, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.8);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.shadow.lifeStealOptionalActorOwnershipAndAtomicFailure") {
    PixelFx fx(PixelFxKind::LifeSteal);
    auto    settings      = fx.settings();
    settings.seed         = 123;
    settings.actor.source = PixelFxActorSource::None;
    auto absent           = PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings);
    REQUIRE(absent);
    REQUIRE(fx.commitConfiguration(std::move(absent).takeValue()));
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::all_of(body.begin(), body.end(), [](uint32_t c) { return c == 0; }));
    settings              = PixelFx::defaults(PixelFxKind::LifeSteal);
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 1;
    settings.actor.height = 1;
    settings.actor.mask   = {1};
    settings.actor.pixels = {0xffa0b0c0u};
    settings.actor.eyes.clear();
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    settings.actor.pixels[0] = 0;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().actor.pixels[0] == 0xffa0b0c0u);
    const auto config = fx.configurationValue();
    auto       seek   = fx.prepareSeek(.95);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.actor.mask.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::LifeSteal, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
}
TEST_CASE("stylize.pixelfx.shadow.lifeStealTranslucentBodyOrderedLayersAndArrivalReverse") {
    PixelFx    fx(PixelFxKind::LifeSteal);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {0., .13, .4, .8, .809, 1., 1.2, 1.209, 1.5, 1.9, 2.2, 3., 4., .809}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        if (time == 1.)
            REQUIRE(std::any_of(fx.color().begin(), fx.color().end(),
                                [](auto c) { return (c >> 24) > 0 && (c >> 24) < 255; }));
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .809) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.lifeStealReservedDarkBoundsRoundtripAndVisualNeutrality") {
    PixelFx fx(PixelFxKind::LifeSteal);
    auto    initial = fx.prepareSeek(1.);
    REQUIRE(initial);
    REQUIRE(fx.commitSeek(std::move(initial).takeValue()));
    fx.draw();
    const auto color = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               light = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    // Upstream declares dark but never reads it in this kernel. Preserve that contract explicitly.
    for (uint32_t dark : {0xff000000u, 0xffffffffu}) {
        auto settings        = fx.settings();
        settings.shadow.dark = dark;
        auto candidate       = PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        const auto  value   = fx.configurationValue();
        const auto* options = value.find("options");
        REQUIRE(options != nullptr);
        const auto* field = options->find("dark");
        REQUIRE(field != nullptr);
        REQUIRE(field->isInt64());
        REQUIRE(field->asInt() == int64_t(dark));
        auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::LifeSteal, value);
        REQUIRE(rt);
        REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
        REQUIRE(fx.configurationValue() == value);
        auto seek = fx.prepareSeek(1.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(std::equal(color.begin(), color.end(), fx.color().begin()));
        REQUIRE(std::equal(light.begin(), light.end(), fx.light().begin()));
        for (uint32_t invalid : {0u, 0x80112233u}) {
            settings             = fx.settings();
            settings.shadow.dark = invalid;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LifeSteal, settings));
            REQUIRE(fx.configurationValue() == value);
            REQUIRE(fx.effectTime() == 1.);
            REQUIRE(std::equal(color.begin(), color.end(), fx.color().begin()));
            REQUIRE(std::equal(light.begin(), light.end(), fx.light().begin()));
        }
    }
}

TEST_CASE("stylize.pixelfx.shadow.necroSkullsPinnedOrbitFlameAndLoopBoundaries") {
    // Independent pinned JS raw planes, not native-generated expected values.
    constexpr double   times[]       = {0,   0.008333333333333333,
                                        0.3, 0.6,
                                        0.9, 1.2,
                                        1.5, 1.8,
                                        2.1, 2.4,
                                        2.7, 2.9916666666666667,
                                        3,   3.0083333333333333,
                                        4.5, 5.991666666666666,
                                        6,   9};
    constexpr uint32_t expected[][2] = {
        {4148401633u, 635753716u},  {1387610299u, 2566451277u}, {2515413118u, 348972013u},  {3461172748u, 2778210514u},
        {3574661769u, 3577266533u}, {4045201452u, 968208690u},  {2604013788u, 1725104033u}, {1001459536u, 2575186238u},
        {1150869393u, 339295394u},  {2506567066u, 3156887451u}, {451725706u, 3331507894u},  {2002357093u, 290244452u},
        {4148401633u, 635753716u},  {1387610299u, 2566451277u}, {2604013788u, 1725104033u}, {2002357093u, 290244452u},
        {4148401633u, 635753716u},  {4148401633u, 635753716u}};
    PixelFx fx(PixelFxKind::NecroSkulls);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < std::size(times); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i][0]);
        REQUIRE(fingerprint(fx.light()) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.necroSkullsOwningBoneRampAndAtomicBounds") {
    PixelFx    fx(PixelFxKind::NecroSkulls);
    auto       settings = fx.settings();
    const auto defaults = settings.shadow.bone;
    REQUIRE(defaults.size() == 5);
    settings.shadow.bone = {0xff112233u};
    auto prepared        = PixelFx::prepareConfiguration(PixelFxKind::NecroSkulls, settings);
    REQUIRE(prepared);
    settings.shadow.bone[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.shadow.bone.size() == 5);
    for (uint32_t c : owned.shadow.bone) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::NecroSkulls, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.shadow.bone.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::NecroSkulls, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.shadow.bone = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::NecroSkulls, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.shadow.necroSkullsLayerCompositionReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::NecroSkulls);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.7, 3., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
        const auto            l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> composed(c.size()), lights(l.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, composed, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(composed == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}


TEST_CASE("stylize.pixelfx.shadow.shadowFlamesPinnedInvertedTonguesSmokeAndLoops") {
    // Independent pinned JS raw planes; expected values are never native-generated.
    constexpr double times[] = {
        0,   0.008333333333333333, 0.1, 0.25, 0.5, 1, 1.5, 2, 2.3916666666666666, 2.4, 2.408333333333333,
        3.6, 4.791666666666667,    4.8, 7.2};
    constexpr uint32_t expected[][2] = {
        {2667411652u, 359142279u},  {864345832u, 1611936411u},  {1079817422u, 3229769740u}, {1163908815u, 4102424922u},
        {918646197u, 1252036250u},  {1526289007u, 3512867143u}, {2590268676u, 3500053117u}, {1361650342u, 2670887880u},
        {3751857004u, 3697070048u}, {2667411652u, 359142279u},  {864345832u, 1611936411u},  {1108486183u, 3889584636u},
        {3751857004u, 3697070048u}, {2667411652u, 359142279u},  {2667411652u, 359142279u}};
    PixelFx fx(PixelFxKind::ShadowFlames);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < std::size(times); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i][0]);
        REQUIRE(fingerprint(fx.light()) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.shadowFlamesOwningPaletteAndAtomicBounds") {
    PixelFx fx(PixelFxKind::ShadowFlames);
    auto    settings = fx.settings();
    settings.palette = {0xff112233u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::ShadowFlames, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.palette.size() == 8);
    for (uint32_t c : owned.palette) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ShadowFlames, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.palette.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShadowFlames, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.palette = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShadowFlames, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.shadow.shadowFlamesLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::ShadowFlames);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.4, 4.8, .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
        const auto            l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> composed(c.size()), lights(l.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, composed, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(composed == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.shadowFlamesPinnedBlackHeartClearsTransformedIncomingLight") {
    // Exact tone=3 cells independently observed in pinned source at .5 seconds.
    constexpr int cells[][2] = {
        {-2, -13}, {-1, -13}, {-2, -12}, {-1, -12}, {-8, -10}, {-7, -10}, {-1, -10}, {6, -10}, {7, -10}, {8, -10},
        {-9, -9},  {-8, -9},  {-7, -9},  {-6, -9},  {-2, -9},  {-1, -9},  {0, -9},   {6, -9},  {7, -9},  {8, -9},
        {-9, -8},  {-8, -8},  {-7, -8},  {-6, -8},  {-2, -8},  {-1, -8},  {0, -8},   {-8, -7}, {-7, -7}, {-2, -7},
        {-1, -7},  {0, -7},   {1, -7},   {-9, -6},  {-8, -6},  {-7, -6},  {-6, -6},  {-1, -6}, {0, -6},  {1, -6},
        {6, -6},   {7, -6},   {11, -6},  {-8, -5},  {-7, -5},  {-6, -5},  {-1, -5},  {0, -5},  {1, -5},  {6, -5},
        {7, -5},   {11, -5},  {12, -5},  {13, -5},  {-12, -4}, {-8, -4},  {-7, -4},  {-6, -4}, {-1, -4}, {0, -4},
        {1, -4},   {6, -4},   {7, -4},   {8, -4},   {12, -4},  {-12, -3}, {-7, -3},  {-1, -3}, {0, -3},  {1, -3},
        {2, -3},   {7, -3},   {8, -3},   {12, -3},  {-13, -2}, {-8, -2},  {-7, -2},  {-2, -2}, {-1, -2}, {0, -2},
        {1, -2},   {2, -2},   {7, -2},   {8, -2},   {-12, -1}, {-11, -1}, {-8, -1},  {-7, -1}, {-6, -1}, {-5, -1},
        {-1, -1},  {0, -1},   {1, -1},   {5, -1},   {6, -1},   {7, -1},   {8, -1},   {11, -1}, {12, -1}};
    PixelFx fx(PixelFxKind::ShadowFlames);
    auto    seek = fx.prepareSeek(.5);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    for (bool transformed : {false, true}) {
        const PixelFxInfo     info{PixelFxKind::ShadowFlames, "shadow/flame-heart-fixture", 64, 48, 32, 32, 2.4, true};
        std::vector<uint32_t> c(64 * 48), l(c.size(), 0xff123456u);
        PixelFxCanvas         canvas{64, 48, 32, 32, c, l};
        PixelFxPlacement      placement;
        Paint                 p{info, c, l, 1};
        if (transformed) {
            placement = {7, -3, .2, true};
            p.place(7, -3, true, .2);
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Both, placement));
        size_t cleared = 0;
        for (const auto& cell : cells) {
            const int x = p.cellX(cell[0] + .5, cell[1] + .5), y = p.cellY(cell[0] + .5, cell[1] + .5);
            if (!p.contains(x, y)) continue;
            REQUIRE(l[p.index(x, y)] == 0);
            ++cleared;
        }
        REQUIRE(cleared > 0);
    }
}


TEST_CASE("stylize.pixelfx.shadow.shadowTendrilsPinnedDefinedSourcePhasesAndTail") {
    // Independent pinned JS raw planes; expected values are never native-generated.
    constexpr double   times[]       = {0,
                                        0.008333333333333333,
                                        0.30833333333333335,
                                        0.31666666666666665,
                                        0.36666666666666664,
                                        0.375,
                                        0.4,
                                        0.4083333333333333,
                                        0.43333333333333335,
                                        0.44166666666666665,
                                        0.48333333333333334,
                                        0.5,
                                        0.5416666666666666,
                                        0.55,
                                        0.5666666666666667,
                                        0.575,
                                        0.75,
                                        1,
                                        1.5,
                                        2,
                                        2.25,
                                        2.5,
                                        2.7,
                                        2.95,
                                        3.2,
                                        4,
                                        5};
    constexpr uint32_t expected[][2] = {
        {1961422789u, 1961422789u}, {1596773409u, 1421190229u}, {588660549u, 1213939801u},  {1889493335u, 1790675109u},
        {504868615u, 71338874u},    {1453893373u, 3767812639u}, {3025496244u, 220578839u},  {4173745362u, 3216794922u},
        {3678196343u, 312644212u},  {1952189299u, 2902013421u}, {2874542342u, 4033423118u}, {3345010635u, 2726539537u},
        {2603253623u, 1843007624u}, {4078255062u, 1146138026u}, {1889520598u, 1859968392u}, {1620175229u, 2125735538u},
        {138698878u, 2643921140u},  {393645007u, 205558383u},   {354636461u, 2400484968u},  {2866113568u, 1815765705u},
        {3958462541u, 804144705u},  {2451940137u, 3539549470u}, {622546174u, 494109201u},   {1961422789u, 1961422789u},
        {1961422789u, 1961422789u}, {1961422789u, 1961422789u}, {1961422789u, 1961422789u}};
    PixelFx fx(PixelFxKind::ShadowTendrils);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < std::size(times); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i][0]);
        REQUIRE(fingerprint(fx.light()) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.shadowTendrilsOwningPaletteAndAtomicBounds") {
    PixelFx fx(PixelFxKind::ShadowTendrils);
    auto    settings = fx.settings();
    settings.palette = {0xff112233u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::ShadowTendrils, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.palette.size() == 8);
    for (uint32_t c : owned.palette) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ShadowTendrils, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.palette.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShadowTendrils, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.palette = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShadowTendrils, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.shadow.shadowTendrilsLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::ShadowTendrils);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.4, 4.8, .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
        const auto            l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> composed(c.size()), lights(l.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, composed, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(composed == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.shadowTendrilsPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ShadowTendrils), reference(PixelFxKind::ShadowTendrils);
        double  elapsed = 0;
        for (double target : {.13, .359, .36, .4, .6, .8, .809, .9, 1., 1.2, 1.209, 1.5, 1.9, 2.05, 2.2, 3., 4., 5.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(target);
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](uint32_t c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](uint32_t c) { return c == 0; }));
    }
}

TEST_CASE("stylize.pixelfx.shadow.shadowTendrilsUndefinedSourceCollarHasDefinedNativeRaster") {
    // Pinned JS emits line(NaN,2.5,NaN,2.5) and its line walker never returns.
    // These hashes come from pristine kernel commands with ONLY that invalid line
    // recorded/omitted by the diagnostic caller. They are not pristine-frame parity.
    PixelFx fx(PixelFxKind::ShadowTendrils);
    auto    seek = fx.prepareSeek(59. / 120);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(fingerprint(fx.color()) == 898230139u);
    REQUIRE(fingerprint(fx.light()) == 871320989u);
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
    const auto l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.draw();
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
}


TEST_CASE("stylize.pixelfx.shadow.soulDrainPinnedSoulTravelLandingRingsAndLoopBoundaries") {
    // Independent pinned JS raw planes, not native-generated expected values.
    constexpr double   times[]       = {0,
                                        0.008333333333333333,
                                        0.11666666666666667,
                                        0.125,
                                        0.15833333333333333,
                                        0.16666666666666666,
                                        0.175,
                                        0.4583333333333333,
                                        0.4666666666666667,
                                        0.8833333333333333,
                                        0.8916666666666667,
                                        0.9,
                                        1.175,
                                        1.1833333333333333,
                                        1.1916666666666667,
                                        1.5666666666666667,
                                        1.575,
                                        1.5833333333333333,
                                        1.8666666666666667,
                                        1.875,
                                        1.9083333333333334,
                                        1.9166666666666667,
                                        2.625,
                                        2.6333333333333333,
                                        2.6416666666666666,
                                        2.65,
                                        2.925,
                                        2.933333333333333,
                                        3.191666666666667,
                                        3.2,
                                        3.2083333333333335,
                                        6.391666666666667,
                                        6.4,
                                        6.408333333333333,
                                        9.6};
    constexpr uint32_t expected[][2] = {
        {1255558277u, 3395572313u}, {2804304545u, 3721955427u}, {560747696u, 1847823439u},  {2395280092u, 2931914258u},
        {351865516u, 232823119u},   {463908170u, 3821316888u},  {742537381u, 920169544u},   {1562068086u, 429989793u},
        {910523831u, 1010466535u},  {3292203062u, 3427268357u}, {1133934281u, 1934770592u}, {3601548970u, 916980610u},
        {1327863895u, 2259880909u}, {3657665903u, 4190187322u}, {3479123630u, 2063597125u}, {3866030293u, 3132317907u},
        {491694386u, 2946032041u},  {4095571046u, 1185264971u}, {1567593323u, 3261612900u}, {423818971u, 2422749107u},
        {1968386536u, 2525343643u}, {91588333u, 3810623358u},   {843254583u, 4249291264u},  {2749428959u, 479704805u},
        {3344333371u, 1264084132u}, {1338105883u, 1550488804u}, {3999470362u, 3790225565u}, {3842489860u, 204705976u},
        {3491412221u, 734483206u},  {1255558277u, 3395572313u}, {2804304545u, 3721955427u}, {3491412221u, 734483206u},
        {1255558277u, 3395572313u}, {2804304545u, 3721955427u}, {506838129u, 3395572313u}};
    PixelFx fx(PixelFxKind::SoulDrain);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < std::size(times); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i][0]);
        REQUIRE(fingerprint(fx.light()) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.soulDrainOwningSoulRampAndAtomicBounds") {
    PixelFx    fx(PixelFxKind::SoulDrain);
    auto       settings = fx.settings();
    const auto defaults = settings.shadow.soul;
    REQUIRE(defaults.size() == 4);
    settings.shadow.soul = {0xff112233u};
    auto prepared        = PixelFx::prepareConfiguration(PixelFxKind::SoulDrain, settings);
    REQUIRE(prepared);
    settings.shadow.soul[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.shadow.soul.size() == 4);
    for (uint32_t c : owned.shadow.soul) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::SoulDrain, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.shadow.soul.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SoulDrain, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.shadow.soul = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SoulDrain, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.shadow.soulDrainLayerCompositionReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::SoulDrain);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.7, 3.2, 6.4, .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
        const auto            l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> composed(c.size()), lights(l.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, composed, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(composed == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.soulDrainAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SoulDrain), reference(PixelFxKind::SoulDrain);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            REQUIRE(std::abs(live.effectTime() - target) < 1e-12);
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
    PixelFx fx(PixelFxKind::SoulDrain);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::SoulDrain, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::SoulDrain, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SoulDrain, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}


TEST_CASE("stylize.pixelfx.shadow.voidOrbPinnedFloat32VortexLensingGulpsAndLoops") {
    // Independent pinned JS raw planes; expected values are never native-generated.
    constexpr double   times[]       = {0,
                                        0.008333333333333333,
                                        0.1,
                                        0.25,
                                        0.5,
                                        0.625,
                                        0.6333333333333333,
                                        0.6416666666666667,
                                        0.65,
                                        0.75,
                                        1,
                                        1.0333333333333334,
                                        1.0416666666666667,
                                        1.05,
                                        1.5,
                                        2,
                                        2.158333333333333,
                                        2.1666666666666665,
                                        2.175,
                                        2.183333333333333,
                                        2.191666666666667,
                                        2.4,
                                        2.566666666666667,
                                        2.575,
                                        2.5833333333333335,
                                        2.591666666666667,
                                        3,
                                        3.191666666666667,
                                        3.2,
                                        3.2083333333333335,
                                        6.391666666666667,
                                        6.4,
                                        6.408333333333333,
                                        9.6};
    constexpr uint32_t expected[][2] = {
        {1983595536u, 1083206901u}, {2053641296u, 2328228282u}, {880968148u, 213745637u},   {403191598u, 4022899682u},
        {2887846369u, 4158710161u}, {4113507486u, 1425968167u}, {2131858398u, 3779485648u}, {572157451u, 450981211u},
        {1514669750u, 660851135u},  {3087247565u, 3607232328u}, {4053986027u, 1902620568u}, {274346367u, 3000679086u},
        {2365492022u, 2940682327u}, {3029861021u, 1308973891u}, {409027014u, 718780478u},   {1830754938u, 2158135003u},
        {1605479156u, 2570444686u}, {3375548442u, 2236482402u}, {1690752872u, 3844914238u}, {3366980430u, 2856108838u},
        {3495435169u, 998688068u},  {2901189523u, 298882891u},  {327377445u, 2904419364u},  {3744814930u, 2230874351u},
        {3065380147u, 3217078890u}, {1516486166u, 1998821067u}, {13731840u, 2487833312u},   {654573758u, 1533944805u},
        {1983595536u, 1083206901u}, {2053641296u, 2328228282u}, {654573758u, 1533944805u},  {1983595536u, 1083206901u},
        {2053641296u, 2328228282u}, {1983595536u, 1083206901u}};
    PixelFx fx(PixelFxKind::VoidOrb);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < std::size(times); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i][0]);
        REQUIRE(fingerprint(fx.light()) == expected[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.shadow.voidOrbOwningPaletteAndAtomicBounds") {
    PixelFx fx(PixelFxKind::VoidOrb);
    auto    settings = fx.settings();
    settings.palette = {0xff112233u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::VoidOrb, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.palette.size() == 8);
    for (uint32_t c : owned.palette) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::VoidOrb, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.palette.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::VoidOrb, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.palette = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::VoidOrb, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.shadow.voidOrbLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::VoidOrb);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 3.2, 6.4, .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end());
        const auto            l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> composed(c.size()), lights(l.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, composed, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(composed == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.shadow.voidOrbAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::VoidOrb), reference(PixelFxKind::VoidOrb);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            REQUIRE(std::abs(live.effectTime() - target) < 1e-12);
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
    PixelFx fx(PixelFxKind::VoidOrb);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::VoidOrb, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::VoidOrb, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::VoidOrb, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}

TEST_CASE("stylize.pixelfx.shadow.voidOrbOpaqueCoreClearsIncomingLightAndVeilRetainsAlpha") {
    PixelFx fx(PixelFxKind::VoidOrb);
    auto    seek = fx.prepareSeek(77. / 120);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) {
        const auto a = c >> 24;
        return a > 0 && a < 255;
    }));
    for (bool transformed : {false, true}) {
        const PixelFxInfo     info{PixelFxKind::VoidOrb, "shadow/void-core-fixture", 96, 96, 48, 96, 3.2, true};
        std::vector<uint32_t> color(96 * 96), light(color.size(), 0xff123456u);
        PixelFxCanvas         canvas{96, 96, 48, 96, color, light};
        PixelFxPlacement      placement;
        Paint                 p{info, color, light, 1};
        if (transformed) {
            placement = {7, -3, .2, true};
            p.place(7, -3, true, .2);
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Both, placement));
        size_t cleared = 0;
        // Safely inside the source's radius-11.4 extinction disc, including rotated pixel rounding.
        for (int j = -9; j < 9; ++j)
            for (int i = -9; i < 9; ++i) {
                const double dx = i + .5, dy = j + .5;
                if (dx * dx + dy * dy >= 81) continue;
                const int x = p.cellX(dx, -62 + dy), y = p.cellY(dx, -62 + dy);
                if (!p.contains(x, y)) continue;
                REQUIRE(light[p.index(x, y)] == 0);
                REQUIRE((color[p.index(x, y)] >> 24) == 255u);
                ++cleared;
            }
        REQUIRE(cleared > 0);
    }
    REQUIRE(fx.effectTime() == 77. / 120);
}

}  // namespace test_pixelfx_shadow_unit
