// Independent pinned arcane/_lib.js and rune-circle.js fixtures.
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Arcane.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_arcane_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
uint32_t fingerprint(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto b : std::as_bytes(pixels)) h = (h ^ uint8_t(b)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.arcane.sourceGlyphRingAndMoteContracts") {
    constexpr uint32_t expected[][2] = {
        {2467605147u, 2218237381u}, {3117524235u, 2218237381u}, {3599539178u, 2218237381u}, {2038223216u, 3989168685u}};
    const PixelFxInfo fxInfo{PixelFxKind::RuneCircle, "arcane/helper-fixture", 256, 112, 128, 80, 4, true};
    for (int n = 0; n < 4; ++n) {
        std::vector<uint32_t> colors(256 * 112), lights(colors.size());
        Paint                 p{fxInfo, colors, lights, 1};
        if (n == 1) p.place(3.25, -1.5, true, .2);
        if (n < 2)
            for (int i = -9; i <= 9; ++i)
                arcane::glyph(p, i, (i + 9) * 11 - 100, -8, hex(0xdcc8ff), n == 0 ? 1 : .48, hex(0x481c88));
        if (n == 2)
            for (int i = 0; i < 5; ++i) {
                constexpr double radii[] = {.4, .5, 2.5, 13, 23};
                arcane::floorRing(
                    p, -85 + i * 40, -8, radii[i], i == 3 ? 0 : .3,
                    [](double g) { return g < 0 ? hex(0x481c88) : hex(0xdcc8ff); },
                    [](double g) { return .2 + .8 * std::abs(std::sin(g)); },
                    [](double g) { return std::cos(g) > .1; });
            }
        if (n == 3)
            for (int i = 0; i < 4; ++i) {
                constexpr double vx[] = {-420, 0, 12, 220}, vy[] = {220, 0, -30, -120}, k[] = {0, .17, .35, 1.3};
                arcane::mote(p, -75 + i * 45, -20, vx[i], vy[i], hex(0xc684ff), 1.1, k[i], .5);
            }
        REQUIRE(fingerprint(colors) == expected[n][0]);
        REQUIRE(fingerprint(lights) == expected[n][1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.runeCirclePinnedFloorMotesAndTwoLoopSeams") {
    PixelFx fx(PixelFxKind::RuneCircle);
    REQUIRE(!fx.isSimulated());
    constexpr int      ticks[]    = {0,   1,   12,  36,  60,  90,  119, 120, 121, 180, 239, 240, 241,
                                     300, 359, 360, 361, 420, 479, 480, 481, 600, 720, 840, 959, 960};
    constexpr uint32_t expected[] = {
        793877027u,  1530551957u, 3478352302u, 1023396478u, 1372347933u, 2970215136u, 2757249368u, 960852583u,
        339475936u,  1400325572u, 2225223212u, 2209397618u, 3591372139u, 717465899u,  407681416u,  3219249088u,
        1114525118u, 2985793230u, 1105269735u, 386755817u,  179151434u,  1493905307u, 3565524570u, 255406587u,
        1518482683u, 4007427915u, 996202945u,  2557647356u, 3761216929u, 1809383575u, 1845903398u, 1587512493u,
        3868666362u, 3867106319u, 1035026360u, 4225530776u, 1256939279u, 2648637682u, 793877027u,  1530551957u,
        3478352302u, 1023396478u, 407681416u,  3219249088u, 3565524570u, 255406587u,  1845903398u, 1587512493u,
        1256939279u, 2648637682u, 793877027u,  1530551957u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i * 2]);
        REQUIRE(fingerprint(fx.light()) == expected[i * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.runeCirclePartitionedAnalyticPlaybackAndConfiguration") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::RuneCircle), reference(PixelFxKind::RuneCircle);
        double  elapsed = 0;
        for (double target : {.125, .37, 1.001, 2.1, 3.99, 4., 4.001, 8., 8.123}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            // Analytic pixels are exact at the injected clock, not a distinct idealized seam.
            REQUIRE(std::abs(live.effectTime() - target) < 1e-12);
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.actorVisibility() == PixelFxActorVisibility::Visible);
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
    PixelFx fx(PixelFxKind::RuneCircle);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::RuneCircle, settings);
    REQUIRE(candidate);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::RuneCircle, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.5);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const double before = fx.effectTime();
    const auto   c = fingerprint(fx.color()), l = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RuneCircle, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == before);
    REQUIRE(fingerprint(fx.color()) == c);
    REQUIRE(fingerprint(fx.light()) == l);
}
TEST_CASE("stylize.pixelfx.arcane.starFallPinnedTrailsLandingRingsAndEnd") {
    PixelFx fx(PixelFxKind::StarFall);
    REQUIRE(fx.isSimulated());
    // Independent pinned upstream raw planes, not native-generated snapshots.
    constexpr int      ticks[]    = {0,   1,   4,   8,   12,  16,  24,  36,  48,  60,  72,  84,  96,
                                     108, 120, 132, 144, 156, 168, 180, 192, 216, 240, 264, 288, 300};
    constexpr uint32_t expected[] = {
        1954988997u, 1954988997u, 1954988997u, 1954988997u, 1954988997u, 3121168877u, 1090147721u, 2815895619u,
        3287189477u, 3953605578u, 1135824139u, 1654542710u, 3710100381u, 4139977007u, 3017552162u, 1006715908u,
        2256601021u, 1369456577u, 3804118593u, 3703730570u, 1366251586u, 1248282162u, 2810463552u, 98226441u,
        3510674948u, 2507523670u, 1128550178u, 681714112u,  190409129u,  1010661444u, 877422652u,  1095639752u,
        1577594845u, 2475511596u, 3208621030u, 381958985u,  1767283434u, 1744775645u, 3522573175u, 3461332535u,
        3538605612u, 3773695701u, 3745636137u, 892870088u,  3270772711u, 1140733413u, 1954988997u, 3182407637u,
        1954988997u, 1954988997u, 1954988997u, 1954988997u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i * 2]);
        REQUIRE(fingerprint(fx.light()) == expected[i * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.starFallPartitionedRngReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::StarFall), reference(PixelFxKind::StarFall);
        double  elapsed = 0;
        for (double target : {.04, .17, .35, .61, .95, 1.2, 1.5, 1.8, 2.1, 2.5, 3.}) {
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
TEST_CASE("stylize.pixelfx.arcane.starFallConfigurationOwnershipAndReplayInvalidation") {
    PixelFx fx(PixelFxKind::StarFall);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::StarFall, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.9);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    auto value = fx.configurationValue();
    auto rt    = PixelFx::prepareConfigurationValue(PixelFxKind::StarFall, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.9);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::StarFall, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(!fx.done());
    REQUIRE(fx.activeTickCount() == 0);
}
TEST_CASE("stylize.pixelfx.arcane.sourceSigilsNegativeIndicesEdgesAndPlacement") {
    const PixelFxInfo  fxInfo{PixelFxKind::ManaBurst, "arcane/sigil-fixture", 256, 112, 128, 80, 1.8, false};
    constexpr uint32_t expected[][2] = {{840011942u, 2218237381u}, {206118075u, 2218237381u}};
    for (int n = 0; n < 2; ++n) {
        std::vector<uint32_t> colors(256 * 112), lights(colors.size());
        Paint                 p{fxInfo, colors, lights, 1};
        if (n == 1) p.place(3.25, -1.5, true, .2);
        for (int i = -9; i <= 9; ++i)
            arcane::sigil(p, i, (i + 9) * 11 - 100, -8, hex(0xdcc8ff), n == 0 ? 1 : .48, hex(0x481c88));
        REQUIRE(fingerprint(colors) == expected[n][0]);
        REQUIRE(fingerprint(lights) == expected[n][1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.manaBurstPinnedGatherReleaseRunesPopsAndTail") {
    PixelFx fx(PixelFxKind::ManaBurst);
    REQUIRE(fx.isSimulated());
    // Independent pinned source bytes, mapped through actual reference sample times.
    constexpr int      ticks[]    = {0,  1,  8,   12,  20,  22,  23,  26,  27,  28,  35,  36,  49,  50,  78,  84,
                                     96, 99, 108, 120, 132, 144, 156, 168, 180, 204, 215, 216, 217, 240, 300, 360};
    constexpr uint32_t expected[] = {
        2253016479u, 1889377413u, 1118637726u, 199643018u,  1911050054u, 2366791349u, 540813235u,  4220935590u,
        3079615106u, 4122382576u, 2864369870u, 2190011322u, 1642835643u, 3287650520u, 1835178023u, 2133369203u,
        1410265600u, 2863314879u, 4100688041u, 729608286u,  3764836851u, 674287376u,  2649473432u, 2545814445u,
        2474742860u, 2415512300u, 171656879u,  3314603024u, 2100481330u, 2114767917u, 2725624502u, 2541112596u,
        2104011531u, 2243405224u, 3659020962u, 3033009682u, 3125371477u, 2319315893u, 3668588320u, 1021813433u,
        2293417419u, 57664627u,   3042549299u, 2242729216u, 677923151u,  2526482883u, 1567742438u, 1428034927u,
        3186407971u, 1600689312u, 788829063u,  3807109579u, 1245914053u, 1245914053u, 1245914053u, 1245914053u,
        1245914053u, 1245914053u, 1245914053u, 1245914053u, 1245914053u, 1245914053u, 1245914053u, 1245914053u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == expected[i * 2]);
        REQUIRE(fingerprint(fx.light()) == expected[i * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.manaBurstPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ManaBurst), reference(PixelFxKind::ManaBurst);
        double  elapsed = 0;
        for (double target : {.19, .22, .225, .3, .42, .65, .8, 1., 1.2, 1.5, 1.8, 2.4, 3.}) {
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
TEST_CASE("stylize.pixelfx.arcane.manaBurstOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::ManaBurst);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::ManaBurst, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ManaBurst, value);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ManaBurst, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.sourceStreakForkPathsAndThreePassInk") {
    const PixelFxInfo  fxInfo{PixelFxKind::SpellCharge, "arcane/arc-fixture", 160, 112, 80, 80, 1.6, true};
    constexpr uint32_t sums[][2]  = {{1542559273u, 1731646637u}, {3117053177u, 3636341158u}, {3597563751u, 2859551549u},
                                     {664549871u, 4211124443u},  {4266216305u, 3759969050u}, {2584534433u, 2292016240u},
                                     {2821263814u, 493635807u},  {3294757767u, 3495804075u}};
    constexpr double   next[]     = {0.8826168975792825, 0.9369377219118178, 0.14042092696763575,
                                     0.9165676208212972, 0.9195536528714001, 0.2645264274906367};
    constexpr size_t   sizes[][5] = {{1, 3}, {3, 33, 9, 9}, {2, 33, 9}, {4, 33, 9, 9, 9}, {2, 33, 9}, {4, 33, 9, 9, 9}};
    for (int n = 0; n < 8; ++n) {
        std::vector<uint32_t> colors(160 * 112), lights(colors.size());
        Paint                 p{fxInfo, colors, lights, 1};
        if (n == 1) p.place(3.25, -1.5, true, .2);
        if (n < 2)
            for (int i = 0; i < 4; ++i) {
                constexpr double vx[] = {-420, 0, 12, 220}, vy[] = {220, 0, -30, -120}, a[] = {0, .17, .6, 1};
                arcane::streak(p, -55 + i * 35, -18, vx[i], vy[i], hex(0xc684ff), a[i], 7, .4);
            }
        else {
            Rng        r(uint32_t(123 + n));
            const auto paths = arcane::arc(r, -25, -30, n == 2 ? -25 : 28, n == 2 ? -30 : -8, .35, 3, .7);
            REQUIRE(r.next() == next[n - 2]);
            REQUIRE(paths.size() == sizes[n - 2][0]);
            for (size_t j = 0; j < paths.size(); ++j) {
                REQUIRE(paths[j].points.size() == sizes[n - 2][j + 1]);
                REQUIRE(paths[j].width == (j == 0 ? 1 : .55));
            }
            constexpr double k[] = {0, 0, .2, .25, .3, .45, .6, 1};
            arcane::drawArc(p, paths, {WHITE, hex(0xecc8ff), hex(0x481c88), hex(0xc684ff)}, k[n], .4);
        }
        REQUIRE(fingerprint(colors) == sums[n][0]);
        REQUIRE(fingerprint(lights) == sums[n][1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.spellChargePinnedSwirlRingCrackleAndLoopSeams") {
    PixelFx fx(PixelFxKind::SpellCharge);
    REQUIRE(!fx.isSimulated());
    constexpr int      ticks[] = {0,   1,   8,   11,  12,  13,  24,  36,  59,  60,  61,  72,  83,  84, 85,
                                  108, 120, 132, 144, 156, 168, 180, 191, 192, 193, 240, 288, 383, 384};
    constexpr uint32_t sums[]  = {
        651609791u,  1234322172u, 4036064623u, 2524382896u, 1800250479u, 1118000834u, 957801111u,  3810655808u,
        2288453685u, 3313327852u, 1171451116u, 3598026611u, 3931790548u, 97792046u,   1365454518u, 1046256356u,
        4292336950u, 2898435431u, 3952917421u, 440417392u,  913281686u,  689868703u,  630875602u,  755912341u,
        728260051u,  3036552526u, 2756754708u, 179971450u,  850037819u,  1743169992u, 302021147u,  391477328u,
        1425569651u, 1790559659u, 2414314554u, 1461244459u, 2451305044u, 3769254320u, 748612600u,  3411443838u,
        3562236487u, 1490017346u, 980793535u,  3278848995u, 2668080210u, 1509481380u, 651609791u,  1234322172u,
        4036064623u, 2524382896u, 3744554194u, 3907396641u, 13567919u,   2989371242u, 2668080210u, 1509481380u,
        651609791u,  1234322172u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == sums[i * 2]);
        REQUIRE(fingerprint(fx.light()) == sums[i * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.spellChargeAnalyticClockAndDrawStayPure") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SpellCharge), reference(PixelFxKind::SpellCharge);
        double  elapsed = 0;
        for (double target : {.1, .3, .5, .7, .9, 1.3, 1.6, 1.601, 3.2, 3.333}) {
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
            REQUIRE(live.actorVisibility() == PixelFxActorVisibility::Visible);
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.spellChargeOptionalActorOwnershipAndAtomicFailure") {
    PixelFx fx(PixelFxKind::SpellCharge);
    auto    settings      = fx.settings();
    settings.seed         = 123;
    settings.actor.source = PixelFxActorSource::None;
    auto absent           = PixelFx::prepareConfiguration(PixelFxKind::SpellCharge, settings);
    REQUIRE(absent);
    REQUIRE(fx.commitConfiguration(std::move(absent).takeValue()));
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::all_of(body.begin(), body.end(), [](uint32_t c) { return c == 0; }));
    settings              = PixelFx::defaults(PixelFxKind::SpellCharge);
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 1;
    settings.actor.height = 1;
    settings.actor.mask   = {1};
    settings.actor.pixels = {0xffa0b0c0u};
    settings.actor.eyes.clear();
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::SpellCharge, settings);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SpellCharge, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::SpellCharge, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
}
TEST_CASE("stylize.pixelfx.arcane.arcaneOrbPinnedGlassSwirlRingsAndTwoLoops") {
    PixelFx fx(PixelFxKind::ArcaneOrb);
    REQUIRE(!fx.isSimulated());
    // Independent pinned upstream planes at actual time-mapped samples.
    constexpr int      ticks[] = {0,   1,   24,  36,  60,  72,  96,  108, 120, 143, 144, 145,
                                  180, 216, 240, 264, 287, 288, 289, 360, 432, 575, 576};
    constexpr uint32_t sums[]  = {
        3974828205u, 3828239712u, 579478114u,  4020477962u, 990251362u,  2213626313u, 1210594614u, 852065586u,
        2739231702u, 3589743327u, 2040140645u, 3297831975u, 4196181377u, 418999702u,  336864891u,  530049689u,
        1924500306u, 481925175u,  1450800319u, 70619621u,   1264360487u, 1194640008u, 154166192u,  1366020223u,
        830740919u,  1617656125u, 4052554794u, 1933189423u, 550779586u,  3382670479u, 4229429472u, 1861490435u,
        2353119059u, 3728507298u, 3974828205u, 3828239712u, 579478114u,  4020477962u, 2040140645u, 2956975381u,
        615455449u,  690355641u,  2353119059u, 3728507298u, 3974828205u, 3828239712u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fingerprint(fx.color()) == sums[i * 2]);
        REQUIRE(fingerprint(fx.light()) == sums[i * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.arcaneOrbTransparentOrderedLayerComposition") {
    PixelFx    fx(PixelFxKind::ArcaneOrb);
    const auto fxInfo = fx.info();
    for (double time : {0., .3, .6, 1.2, 2.399999, 2.4, 4.8}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(std::any_of(fx.color().begin(), fx.color().end(),
                            [](uint32_t c) { return (c >> 24) > 0 && (c >> 24) < 255; }));
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
    }
}
TEST_CASE("stylize.pixelfx.arcane.arcaneOrbAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ArcaneOrb), reference(PixelFxKind::ArcaneOrb);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 2.399, 2.4, 2.401, 4.8, 4.913}) {
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
    PixelFx fx(PixelFxKind::ArcaneOrb);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::ArcaneOrb, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::ArcaneOrb, config);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ArcaneOrb, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.arcaneBeamAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ArcaneBeam), reference(PixelFxKind::ArcaneBeam);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 2.399, 2.4, 2.401, 4.8, 4.913}) {
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
    PixelFx fx(PixelFxKind::ArcaneBeam);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::ArcaneBeam, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    auto        actorConfig  = fx.configurationValue();
    const auto* actorOptions = actorConfig.find("options");
    REQUIRE(actorOptions);
    REQUIRE(actorOptions->find("actor"));
    settings              = fx.settings();
    settings.actor.source = PixelFxActorSource::Hero;
    auto actorPrepared    = PixelFx::prepareConfiguration(PixelFxKind::ArcaneBeam, settings);
    REQUIRE(actorPrepared);
    REQUIRE(fx.commitConfiguration(std::move(actorPrepared).takeValue()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::ArcaneBeam, config);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ArcaneBeam, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.arcaneBeamPinnedBandsFlareAndVapour") {
    PixelFx fx(PixelFxKind::ArcaneBeam);
    REQUIRE(!fx.isSimulated());
    // Pinned upstream fixtures: alpha/light exact, sampled RGB <=1 only for measured libm blend truncation.
    constexpr int      ticks[]     = {0,  1,  11, 24, 33,  36,  39,  48,  60,  64,  72,  84,
                                      85, 88, 96, 97, 120, 143, 144, 145, 168, 216, 287, 288};
    constexpr int      points[][2] = {{41, 60},  {70, 60},  {110, 59}, {160, 61}, {200, 60}, {209, 60},
                                      {215, 60}, {200, 35}, {201, 38}, {197, 39}, {202, 39}, {199, 43},
                                      {200, 45}, {205, 45}, {203, 54}, {207, 43}, {209, 84}, {150, 84}};
    constexpr uint32_t alphaSums[] = {471898119u,  3159158596u, 2748975620u, 1821286820u, 1224275231u, 339746488u,
                                      3904460778u, 867631192u,  2167545132u, 1452769769u, 3304629489u, 3370375025u,
                                      339019704u,  3703085599u, 2556449774u, 2012797154u, 3333401048u, 2369802054u,
                                      471898119u,  3159158596u, 1821286820u, 1155556067u, 2369802054u, 471898119u};
    constexpr uint32_t lightSums[] = {1106868194u, 3367772465u, 2173131058u, 2086540789u, 2458882393u, 766576340u,
                                      2277687301u, 2121696094u, 2332376157u, 1648210539u, 947188522u,  4107988359u,
                                      1497808570u, 2664741387u, 3858940770u, 1866443641u, 3168992081u, 735120006u,
                                      1106868194u, 3367772465u, 2086540789u, 947188522u,  735120006u,  1106868194u};
    constexpr uint32_t pixels[]    = {
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4294967295u, 4294935750u, 334252186u,
        871123098u,  166480026u,  1357662362u, 1290553498u, 1256999066u, 1709983898u, 4294935750u, 1407994010u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4294967295u,
        4293675162u, 334252186u,  938231962u,  166480026u,  1407994010u, 1290553498u, 1223444634u, 1659652250u,
        4294967295u, 1374439578u, 0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u,
        4294935750u, 4294967295u, 0u,          166414490u,  4293675162u, 518736026u,  4293675162u, 1105938586u,
        636242074u,  1458325658u, 4293675162u, 4294603962u, 0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294967295u, 4294967295u, 0u,          736905370u,  1223444634u, 736905370u,
        1307330714u, 468469914u,  49039514u,   1391216794u, 4293675162u, 1122781338u, 0u,          0u,
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294967295u, 4294967295u, 0u,          653019290u,
        804014234u,  518801562u,  736905370u,  99371162u,   351029402u,  1173112986u, 4293674906u, 4293675162u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4293675162u,
        0u,          535513242u,  585910426u,  418138266u,  619464858u,  4293675162u, 384583834u,  4294538426u,
        4294803394u, 955009178u,  0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u,
        4294967295u, 4294967295u, 4293675162u, 434915482u,  401361050u,  4293675162u, 585910426u,  183257242u,
        401361050u,  4294604476u, 4294831605u, 871123098u,  0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294935750u, 4294967295u, 0u,          200034458u,  485247130u,  116148378u,
        653019290u,  317474970u,  317474970u,  653019290u,  4293675162u, 4293609626u, 0u,          0u,
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4293675162u, 0u,          4293675162u,
        418138266u,  65816730u,   502024346u,  200034458u,  116148378u,  434915482u,  4287372876u, 4294803651u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4293675162u,
        4293675162u, 183257242u,  351029402u,  99371162u,   384583834u,  149702810u,  49039514u,   720128154u,
        4290784363u, 166480026u,  0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u,
        4294967295u, 4293675162u, 0u,          99371162u,   116148378u,  49039514u,   132925594u,  32262298u,
        132925594u,  4294617047u, 4294887146u, 720128154u,  0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294935750u, 4293675162u, 0u,          0u,          0u,          0u,
        0u,          116148378u,  921454746u,  4294604219u, 4293675162u, 1038895258u, 0u,          0u,
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294935750u, 4293675162u, 4293675162u, 0u,
        0u,          0u,          0u,          200034458u,  971786394u,  4294604219u, 4293674905u, 1022118042u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294967295u, 4293675162u,
        4293675162u, 0u,          0u,          0u,          116148378u,  434915482u,  1055672474u, 1089226906u,
        4293675162u, 971786394u,  0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u,
        4294935750u, 4294967295u, 0u,          0u,          183257242u,  0u,          552355994u,  837568666u,
        1206667418u, 1189890202u, 4294803651u, 4293675162u, 0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294935750u, 4293675162u, 4293675162u, 0u,          233588890u,  0u,
        585910426u,  871123098u,  1256999066u, 1156335770u, 4294803651u, 4293675162u, 0u,          0u,
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294967295u, 4294967295u, 0u,          132925594u,
        418138266u,  434915482u,  854345882u,  1055672474u, 1307330714u, 1475102874u, 4294967295u, 418138266u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294935750u, 4294967295u,
        4294935750u, 334252186u,  770459802u,  183257242u,  1324107930u, 1290553498u, 1290553498u, 1777092762u,
        4294967295u, 1407994010u, 0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u,
        4294935750u, 4294967295u, 4294935750u, 334252186u,  871123098u,  166480026u,  1357662362u, 1290553498u,
        1256999066u, 1709983898u, 4294935750u, 1407994010u, 0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294935750u, 4294967295u, 4293675162u, 334252186u,  938231962u,  166480026u,
        1407994010u, 1290553498u, 1223444634u, 1659652250u, 4294967295u, 1374439578u, 0u,          0u,
        4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294967295u, 4294967295u, 0u,          736905370u,
        1223444634u, 736905370u,  1307330714u, 468469914u,  49039514u,   1391216794u, 4293675162u, 1122781338u,
        0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294953196u, 4294967295u, 4293675162u,
        0u,          99371162u,   116148378u,  49039514u,   132925594u,  32262298u,   132925594u,  4294617047u,
        4294887146u, 720128154u,  0u,          0u,          4294967295u, 4294967295u, 4294967295u, 4294967295u,
        4294935750u, 4294967295u, 4294935750u, 334252186u,  770459802u,  183257242u,  1324107930u, 1290553498u,
        1290553498u, 1777092762u, 4294967295u, 1407994010u, 0u,          0u,          4294967295u, 4294967295u,
        4294967295u, 4294953196u, 4294935750u, 4294967295u, 4294935750u, 334252186u,  871123098u,  166480026u,
        1357662362u, 1290553498u, 1256999066u, 1709983898u, 4294935750u, 1407994010u, 0u,          0u};
    for (size_t i = 0; i < std::size(ticks); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        uint32_t a = 2166136261u;
        for (auto c : fx.color()) a = (a ^ uint8_t(c >> 24)) * 16777619u;
        REQUIRE(a == alphaSums[i]);
        REQUIRE(fingerprint(fx.light()) == lightSums[i]);
        for (size_t j = 0; j < std::size(points); ++j) {
            const auto actual   = fx.color()[size_t(points[j][1]) * 264 + size_t(points[j][0])],
                       expected = pixels[i * std::size(points) + j];
            REQUIRE((actual >> 24) == (expected >> 24));
            for (int shift : {0, 8, 16})
                REQUIRE(std::abs(int((actual >> shift) & 255) - int((expected >> shift) & 255)) <= 1);
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.arcaneBeamFrontOnlyAndDitherCellContract") {
    constexpr int values[] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = -8; y < 8; ++y)
        for (int x = -8; x < 8; ++x) {
            const double expected = (values[((y & 3) << 2) | (x & 3)] + .5) / 16;
            REQUIRE(arcane::bayer(x + .1, y + .9) == expected);
            REQUIRE(arcane::bayer(x, y) == expected);
        }
    PixelFx    fx(PixelFxKind::ArcaneBeam);
    const auto fxInfo = fx.info();
    for (double t : {0., .09166666666666666, .3, .6, 1.199999, 1.2, 2.4}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(std::all_of(c.begin(), c.end(), [](auto v) { return v == 0; }));
        REQUIRE(std::all_of(l.begin(), l.end(), [](auto v) { return v == 0; }));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
    }
}
TEST_CASE("stylize.pixelfx.arcane.arcaneMissilesPinnedFiveLaunchesHitsAndTail") {
    constexpr int      ticks[]    = {0,   5,   6,   7,   18,  19,  30,  31,  42,  43,  72,  93,  94,  95,  102, 103,
                                     104, 114, 115, 116, 131, 132, 133, 145, 146, 147, 168, 209, 210, 211, 240};
    constexpr uint32_t sums[][62] = {
        {3231812033u, 630379381u,  1060514949u, 691681845u,  469480549u,  2522365597u, 3635038069u, 3667376573u,
         831627492u,  1259434248u, 233756775u,  1538326517u, 1128655288u, 3808680963u, 895849028u,  801464679u,
         3053530294u, 2076938076u, 2341807234u, 4232030821u, 2747180164u, 2847206181u, 2508423438u, 420240947u,
         1675538709u, 2977966996u, 3396602592u, 2450825095u, 2036434162u, 3432000591u, 523021521u,  162894037u,
         1299023345u, 2014253788u, 640533788u,  2231583377u, 3594977675u, 1410336937u, 3569494645u, 2318769766u,
         3487137325u, 1430229444u, 929880927u,  402812028u,  674911656u,  3525148106u, 3335319072u, 2359779961u,
         368728637u,  2827279661u, 4164967786u, 1031717505u, 1070728003u, 3472537189u, 937687493u,  937687493u,
         937687493u,  937687493u,  937687493u,  937687493u,  937687493u,  937687493u},
        {1126744353u, 2020084013u, 585430692u,  2456975253u, 2584201761u, 4116111069u, 2200946030u, 1544472861u,
         2603288454u, 35408303u,   703714720u,  1083832147u, 3490761730u, 3121435485u, 3403257683u, 3468841771u,
         3271703271u, 3678637514u, 3942676113u, 3287635172u, 3649043182u, 881146664u,  2758412401u, 4003918708u,
         4135175131u, 131322284u,  3587609320u, 159029990u,  2120267409u, 306078296u,  465819818u,  3532968131u,
         4111696721u, 2149803215u, 2985689449u, 1842157266u, 708843022u,  3930196738u, 2274153074u, 2853906975u,
         3153389158u, 594219597u,  1659691777u, 3983176982u, 1538613478u, 1221108900u, 2174191223u, 3601583398u,
         3195787088u, 2441994116u, 2361770271u, 1912544218u, 1302368080u, 732984843u,  937687493u,  937687493u,
         937687493u,  937687493u,  937687493u,  937687493u,  937687493u,  937687493u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::ArcaneMissiles);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto c           = PixelFx::prepareConfiguration(PixelFxKind::ArcaneMissiles, settings);
            REQUIRE(c);
            REQUIRE(fx.commitConfiguration(std::move(c).takeValue()));
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
TEST_CASE("stylize.pixelfx.arcane.arcaneMissilesPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ArcaneMissiles), reference(PixelFxKind::ArcaneMissiles);
        double  elapsed = 0;
        for (double target : {.049, .05, .059, .15, .35, .45, .78, .8, .86, .96, 1.1, 1.217, 1.75, 2.4, 3.}) {
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
TEST_CASE("stylize.pixelfx.arcane.arcaneMissilesOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::ArcaneMissiles);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::ArcaneMissiles, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto  value   = fx.configurationValue();
    const auto* options = value.find("options");
    REQUIRE(options);
    REQUIRE(options->find("actor"));
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::ArcaneMissiles, value);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ArcaneMissiles, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.arcaneMissilesOrderedLayersAndReverseSeek") {
    PixelFx    fx(PixelFxKind::ArcaneMissiles);
    const auto fxInfo     = fx.info();
    uint32_t   firstColor = 0, firstLight = 0;
    for (double t : {.8, 1.1, 1.217, 1.75, 3., .8}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(std::all_of(c.begin(), c.end(), [](auto v) { return v == 0; }));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .8) {
            if (firstColor) {
                REQUIRE(fingerprint(fx.color()) == firstColor);
                REQUIRE(fingerprint(fx.light()) == firstLight);
            } else {
                firstColor = fingerprint(fx.color());
                firstLight = fingerprint(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.sourceShadesAndSqueezedSilhouette") {
    constexpr uint8_t  mask[]     = {1, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0};
    constexpr uint8_t  expected[] = {1, 2, 2, 3, 0, 2, 0, 3, 0, 2, 2, 3, 0, 2, 2, 0, 1, 2, 2, 0};
    constexpr uint32_t sums[]     = {1596436749u, 3445853213u, 2452471237u};
    const PixelFxInfo  fxInfo{PixelFxKind::BlinkIn, "arcane/squeeze-fixture", 240, 96, 120, 80, 1.4, false};
    for (int kind = 0; kind < 3; ++kind) {
        PixelFxActor actor;
        actor.width  = 5;
        actor.height = 4;
        actor.x      = kind == 2 ? -1.5 : -2;
        actor.y      = -6;
        actor.mask.assign(std::begin(mask), std::end(mask));
        if (kind != 1)
            for (size_t i = 0; i < std::size(mask); ++i)
                actor.pixels.push_back(
                    std::array<uint32_t, 4>{hex(0x3d3d3d), hex(0x3e3e3e), hex(0x9e9e9e), hex(0x9f9f9f)}[i % 4]);
        const auto sh = arcane::shades(actor);
        for (size_t i = 0; i < sh.size(); ++i) REQUIRE(sh[i] == (kind == 1 ? (mask[i] ? 2 : 0) : expected[i]));
        std::vector<uint32_t> c(240 * 96), l(c.size());
        Paint                 painter{fxInfo, c, l, 1};
        constexpr double      scales[] = {0, .02, .021, .2, .5, 1, 1.6};
        for (size_t i = 0; i < std::size(scales); ++i)
            arcane::squeezed(painter, actor, sh, scales[i], WHITE, hex(0xc684ff), .55, -90 + double(i) * 30);
        REQUIRE(fingerprint(c) == sums[kind]);
        REQUIRE(fingerprint(l) == 2452471237u);
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkInPinnedSpiralLineSilhouetteArrivalAndTail") {
    constexpr int      ticks[]    = {0,  1,  12, 24, 26, 27, 30, 31, 32, 42, 43,  44,  55,  56,  57,  58, 59,
                                     60, 64, 65, 66, 72, 81, 82, 83, 84, 96, 108, 132, 167, 168, 169, 240};
    constexpr uint32_t sums[][66] = {
        {2413956549u, 2413956549u, 1378913110u, 1490324329u, 2963039718u, 2394539143u, 2288787903u, 1874300436u,
         1992886732u, 3199281622u, 154521917u,  3606047108u, 379484287u,  1700810613u, 1220909875u, 4036077703u,
         1949564623u, 938042627u,  738441924u,  2426754270u, 891609087u,  4163349387u, 3844129449u, 1606283819u,
         2074379019u, 1480348325u, 2444617413u, 3929816517u, 569639189u,  42671597u,   790500672u,  643468181u,
         3120119970u, 3473820314u, 2223005198u, 3797128111u, 2135448939u, 1713298490u, 1622296172u, 2659538835u,
         2286290558u, 1261104738u, 3719733496u, 2072705726u, 1735909057u, 3881503501u, 1502018663u, 3810210665u,
         1468256365u, 1781426722u, 4062026505u, 2680605035u, 4181550547u, 3173884454u, 3405109793u, 1806633651u,
         665651771u,  892547309u,  2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u,
         2413956549u, 2413956549u},
        {2413956549u, 2413956549u, 112035657u,  3345332709u, 3566247087u, 2062594314u, 2780054738u, 1356283958u,
         2494616649u, 1250107267u, 326833466u,  1702962508u, 81728358u,   2700662933u, 349724622u,  2482615647u,
         2295422713u, 816814652u,  848860420u,  3464605828u, 2443440688u, 791530658u,  2795931679u, 3983729443u,
         829441227u,  2057424057u, 2223168676u, 3667818209u, 1983514660u, 591894873u,  2290487642u, 143099554u,
         1077240717u, 2318353843u, 1394741255u, 450576444u,  1750381283u, 755548381u,  4146397264u, 3968118026u,
         2272281760u, 3672374262u, 4206315086u, 4151415879u, 2988564971u, 4173535381u, 3585206581u, 664692611u,
         2084893637u, 2784395216u, 3031452658u, 981896034u,  4005362936u, 3736144884u, 1773470388u, 683705649u,
         2782722720u, 3676106382u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u,
         2413956549u, 2413956549u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::BlinkIn);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::BlinkIn, settings);
            REQUIRE(candidate);
            REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        }
        for (size_t i = 0; i < std::size(ticks); ++i) {
            auto seek = fx.prepareSeek(ticks[i] / 120.);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(fingerprint(fx.color()) == sums[variant][i * 2]);
            REQUIRE(fingerprint(fx.light()) == sums[variant][i * 2 + 1]);
            REQUIRE(fx.actorVisibility() ==
                    (ticks[i] / 120. < .48 ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible));
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkInPartitionedReplayAndExactVisibilityBoundary") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::BlinkIn), reference(PixelFxKind::BlinkIn);
        double  elapsed = 0;
        for (double target : {.13, .219, .26, .36, .479, .48, .481, .6, .9, 1.4, 2.4, 3.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(live.actorVisibility() == reference.actorVisibility());
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto c) { return c == 0; }));
    }
    PixelFx fx(PixelFxKind::BlinkIn);
    for (double time : {0., .479999, .48, .480001, 1.4, .479999}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() ==
                (time < .48 ? PixelFxActorVisibility::Hidden : PixelFxActorVisibility::Visible));
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkInRequiredActorOwnershipEmptyMaskAndAtomicFailure") {
    PixelFx fx(PixelFxKind::BlinkIn);
    auto    settings      = fx.settings();
    settings.actor.source = PixelFxActorSource::Custom;
    auto prepared         = PixelFx::prepareConfiguration(PixelFxKind::BlinkIn, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    const auto originalMask = settings.actor.mask;
    settings.actor.mask[0] ^= 1;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().actor.mask == originalMask);
    const auto  value   = fx.configurationValue();
    const auto* options = value.find("options");
    REQUIRE(options);
    REQUIRE(options->find("actor"));
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::BlinkIn, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = fingerprint(fx.color()), l = fingerprint(fx.light());
    for (int invalid = 0; invalid < 3; ++invalid) {
        auto candidate = fx.settings();
        if (invalid == 0) candidate.actor.source = PixelFxActorSource::None;
        if (invalid == 1) candidate.actor.mask.clear();
        if (invalid == 2) candidate.palette.clear();
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BlinkIn, candidate));
        REQUIRE(fx.configurationValue() == value);
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(fingerprint(fx.color()) == c);
        REQUIRE(fingerprint(fx.light()) == l);
    }
    settings = fx.settings();
    std::fill(settings.actor.mask.begin(), settings.actor.mask.end(), uint8_t(0));
    auto empty = PixelFx::prepareConfiguration(PixelFxKind::BlinkIn, settings);
    REQUIRE(empty);
    REQUIRE(fx.commitConfiguration(std::move(empty).takeValue()));
    const auto emptySettings = fx.settings();
    REQUIRE(
        std::all_of(emptySettings.actor.mask.begin(), emptySettings.actor.mask.end(), [](auto v) { return v == 0; }));
}
TEST_CASE("stylize.pixelfx.arcane.blinkInOrderedLayersAndArrivalReverse") {
    PixelFx    fx(PixelFxKind::BlinkIn);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double t : {.25, .3, .4, .475, .48333333333333334, .6, 1.4, 3., .475}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .475) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkOutPinnedFlashSqueezePopGhostAndTail") {
    constexpr int      ticks[]    = {0,  1,  12, 24, 33, 34, 37, 38,  39,  40,  44,  45,  51,  52,  53,  60, 61,
                                     62, 63, 64, 72, 78, 84, 96, 108, 120, 134, 135, 156, 191, 192, 193, 240};
    constexpr uint32_t sums[][66] = {
        {37596445u,   2413956549u, 3304983778u, 78622040u,   2767329905u, 2447040446u, 1635609480u, 1128786557u,
         2625652066u, 2892579855u, 1384108725u, 1586770556u, 1812364062u, 2613438427u, 2727405889u, 225152896u,
         812589878u,  2136562064u, 3218886815u, 1668813540u, 3776001506u, 2205565424u, 2004172742u, 1203018372u,
         154215449u,  3502907201u, 1094688983u, 3305903681u, 1324333303u, 3720230113u, 1145334053u, 2644446897u,
         3836092911u, 2635858417u, 2656100707u, 1272308081u, 831522385u,  685737513u,  1812948105u, 3622104015u,
         2205204524u, 1367063721u, 1157760308u, 3857217780u, 2778258674u, 1052920935u, 1291141012u, 2215146058u,
         1740140586u, 2984156974u, 3587227159u, 3688875914u, 2688283794u, 241239934u,  3488552503u, 3122374235u,
         1928319789u, 1013937990u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u,
         2413956549u, 2413956549u},
        {4286169221u, 2413956549u, 4286169221u, 4256696649u, 1199745679u, 2819499454u, 2307968830u, 1243505238u,
         2168599052u, 359454821u,  2327387852u, 2020453634u, 125568139u,  2480985050u, 131002805u,  2218240006u,
         3413131271u, 1752161877u, 7871587u,    401330229u,  4184296113u, 4069543485u, 1193235989u, 114020u,
         470559309u,  3935133337u, 1901682316u, 4268678889u, 1056993969u, 3626851557u, 1159743376u, 4085854605u,
         2651150852u, 1117838013u, 1240659288u, 3330783569u, 3267341395u, 830462400u,  981942686u,  3536268873u,
         3874608801u, 1464343808u, 1369448390u, 4028511573u, 307560928u,  1901007372u, 4075489092u, 1778436044u,
         3767132553u, 1743257309u, 2513350533u, 2194152135u, 3249032879u, 4048895548u, 955106047u,  973264759u,
         4067270363u, 1241509138u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u, 2413956549u,
         2413956549u, 2413956549u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::BlinkOut);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::BlinkOut, settings);
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
            REQUIRE(fx.actorVisibility() ==
                    (ticks[i] / 120. < .32 ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkOutPartitionedReplayAndExactVisibilityBoundary") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::BlinkOut), reference(PixelFxKind::BlinkOut);
        double  elapsed = 0;
        for (double target : {.13, .219, .28, .319, .32, .321, .37, .44, .52, .9, 1.6, 2.4, 3.}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
                live.draw();
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(live.activeTickCount() == reference.activeTickCount());
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(live.actorVisibility() == reference.actorVisibility());
        }
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto c) { return c == 0; }));
    }
    PixelFx fx(PixelFxKind::BlinkOut);
    for (double time : {0., .319999, .32, .320001, 1.6, .319999}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() ==
                (time < .32 ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
    }
}
TEST_CASE("stylize.pixelfx.arcane.blinkOutRequiredActorOwnershipEmptyMaskAndAtomicFailure") {
    PixelFx fx(PixelFxKind::BlinkOut);
    auto    settings      = fx.settings();
    settings.actor.source = PixelFxActorSource::Custom;
    auto prepared         = PixelFx::prepareConfiguration(PixelFxKind::BlinkOut, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    const auto originalMask = settings.actor.mask;
    settings.actor.mask[0] ^= 1;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().actor.mask == originalMask);
    const auto  value   = fx.configurationValue();
    const auto* options = value.find("options");
    REQUIRE(options);
    REQUIRE(options->find("actor"));
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::BlinkOut, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = fingerprint(fx.color()), l = fingerprint(fx.light());
    for (int invalid = 0; invalid < 3; ++invalid) {
        auto candidate = fx.settings();
        if (invalid == 0) candidate.actor.source = PixelFxActorSource::None;
        if (invalid == 1) candidate.actor.mask.clear();
        if (invalid == 2) candidate.palette.clear();
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BlinkOut, candidate));
        REQUIRE(fx.configurationValue() == value);
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(fingerprint(fx.color()) == c);
        REQUIRE(fingerprint(fx.light()) == l);
    }
    settings = fx.settings();
    std::fill(settings.actor.mask.begin(), settings.actor.mask.end(), uint8_t(0));
    auto empty = PixelFx::prepareConfiguration(PixelFxKind::BlinkOut, settings);
    REQUIRE(empty);
    REQUIRE(fx.commitConfiguration(std::move(empty).takeValue()));
    const auto emptySettings = fx.settings();
    REQUIRE(
        std::all_of(emptySettings.actor.mask.begin(), emptySettings.actor.mask.end(), [](auto v) { return v == 0; }));
}
TEST_CASE("stylize.pixelfx.arcane.blinkOutGhostAlphaOrderedLayersAndReverse") {
    PixelFx    fx(PixelFxKind::BlinkOut);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.2, .325, .4, .45, .5166666666666667, .525, .65, 1.6, 3., .65}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (time == .65)
            REQUIRE(std::any_of(c.begin(), c.end(), [](auto v) { return (v >> 24) > 0 && (v >> 24) < 255; }));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == time);
        if (time == .65) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.magicCircleCastPinnedPensRunesFlashColumnBreakAndTail") {
    constexpr int ticks[] = {0,   4,   5,   12,  13,  35,  36,  50,  51,  57,  58,  62,  63,  72,  105, 106, 107, 108,
                             109, 155, 156, 157, 177, 178, 181, 182, 183, 184, 190, 191, 192, 201, 202, 240, 289, 290,
                             291, 292, 313, 314, 315, 323, 324, 325, 381, 382, 383, 413, 414, 415, 467, 468, 469, 528};
    constexpr uint32_t sums[][108] = {
        {3251880901u, 3251880901u, 3251880901u, 3251880901u, 2858376661u, 823694681u,  190339613u,  3514280748u,
         1128263169u, 1132245178u, 4099441441u, 1555345450u, 833948467u,  3634389661u, 3530872433u, 2986995944u,
         2910285045u, 203715541u,  3766569555u, 3179520425u, 4046115805u, 1429450445u, 1846646561u, 1959028596u,
         2878942565u, 3754176876u, 885397721u,  2590680070u, 3319805129u, 1051833213u, 2740741055u, 760527066u,
         641087621u,  315181476u,  1825789447u, 187024114u,  1263509217u, 1097813752u, 1692217013u, 3356021046u,
         3915401920u, 2999293212u, 2141551823u, 3296058148u, 1386980830u, 4067973020u, 2621929943u, 2520753905u,
         3038760275u, 1102726995u, 1504066563u, 2549712919u, 3093049039u, 706644331u,  3596931041u, 2502076804u,
         1058099860u, 1610436302u, 1426466058u, 2832825907u, 1178861731u, 1512544820u, 3473162030u, 2237118910u,
         3549957436u, 2180132512u, 3343461119u, 1421136814u, 3804869186u, 87316261u,   909097944u,  1264411566u,
         2083769050u, 2007195953u, 634024577u,  1628268632u, 1085715822u, 1768790950u, 2062493660u, 2563162587u,
         3416842606u, 1083534854u, 3485427652u, 3206788712u, 1726746525u, 80441980u,   1670917908u, 401927942u,
         1762866443u, 3343247020u, 4109745051u, 2863864451u, 2134290585u, 3755259899u, 1522191049u, 3657893700u,
         222587593u,  1655807633u, 222587593u,  2343461622u, 3251880901u, 3251880901u, 3251880901u, 3251880901u,
         3251880901u, 3251880901u, 3251880901u, 3251880901u},
        {3251880901u, 3251880901u, 3251880901u, 3251880901u, 310820684u,  1954451021u, 493708408u,  2094417192u,
         534703836u,  675916470u,  2740816617u, 3184095263u, 977862809u,  4119046626u, 232352752u,  4079732533u,
         1912179073u, 731759233u,  3693286224u, 2177594337u, 448638953u,  2277210441u, 2183369130u, 106733035u,
         697897482u,  2873752798u, 2126441427u, 1597832998u, 2823265012u, 2146492026u, 3325360481u, 3892967893u,
         257593637u,  1835052265u, 3971830263u, 402127812u,  2590021245u, 445848815u,  658219090u,  859778085u,
         2776760331u, 201496661u,  124574985u,  2005842449u, 2264242591u, 3651994228u, 1222529204u, 2412927657u,
         2148658335u, 2661129691u, 2363555099u, 1226525505u, 3160545082u, 3054960819u, 3642172480u, 1694800965u,
         56815086u,   733549130u,  3876637466u, 2125759509u, 3666808521u, 1117925220u, 806252277u,  3823425511u,
         3843266291u, 553273836u,  3258284177u, 3407731480u, 1168685884u, 3957237453u, 3276168815u, 582171393u,
         642090754u,  507715650u,  1686546172u, 2757676778u, 3410411934u, 2406620050u, 2291994238u, 1139469144u,
         250203909u,  3338907060u, 2781906937u, 3626242450u, 2796899358u, 2323819162u, 95668375u,   1834324771u,
         4010278942u, 3027847612u, 3666867411u, 781874450u,  3785497653u, 864372650u,  640046605u,  2165642923u,
         3232566285u, 268315535u,  1143410433u, 2908124479u, 3251880901u, 3251880901u, 3251880901u, 3251880901u,
         3251880901u, 3251880901u, 3251880901u, 3251880901u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::MagicCircleCast);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::MagicCircleCast, settings);
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
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.magicCircleCastPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::MagicCircleCast), reference(PixelFxKind::MagicCircleCast);
        double  elapsed = 0;
        for (double target :
             {.04, .42, .6, .88, .9, 1.3, 1.519, 1.52, 1.525, 1.8, 2.419, 2.42, 2.425, 3.18, 3.45, 3.9, 4.4, 5.}) {
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
TEST_CASE("stylize.pixelfx.arcane.magicCircleCastOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::MagicCircleCast);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::MagicCircleCast, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::MagicCircleCast, value);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::MagicCircleCast, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.magicCircleCastOrderedFarColumnAndNearComposition") {
    PixelFx    fx(PixelFxKind::MagicCircleCast);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.2, .6, 1.3, 1.525, 1.8, 2.425, 3.2, 3.9, 4.4, 1.8}) {
        auto seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> c(fx.color().size()), l(c.size());
        PixelFxCanvas         canvas{fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (time == 1.8) REQUIRE(std::any_of(c.begin(), c.end(), [](auto v) { return v != 0; }));
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
TEST_CASE("stylize.pixelfx.arcane.portalPinnedRiftWellAndTwoLoops") {
    constexpr int      ticks[]    = {0,   1,   12,  24,  36,  60,  72,  96,  108, 120, 143, 144,
                                     145, 180, 216, 240, 264, 287, 288, 289, 360, 432, 575, 576};
    constexpr uint32_t sums[][48] = {
        {3015141287u, 1453645308u, 612757036u,  2308971194u, 436037618u,  1379259195u, 3069499547u, 4201993040u,
         1422359408u, 3186924987u, 3100527662u, 3834363171u, 1155640353u, 993993596u,  3903405416u, 3335796330u,
         4065605478u, 1063296541u, 617059660u,  1949955355u, 4149058886u, 954702250u,  2404083835u, 1569655780u,
         3484471522u, 4026832596u, 2574660179u, 1561830802u, 2882270161u, 295241203u,  3328339136u, 87292384u,
         736097748u,  99763971u,   3153604902u, 1715097720u, 3015141287u, 1453645308u, 612757036u,  2308971194u,
         1155640353u, 993993596u,  2404083835u, 1569655780u, 3153604902u, 1715097720u, 3015141287u, 1453645308u},
        {950902263u,  1539920558u, 3294707346u, 3716222407u, 2560949898u, 607989317u,  1890533317u, 3115525860u,
         13570020u,   2368488811u, 1791195850u, 1282452281u, 1348699597u, 1422499268u, 2067641858u, 1234564595u,
         3876338211u, 3183412907u, 4258807549u, 2067132146u, 2006950508u, 4010662155u, 1989315397u, 2487806502u,
         786441088u,  199136405u,  3956757825u, 4203685348u, 1298154816u, 2748172337u, 4133022345u, 3426456251u,
         3092190459u, 551743122u,  1217093522u, 185326560u,  950902263u,  1539920558u, 3294707346u, 3716222407u,
         1348699597u, 1422499268u, 1989315397u, 2487806502u, 1217093522u, 185326560u,  950902263u,  1539920558u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::Portal);
        REQUIRE(!fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Portal, settings);
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
TEST_CASE("stylize.pixelfx.arcane.sourceToneDitherAndSharpness") {
    PixelFx            fx(PixelFxKind::Portal);
    const auto         ramp   = fx.settings().palette;
    constexpr uint32_t sums[] = {827582586u, 3911094022u, 1676248796u};
    size_t             index  = 0;
    for (double sharp : {0., .65, 1.}) {
        std::vector<uint32_t> words;
        for (int y = -2; y < 2; ++y)
            for (int x = -2; x < 2; ++x)
                for (double intensity : {-.1, 0., .01, .06, .2, .5, .99, 1., 1.1})
                    words.push_back(arcane::tone(ramp, intensity, x + .25, y + .75, sharp == .65 ? 0 : .06, sharp));
        REQUIRE(fingerprint(words) == sums[index++]);
    }
}
TEST_CASE("stylize.pixelfx.arcane.portalDimensionsAtomicBoundsAndRoundtrip") {
    PixelFx fx(PixelFxKind::Portal);
    REQUIRE(fx.settings().arcane.portalWidth == 17);
    REQUIRE(fx.settings().arcane.portalHeight == 29);
    auto seek = fx.prepareSeek(.7);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto value = fx.configurationValue();
    const auto color = fingerprint(fx.color()), light = fingerprint(fx.light());
    for (double bad :
         {0., -.5, .124, 128.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        for (int dimension = 0; dimension < 2; ++dimension) {
            auto settings = fx.settings();
            if (dimension == 0)
                settings.arcane.portalWidth = bad;
            else
                settings.arcane.portalHeight = bad;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Portal, settings));
            REQUIRE(fx.configurationValue() == value);
            REQUIRE(fx.effectTime() == .7);
            REQUIRE(fingerprint(fx.color()) == color);
            REQUIRE(fingerprint(fx.light()) == light);
        }
    for (double bound : {.125, 128.}) {
        auto settings                = fx.settings();
        settings.arcane.portalWidth  = bound;
        settings.arcane.portalHeight = bound;
        auto prepared                = PixelFx::prepareConfiguration(PixelFxKind::Portal, settings);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        const auto config = fx.configurationValue();
        auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Portal, config);
        REQUIRE(rt);
        REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
        REQUIRE(fx.configurationValue() == config);
        REQUIRE(fx.settings().arcane.portalWidth == bound);
        REQUIRE(fx.settings().arcane.portalHeight == bound);
    }
    auto settings                = fx.settings();
    settings.arcane.portalWidth  = 31;
    settings.arcane.portalHeight = 14;
    auto prepared                = PixelFx::prepareConfiguration(PixelFxKind::Portal, settings);
    REQUIRE(prepared);
    settings.arcane.portalWidth = 1;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().arcane.portalWidth == 31);
    REQUIRE(fx.settings().arcane.portalHeight == 14);
}
TEST_CASE("stylize.pixelfx.arcane.portalAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Portal), reference(PixelFxKind::Portal);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 2.399, 2.4, 2.401, 4.8, 4.913}) {
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
    PixelFx fx(PixelFxKind::Portal);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Portal, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Portal, config);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Portal, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.portalOrderedLayersAndReverse") {
    PixelFx    fx(PixelFxKind::Portal);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.3, .6, 1.2, 2.399999, 2.4, 4.8, .3}) {
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
        if (time == .3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.arcane.timeStopPinnedTicksFreezeCracksShatterAndTail") {
    constexpr int      ticks[]    = {0,   1,   12,  35,  36,  43,  44,  45,  55,  56,  68,  69,  83,  84,  102,
                                     103, 123, 124, 148, 149, 178, 179, 209, 210, 211, 212, 217, 218, 263, 264,
                                     265, 270, 284, 285, 290, 291, 292, 312, 336, 384, 431, 432, 433, 480};
    constexpr uint32_t sums[][88] = {
        {2378289469u, 4271100220u, 225085685u,  2404284633u, 2595659663u, 439773162u,  924304951u,  2151620005u,
         907213998u,  452647898u,  1722940689u, 1578189187u, 1877552903u, 1903574110u, 1102897609u, 1238539984u,
         4060732008u, 4198275492u, 3602752022u, 960185286u,  2937969725u, 3022313107u, 3869743706u, 2384113498u,
         2865505401u, 914898920u,  308961528u,  1356685541u, 3701594838u, 2711335445u, 3163156575u, 2605481645u,
         664301986u,  1514778971u, 1489520246u, 3767018594u, 2201670090u, 3761842331u, 1480520490u, 31510719u,
         1714117209u, 860169923u,  1484000744u, 2554067006u, 977762421u,  1023414172u, 3373810845u, 1185204681u,
         3627021281u, 3941780182u, 3627021281u, 3161509438u, 1507694307u, 2482817188u, 2575403548u, 3389375438u,
         2575403548u, 854628710u,  1940188781u, 854628710u,  1023286822u, 854628710u,  1218899239u, 854628710u,
         625272608u,  854628710u,  615164906u,  854628710u,  776956364u,  854628710u,  2935940703u, 2908517718u,
         2579604400u, 3735626722u, 1770110010u, 1976576385u, 1461882518u, 2566439432u, 105586424u,  1791258612u,
         1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u},
        {4158193245u, 2528957127u, 16581602u,   2819126382u, 3038747832u, 144019453u,  3080185333u, 2195947209u,
         1603266313u, 2266263248u, 1507458218u, 2893839863u, 2163592097u, 1853726498u, 2347173673u, 3513595581u,
         587057442u,  1098108881u, 3719518727u, 219001576u,  2909815822u, 3053237632u, 2591377617u, 2233864826u,
         3559870659u, 683310697u,  3779249468u, 1564145309u, 1451874995u, 85173607u,   239157376u,  3258247667u,
         1151478730u, 1341672662u, 1787676892u, 2956707966u, 1545845494u, 3574104507u, 2988953474u, 1140813766u,
         2786731984u, 1706150846u, 1810983806u, 1901934726u, 2415560349u, 481234120u,  2415560349u, 165059560u,
         3670531212u, 2232691771u, 3670531212u, 3755161693u, 1862308778u, 1031545371u, 2922962686u, 3541357945u,
         2922962686u, 1105621941u, 359521590u,  1105621941u, 4221912786u, 1105621941u, 4138607324u, 1105621941u,
         2498558914u, 1105621941u, 3974086374u, 1105621941u, 4263406186u, 1105621941u, 1387213858u, 192360514u,
         1918803411u, 382417502u,  1238486411u, 2468959166u, 1986024859u, 2290823791u, 2798096290u, 1076475845u,
         1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u, 1076475845u},
    };
    for (int variant = 0; variant < 2; ++variant) {
        PixelFx fx(PixelFxKind::TimeStop);
        REQUIRE(fx.isSimulated());
        if (variant) {
            auto settings    = fx.settings();
            settings.seed    = 123;
            settings.palette = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
            auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::TimeStop, settings);
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
TEST_CASE("stylize.pixelfx.arcane.timeStopPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::TimeStop), reference(PixelFxKind::TimeStop);
        double  elapsed = 0;
        for (double target : {.3, .36, .7, 1.49, 1.749, 1.75, 1.759, 1.82, 2.2, 2.3, 2.419, 2.425, 2.7, 3.6, 4., 5.}) {
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
TEST_CASE("stylize.pixelfx.arcane.timeStopOwningConfigurationAndAtomicFailure") {
    PixelFx fx(PixelFxKind::TimeStop);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::TimeStop, settings);
    REQUIRE(candidate);
    auto stale = fx.prepareSeek(.8);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::TimeStop, value);
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
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::TimeStop, settings));
    REQUIRE(fx.configurationValue() == value);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(fingerprint(fx.color()) == color);
    REQUIRE(fingerprint(fx.light()) == light);
}
TEST_CASE("stylize.pixelfx.arcane.timeStopGlassLayerOrderAndFrozenReverse") {
    PixelFx    fx(PixelFxKind::TimeStop);
    const auto fxInfo = fx.info();
    uint32_t   first  = 0;
    for (double time : {.4, 1., 1.75, 1.759, 1.82, 2.3, 2.425, 2.7, 3.6, 4., 2.3}) {
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
        if (time == 2.3) {
            if (first)
                REQUIRE(fingerprint(fx.color()) == first);
            else
                first = fingerprint(fx.color());
        }
    }
}
}  // namespace test_pixelfx_arcane_unit
