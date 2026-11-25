// Status kernels and prepared character silhouettes; pinned upstream fixtures are independent of native drawing.
#include <algorithm>
#include <array>
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/StatusEyes.h"
#include "stylize/pixelfx/StatusShape.h"
#include "stylize/pixelfx/StatusVisual.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_status_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> p) {
    uint32_t h = 2166136261u;
    for (auto c : p)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
uint32_t alphaChecksum(std::span<const uint32_t> p) {
    uint32_t h = 2166136261u;
    for (auto c : p) h = (h ^ (c >> 24)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.status.hastePinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 2512224569u, 2079083218u, 984569823u, true},
        {0.008333333333333333, 1582828351u, 2565652805u, 1185630353u, true},
        {0.1, 396915021u, 3214316272u, 1955487147u, true},
        {0.2, 584101378u, 710192640u, 4051392762u, true},
        {0.2916666666666667, 2542300716u, 3847232739u, 1472730947u, true},
        {0.3, 1863622966u, 112015663u, 713634799u, true},
        {0.30833333333333335, 283529746u, 2217420429u, 2339510126u, true},
        {0.5916666666666667, 307314700u, 1257368321u, 2378604917u, true},
        {0.6, 3907518276u, 3846139870u, 3631970612u, true},
        {0.6083333333333333, 1139660881u, 1261402073u, 278169423u, true},
        {0.7916666666666666, 45523965u, 3132937419u, 66865241u, true},
        {1, 216942005u, 3350063753u, 871231989u, true},
        {1.1916666666666667, 3863976156u, 4064620408u, 762179144u, true},
        {1.2, 2512224569u, 2079083218u, 984569823u, true},
        {1.2083333333333333, 1582828351u, 2565652805u, 1185630353u, true},
        {1.5, 1863622966u, 112015663u, 713634799u, true},
        {2, 3987148144u, 3386179045u, 1689969320u, true},
        {2.3916666666666666, 3863976156u, 4064620408u, 762179144u, true},
        {2.4, 2512224569u, 2079083218u, 984569823u, true},
        {2.408333333333333, 1582828351u, 2565652805u, 1185630353u, true},
        {3, 3907518276u, 3846139870u, 3631970612u, true},
        {4, 52853984u, 4231763016u, 1084273101u, true},
        {4.091666666666667, 3800734604u, 3337621777u, 4243629651u, true},
        {4.116666666666666, 2051368953u, 2786635664u, 3083386728u, true},
        {5, 584101378u, 710192640u, 4051392762u, true},
    };
    PixelFx fx(PixelFxKind::Haste);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.hasteLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Haste);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.hastePartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Haste), reference(PixelFxKind::Haste);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.hasteOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Haste);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Haste, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Haste, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Haste);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Haste, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Haste, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Haste, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.slowedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 3271564737u, 3719076529u, 2442253158u, true},
        {0.008333333333333333, 2962943289u, 584877743u, 989847623u, true},
        {0.1, 2761772581u, 4026642284u, 2952048994u, true},
        {0.2, 1308683333u, 3797678742u, 1903670069u, true},
        {0.2916666666666667, 2975870022u, 200827288u, 1913385852u, true},
        {0.3, 2099975769u, 2029664541u, 2951770983u, false},
        {0.30833333333333335, 3484223948u, 252795066u, 1306731754u, true},
        {0.5916666666666667, 846059402u, 401121051u, 3299933394u, true},
        {0.6, 3825948406u, 1146737534u, 2587837898u, true},
        {0.6083333333333333, 2404158528u, 1414061291u, 279494136u, true},
        {0.7916666666666666, 2604682333u, 2617134722u, 1479860745u, true},
        {1, 1596152047u, 902875371u, 2381025270u, true},
        {1.1916666666666667, 3280527296u, 3650612197u, 1530095569u, true},
        {1.2, 556647949u, 2762452139u, 339884540u, true},
        {1.2083333333333333, 3335705952u, 1616910533u, 877305365u, true},
        {1.5, 133680858u, 1223517869u, 861872745u, true},
        {2, 1465923332u, 802180542u, 2970528373u, true},
        {2.3916666666666666, 3427865404u, 2300414671u, 458913272u, true},
        {2.4, 3271564737u, 3719076529u, 2442253158u, true},
        {2.408333333333333, 2962943289u, 584877743u, 989847623u, true},
        {3, 3825948406u, 1146737534u, 2587837898u, true},
        {4, 3061138300u, 1366309958u, 2651151716u, true},
        {4.091666666666667, 417469151u, 3418935849u, 1053892498u, false},
        {4.116666666666666, 3760795074u, 3675327895u, 176520165u, false},
        {5, 1308683333u, 3797678742u, 1903670069u, true},
    };
    PixelFx fx(PixelFxKind::Slowed);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.slowedMeasuredExpBodyBlendRgb1") {
    struct Pixel {
        double   time;
        size_t   index;
        uint32_t color;
    };
    constexpr Pixel pixels[] = {
        {0.3, 2146, 4292863390u},
        {0.3, 2209, 4292863390u},
        {0.3, 2210, 4292863390u},
        {0.3, 2211, 4292863390u},
        {0.3, 2274, 4292863390u},
        {0.3, 2275, 4292863390u},
        {0.3, 2276, 4292863390u},
        {0.3, 2339, 4292863390u},
        {0.3, 2404, 4292863390u},
        {0.3, 2405, 4292863390u},
        {0.3, 2470, 4292863390u},
        {0.3, 2841, 4292863390u},
        {4.091666666666667, 2145, 4292929182u},
        {4.091666666666667, 2146, 4292929182u},
        {4.091666666666667, 2209, 4292929182u},
        {4.091666666666667, 2210, 4292929182u},
        {4.091666666666667, 2211, 4292929182u},
        {4.091666666666667, 2274, 4292929182u},
        {4.091666666666667, 2275, 4292929182u},
        {4.091666666666667, 2276, 4292929182u},
        {4.091666666666667, 2338, 4292929182u},
        {4.091666666666667, 2339, 4292929182u},
        {4.091666666666667, 2403, 4292929182u},
        {4.091666666666667, 2404, 4292929182u},
        {4.116666666666666, 1952, 4292929182u},
        {4.116666666666666, 2082, 4292929182u},
        {4.116666666666666, 2146, 4292929182u},
        {4.116666666666666, 2209, 4292929182u},
        {4.116666666666666, 2210, 4292929182u},
        {4.116666666666666, 2211, 4292929182u},
        {4.116666666666666, 2274, 4292929182u},
        {4.116666666666666, 2275, 4292929182u},
        {4.116666666666666, 2276, 4292929182u},
        {4.116666666666666, 2338, 4292929182u},
        {4.116666666666666, 2339, 4292929182u},
        {4.116666666666666, 2404, 4292929182u},
    };
    PixelFx fx(PixelFxKind::Slowed);
    for (const auto& p : pixels) {
        fx.render(p.time);
        const auto a = fx.color()[p.index];
        REQUIRE((a >> 24) == (p.color >> 24));
        for (int s : {0, 8, 16}) REQUIRE(std::abs(int((a >> s) & 255u) - int((p.color >> s) & 255u)) <= 1);
    }
}
TEST_CASE("stylize.pixelfx.status.slowedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Slowed);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.slowedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Slowed), reference(PixelFxKind::Slowed);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.slowedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Slowed);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Slowed, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Slowed, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Slowed);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Slowed, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Slowed, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Slowed, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.bleedingPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 2144952442u, 954452712u, 1622865769u, true},
        {0.008333333333333333, 1517307786u, 4074456910u, 2904593957u, true},
        {0.1, 2230391886u, 1082171097u, 1938708257u, true},
        {0.2, 431290079u, 3786848980u, 2824865037u, true},
        {0.2916666666666667, 4034961699u, 4100096481u, 3008767893u, true},
        {0.3, 2498253057u, 3135611569u, 3008767893u, true},
        {0.30833333333333335, 1293886516u, 1100728549u, 3008767893u, true},
        {0.5916666666666667, 652727281u, 4096828623u, 3750567821u, true},
        {0.6, 2656605281u, 3962511121u, 3750567821u, true},
        {0.6083333333333333, 812353697u, 735986401u, 3605665145u, true},
        {0.7916666666666666, 1302655985u, 3262980561u, 3753871149u, true},
        {1, 1797464753u, 1632712883u, 235902825u, true},
        {1.1916666666666667, 3747296984u, 3948984244u, 3333245849u, true},
        {1.2, 1802986671u, 3250576908u, 1622865769u, true},
        {1.2083333333333333, 3431082516u, 3784792106u, 2904593957u, true},
        {1.5, 4200221221u, 161168958u, 3008767893u, true},
        {2, 253964996u, 154633185u, 3753871149u, true},
        {2.3916666666666666, 113309968u, 3522225165u, 3333245849u, true},
        {2.4, 2144952442u, 954452712u, 1622865769u, true},
        {2.408333333333333, 1517307786u, 4074456910u, 2904593957u, true},
        {3, 2656605281u, 3962511121u, 3750567821u, true},
        {4, 707404481u, 2910101259u, 3368630149u, true},
        {4.091666666666667, 2322914558u, 3845364553u, 2816156657u, true},
        {4.116666666666666, 3112300609u, 383684547u, 1800524785u, true},
        {5, 431290079u, 3786848980u, 2824865037u, true},
    };
    PixelFx fx(PixelFxKind::Bleeding);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.bleedingLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Bleeding);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.bleedingPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Bleeding), reference(PixelFxKind::Bleeding);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.bleedingOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Bleeding);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Bleeding, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Bleeding, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Bleeding);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Bleeding, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Bleeding, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Bleeding, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}

TEST_CASE("stylize.pixelfx.status.shapeOwnsSourceOrderedGeometryAndRawColors") {
    PixelFxActor a;
    a.source = PixelFxActorSource::Custom;
    a.width  = 5;
    a.height = 4;
    a.x      = -2.25;
    a.y      = -4.5;
    a.mask   = {1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1};
    a.pixels = {0u,          4279383126u, 2148606515u, 1122867u,    0u,          4279383126u, 2148606515u,
                1122867u,    0u,          4279383126u, 2148606515u, 1122867u,    0u,          4279383126u,
                2148606515u, 1122867u,    0u,          4279383126u, 2148606515u, 1122867u};
    const pixelfx::status::Shape          shape(a);
    const std::vector<std::array<int, 2>> body = {{{0, 0}}, {{1, 0}}, {{2, 0}}, {{0, 1}}, {{2, 1}}, {{3, 1}}, {{0, 2}},
                                                  {{1, 2}}, {{2, 2}}, {{4, 2}}, {{2, 3}}, {{3, 3}}, {{4, 3}}};
    REQUIRE(shape.body == body);
    const std::vector<std::array<int, 2>> up = {{{0, 0}}, {{1, 0}}, {{2, 0}}, {{3, 1}}, {{1, 2}}, {{4, 2}}, {{3, 3}}};
    REQUIRE(shape.up == up);
    const std::vector<std::array<int, 3>> side = {{{0, 0, -1}}, {{2, 0, 1}}, {{0, 1, -1}}, {{0, 1, 1}},
                                                  {{2, 1, -1}}, {{3, 1, 1}}, {{0, 2, -1}}, {{2, 2, 1}},
                                                  {{4, 2, -1}}, {{4, 2, 1}}, {{2, 3, -1}}, {{4, 3, 1}}};
    REQUIRE(shape.side == side);
    const std::vector<std::array<int, 2>> rows = {{{0, 2}}, {{0, 3}}, {{0, 4}}, {{2, 4}}};
    REQUIRE(shape.rows == rows);
    const std::vector<std::array<double, 4>> outline = {{{0, -1, -0.4472135954999579, -0.8944271909999159}},
                                                        {{1, -1, 0, -1}},
                                                        {{2, -1, 0.4472135954999579, -0.8944271909999159}},
                                                        {{-1, 0, -0.8944271909999159, -0.4472135954999579}},
                                                        {{3, 0, 0.7071067811865475, -0.7071067811865475}},
                                                        {{-1, 1, -1, 0}},
                                                        {{1, 1, 0, 0}},
                                                        {{4, 1, 0.7071067811865475, -0.7071067811865475}},
                                                        {{-1, 2, -0.8944271909999159, 0.4472135954999579}},
                                                        {{3, 2, 0.7071067811865475, -0.7071067811865475}},
                                                        {{5, 2, 0.8944271909999159, -0.4472135954999579}},
                                                        {{0, 3, -0.4472135954999579, 0.8944271909999159}},
                                                        {{1, 3, -0.31622776601683794, 0.9486832980505138}},
                                                        {{5, 3, 0.8944271909999159, 0.4472135954999579}},
                                                        {{2, 4, -0.4472135954999579, 0.8944271909999159}},
                                                        {{3, 4, 0, 1}},
                                                        {{4, 4, 0.4472135954999579, 0.8944271909999159}}};
    REQUIRE(shape.outline == outline);
    const std::vector<int> top = {0, 0, 0, 1, 2};
    REQUIRE(shape.top == top);
    REQUIRE(pixelfx::status::colorAt(a, 0, 0) == 0u);
    REQUIRE(pixelfx::status::colorAt(a, 2, 0) == 2148606515u);
    REQUIRE(pixelfx::status::colorAt(a, 2, 1) == 1122867u);
    a.mask.assign(20, 0);
    a.pixels.assign(20, 0);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x)
            REQUIRE(shape.isOccupied(x, y) ==
                    (std::find(body.begin(), body.end(), std::array<int, 2>{x, y}) != body.end()));
    REQUIRE(!shape.isOccupied(-1, 0));
    REQUIRE(!shape.isOccupied(5, 0));
    REQUIRE(!shape.isOccupied(0, -1));
    REQUIRE(!shape.isOccupied(0, 4));
}
TEST_CASE("stylize.pixelfx.status.heightMinimumAndMaterialRejectionAreAtomic") {
    for (auto kind : {PixelFxKind::Haste, PixelFxKind::Slowed, PixelFxKind::Bleeding}) {
        PixelFx fx(kind);
        auto    settings      = fx.settings();
        settings.actor.source = PixelFxActorSource::Custom;
        settings.actor.width  = 1;
        settings.actor.height = 1;
        settings.actor.scale  = 1;
        settings.actor.x      = 0;
        settings.actor.y      = -1;
        settings.actor.head   = {0, -1};
        settings.actor.chest  = {0, -.5};
        settings.actor.eyes.clear();
        settings.actor.mask   = {1};
        settings.actor.pixels = {0};
        const auto old        = fx.configurationValue();
        auto       one        = PixelFx::prepareConfiguration(kind, settings);
        if (kind == PixelFxKind::Bleeding) {
            REQUIRE(!one);
            REQUIRE(fx.configurationValue() == old);
            REQUIRE(one.status().describe().find("/options/actor/height") != std::string::npos);
        } else
            REQUIRE(one);
        settings.actor.height = 2;
        settings.actor.mask   = {1, 1};
        settings.actor.pixels = {0, 0};
        auto two              = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(two);
        REQUIRE(fx.commitConfiguration(std::move(two).takeValue()));
        fx.render(.3);
        const auto config = fx.configurationValue();
        auto       schema = PixelFx::describeParameters(kind);
        REQUIRE(schema);
        bool found = false;
        for (const auto& param : *schema.value().find("parameters")->getIf<eve::Value::Array>())
            if (param.find("name")->asString() == "actor") {
                found = true;
                REQUIRE(param.find("constraints")->find("minimumHeight")->asInt() ==
                        (kind == PixelFxKind::Bleeding ? 2 : 1));
            }
        REQUIRE(found);
        if (kind == PixelFxKind::Haste) {
            settings = fx.settings();
            for (uint32_t c : {0u, 0x00112233u, 0x80112233u}) {
                settings.status.dust = c;
                REQUIRE(PixelFx::prepareConfiguration(kind, settings));
                REQUIRE(fx.configurationValue() == config);
            }
            for (int64_t c : {-1LL, 4294967296LL}) {
                auto invalid = config;
                invalid.find("options")->set("dust", eve::Value(c));
                REQUIRE(!PixelFx::prepareConfigurationValue(kind, invalid));
                REQUIRE(fx.configurationValue() == config);
            }
        }
    }
}

TEST_CASE("stylize.pixelfx.status.hasteStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {1, 0, 3018206143u, 3829865810u, 3634229857u, true},
        {1, 0.008333333333333333, 4142299947u, 2392408281u, 3844559279u, true},
        {1, 0.1, 676194327u, 3988272184u, 2820312630u, true},
        {1, 0.3, 709043149u, 1990469534u, 2280711183u, true},
        {1, 0.6, 2453563478u, 597344110u, 565130313u, true},
        {1, 1, 2940492234u, 1255949423u, 3283176717u, true},
        {1, 1.1916666666666667, 3031561690u, 724235917u, 4185955045u, true},
        {1, 1.2, 3018206143u, 3829865810u, 3634229857u, true},
        {1, 1.2083333333333333, 4142299947u, 2392408281u, 3844559279u, true},
        {1, 2, 2599323275u, 2782265099u, 4035349965u, true},
        {1, 2.4, 3018206143u, 3829865810u, 3634229857u, true},
        {1, 4, 686813840u, 2437588723u, 3398598248u, true},
        {2, 0, 613906484u, 551811745u, 984569823u, true},
        {2, 0.008333333333333333, 2085474988u, 1440449714u, 1185630353u, true},
        {2, 0.1, 3659792600u, 2145285861u, 1955487147u, true},
        {2, 0.3, 3181852102u, 3739716455u, 713634799u, true},
        {2, 0.6, 4113581223u, 613956963u, 3631970612u, true},
        {2, 1, 2718326692u, 3070564458u, 871231989u, true},
        {2, 1.1916666666666667, 2819153547u, 1200967755u, 762179144u, true},
        {2, 1.2, 613906484u, 551811745u, 984569823u, true},
        {2, 1.2083333333333333, 2085474988u, 1440449714u, 1185630353u, true},
        {2, 2, 3094391255u, 1135528228u, 1689969320u, true},
        {2, 2.4, 613906484u, 551811745u, 984569823u, true},
        {2, 4, 446986340u, 2369722082u, 1084273101u, true},
    };
    PixelFx fx(PixelFxKind::Haste);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Haste, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        if (f.exact) REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.slowedStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {1, 0, 1104514910u, 4270299881u, 1050298047u, true},
        {1, 0.008333333333333333, 4106948370u, 4270299881u, 3544852872u, true},
        {1, 0.3, 3741653424u, 2088362374u, 2401718777u, true},
        {1, 0.6, 605021969u, 1730641663u, 3073598385u, true},
        {1, 1, 4270913741u, 2434359691u, 3560587470u, true},
        {1, 1.1916666666666667, 3760035062u, 3822331351u, 4063772459u, true},
        {1, 1.2, 2758321947u, 1042640338u, 1240216466u, true},
        {1, 2, 3572088004u, 478767689u, 408547463u, true},
        {1, 2.4, 1104514910u, 4270299881u, 1050298047u, true},
        {1, 4.091666666666667, 1570710436u, 2331322903u, 1066721032u, true},
        {1, 4.116666666666666, 978968468u, 817196066u, 704813141u, true},
        {1, 5, 1928039741u, 1536874045u, 3385413933u, true},
        {2, 0, 1505434378u, 2696183970u, 2442253158u, true},
        {2, 0.008333333333333333, 856546081u, 2958057985u, 989847623u, true},
        {2, 0.3, 3746832100u, 156786381u, 2951770983u, false},
        {2, 0.6, 1883924108u, 3855497435u, 2587837898u, true},
        {2, 1, 2683504825u, 3582602008u, 2381025270u, true},
        {2, 1.1916666666666667, 2826313134u, 1249995033u, 1530095569u, true},
        {2, 1.2, 2362543344u, 2648925192u, 339884540u, true},
        {2, 2, 1684808363u, 1395595496u, 2970528373u, true},
        {2, 2.4, 1505434378u, 2696183970u, 2442253158u, true},
        {2, 4.091666666666667, 1854007740u, 2945048104u, 1053892498u, true},
        {2, 4.116666666666666, 1733844201u, 1420782073u, 176520165u, true},
        {2, 5, 2966839670u, 642047824u, 1903670069u, true},
    };
    PixelFx fx(PixelFxKind::Slowed);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Slowed, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        if (f.exact) REQUIRE(checksum(color) == f.color);
        if (!f.exact) {
            struct Pixel {
                size_t   index;
                uint32_t color;
            };
            constexpr Pixel pixels[] = {
                {2015, 4293584349u}, {2016, 4293584349u}, {2078, 4293584349u}, {2079, 4293584349u},
                {2080, 4293584349u}, {2142, 4293584349u}, {2399, 4293584349u}, {2459, 4293584349u},
                {2460, 4293584349u}, {2523, 4293584349u}, {2651, 4293584349u}, {2714, 4293584349u},
                {2715, 4293584349u}, {2778, 4293584349u}, {2842, 4293584349u}, {2969, 4293584349u},
            };
            for (const auto& p : pixels) {
                REQUIRE((color[p.index] >> 24) == (p.color >> 24));
                for (int bit : {0, 8, 16})
                    REQUIRE(std::abs(int((color[p.index] >> bit) & 255u) - int((p.color >> bit) & 255u)) <= 1);
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.bleedingStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {1, 0, 3120495619u, 3432459742u, 94021875u, true},
        {1, 0.008333333333333333, 1746847951u, 3432459742u, 2649997774u, true},
        {1, 0.1, 3228216401u, 2468621765u, 2633284965u, true},
        {1, 0.3, 2388486472u, 2420869819u, 3691003437u, true},
        {1, 0.6, 880355030u, 1507164185u, 3631868325u, true},
        {1, 1, 391468718u, 637104391u, 84815909u, true},
        {1, 1.1916666666666667, 908961082u, 576757565u, 2755772749u, true},
        {1, 1.2, 2006564510u, 576757565u, 94021875u, true},
        {1, 1.2083333333333333, 3400095304u, 576757565u, 2649997774u, true},
        {1, 2, 2737641721u, 576757565u, 2784191893u, true},
        {1, 2.4, 3120495619u, 3432459742u, 94021875u, true},
        {1, 4, 2637481138u, 2588771108u, 2554856365u, true},
        {2, 0, 1203236454u, 3958060474u, 1622865769u, true},
        {2, 0.008333333333333333, 3067849097u, 3958060474u, 2904593957u, true},
        {2, 0.1, 3068767561u, 1947429281u, 1938708257u, true},
        {2, 0.3, 790416961u, 4277990297u, 3008767893u, true},
        {2, 0.6, 3233230470u, 1461201681u, 3750567821u, true},
        {2, 1, 3618642209u, 3143160844u, 235902825u, true},
        {2, 1.1916666666666667, 191099039u, 2217719694u, 3333245849u, true},
        {2, 1.2, 846107067u, 2217719694u, 1622865769u, true},
        {2, 1.2083333333333333, 1277827260u, 2217719694u, 2904593957u, true},
        {2, 2, 2569251840u, 1809684367u, 3753871149u, true},
        {2, 2.4, 1203236454u, 3958060474u, 1622865769u, true},
        {2, 4, 3631469405u, 1782955613u, 3368630149u, true},
    };
    PixelFx fx(PixelFxKind::Bleeding);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Bleeding, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        if (f.exact) REQUIRE(checksum(color) == f.color);
    }
}


TEST_CASE("stylize.pixelfx.status.stunnedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 2447965148u, 3984116047u, 4088677257u, true},
        {0.008333333333333333, 1837944764u, 92092838u, 1933758541u, true},
        {0.1, 1721702610u, 4049312955u, 121623464u, true},
        {0.2, 1209759205u, 1149422712u, 3540519330u, true},
        {0.2916666666666667, 2044282524u, 590865155u, 2931453216u, true},
        {0.3, 3873062138u, 3223404482u, 2804695908u, true},
        {0.30833333333333335, 2613011283u, 3554030756u, 318731827u, true},
        {0.5916666666666667, 1132340774u, 3718072857u, 3510283851u, true},
        {0.6, 1183648328u, 881645841u, 3601127760u, true},
        {0.6083333333333333, 1290287645u, 24082573u, 101455291u, true},
        {0.7916666666666666, 4220135772u, 3999757877u, 3221432672u, true},
        {1, 2436882424u, 3652463407u, 3891260666u, true},
        {1.1916666666666667, 3176989328u, 3040187588u, 3413639031u, true},
        {1.2, 3836324375u, 2123806166u, 64680031u, true},
        {1.2083333333333333, 2749291828u, 670651514u, 2137292203u, true},
        {1.5, 2202779883u, 1411454492u, 753979899u, true},
        {2, 2447965148u, 3984116047u, 4088677257u, true},
        {2.3916666666666666, 2816410057u, 3195847889u, 3240679452u, true},
        {2.4, 3298735265u, 164933274u, 1827879250u, true},
        {2.408333333333333, 2521889456u, 1565246931u, 1840903333u, true},
        {3, 2436882424u, 3652463407u, 3891260666u, true},
        {4, 2447965148u, 3984116047u, 4088677257u, true},
        {4.091666666666667, 2653471095u, 1823392145u, 1974134149u, true},
        {4.116666666666666, 2835846047u, 348705793u, 1007713743u, true},
        {5, 2436882424u, 3652463407u, 3891260666u, true},
    };
    PixelFx fx(PixelFxKind::Stunned);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.stunnedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Stunned);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.stunnedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Stunned), reference(PixelFxKind::Stunned);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.stunnedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Stunned);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Stunned, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Stunned, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Stunned);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Stunned, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Stunned, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Stunned, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.sleepPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 3230703250u, 237976728u, 4054921533u, true},
        {0.008333333333333333, 3845140324u, 780283123u, 1739358571u, true},
        {0.1, 3269357898u, 2920204496u, 4099373582u, true},
        {0.2, 112750081u, 1048275664u, 1175667891u, true},
        {0.2916666666666667, 3212132683u, 1653109856u, 1765740463u, true},
        {0.3, 2979842756u, 1149102235u, 4095979270u, true},
        {0.30833333333333335, 254651994u, 3102394897u, 3872979018u, true},
        {0.5916666666666667, 3751698216u, 850246053u, 1047872115u, true},
        {0.6, 3265080162u, 2900271345u, 3086121436u, true},
        {0.6083333333333333, 2111660358u, 3093427414u, 3680197727u, true},
        {0.7916666666666666, 3787378735u, 18535055u, 3397621215u, true},
        {1, 2511448044u, 3626714748u, 2396502363u, true},
        {1.1916666666666667, 3489785511u, 57781873u, 1966474839u, true},
        {1.2, 1094782879u, 1862438325u, 2792547783u, true},
        {1.2083333333333333, 765606046u, 2547729257u, 2057205116u, true},
        {1.5, 742647092u, 2224835321u, 716888281u, true},
        {2, 1380263032u, 3322751665u, 1208002449u, true},
        {2.3916666666666666, 1501261730u, 1041679194u, 261642348u, true},
        {2.4, 3230703250u, 237976728u, 4054921533u, true},
        {2.408333333333333, 3845140324u, 780283123u, 1739358571u, true},
        {3, 3265080162u, 2900271345u, 3086121436u, true},
        {4, 2133931049u, 478692345u, 3094323418u, true},
        {4.091666666666667, 3340696561u, 2913437288u, 2086420645u, true},
        {4.116666666666666, 956717160u, 2628794013u, 3355533003u, true},
        {5, 112750081u, 1048275664u, 1175667891u, true},
    };
    PixelFx fx(PixelFxKind::Sleep);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.sleepLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Sleep);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.sleepPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Sleep), reference(PixelFxKind::Sleep);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.sleepOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Sleep);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Sleep, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Sleep, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Sleep);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Sleep, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Sleep, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Sleep, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.confusedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 2178387850u, 1212047327u, 3797373131u, true},
        {0.008333333333333333, 1640215036u, 1357154441u, 1585274090u, true},
        {0.1, 3142095875u, 792533236u, 2770686313u, true},
        {0.2, 2537065120u, 1180560689u, 1823193974u, true},
        {0.2916666666666667, 1403831150u, 3542989032u, 2119348165u, true},
        {0.3, 3509808051u, 2624403861u, 1622554263u, true},
        {0.30833333333333335, 3565210652u, 1659251322u, 76573083u, true},
        {0.5916666666666667, 1932369513u, 932960673u, 3516145485u, true},
        {0.6, 2556674620u, 1919595016u, 1404822442u, true},
        {0.6083333333333333, 1907188885u, 2295231001u, 2273620894u, true},
        {0.7916666666666666, 1874532953u, 350085577u, 3423139448u, true},
        {1, 3625270585u, 1266143069u, 2093841682u, true},
        {1.1916666666666667, 4029033858u, 2875189600u, 2394330012u, true},
        {1.2, 409871978u, 3276534534u, 2097858839u, true},
        {1.2083333333333333, 253617360u, 547386988u, 3009418377u, true},
        {1.5, 1900193805u, 3175385763u, 1075672270u, true},
        {2, 2539767660u, 1716082196u, 3487844627u, true},
        {2.3916666666666666, 3433475424u, 560289112u, 3219230866u, true},
        {2.4, 2178387850u, 1212047327u, 3797373131u, true},
        {2.408333333333333, 1640215036u, 1357154441u, 1585274090u, true},
        {3, 2556674620u, 1919595016u, 3011310918u, true},
        {4, 4214181378u, 4273727773u, 2658240449u, true},
        {4.091666666666667, 3044481645u, 1399784590u, 1287735675u, true},
        {4.116666666666666, 3759348701u, 1124360010u, 1293441689u, true},
        {5, 2537065120u, 1180560689u, 1823193974u, true},
    };
    PixelFx fx(PixelFxKind::Confused);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.confusedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Confused);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.confusedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Confused), reference(PixelFxKind::Confused);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.confusedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Confused);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Confused, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Confused, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Confused);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Confused, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Confused, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Confused, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.charmedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 4094330556u, 1124388582u, 834675050u, true},
        {0.008333333333333333, 384213569u, 720840830u, 1764421122u, true},
        {0.1, 3397290170u, 554808130u, 785893246u, true},
        {0.2, 2380109545u, 837912916u, 2512631379u, true},
        {0.2916666666666667, 1545469826u, 2519620745u, 1073445066u, true},
        {0.3, 1318018762u, 2656736116u, 202854911u, true},
        {0.30833333333333335, 837765849u, 1730152807u, 2711572815u, true},
        {0.5916666666666667, 1205116518u, 1914093190u, 1929031948u, true},
        {0.6, 3922243637u, 2140871721u, 2041254653u, true},
        {0.6083333333333333, 2811486904u, 2577668438u, 2753280166u, true},
        {0.7916666666666666, 2177191186u, 952139759u, 3346237936u, true},
        {1, 4002834127u, 244003764u, 2603879130u, true},
        {1.1916666666666667, 873762757u, 3306261090u, 4179751695u, true},
        {1.2, 684254718u, 2643878682u, 2054186633u, true},
        {1.2083333333333333, 639830374u, 3028270473u, 1277383786u, true},
        {1.5, 3918349884u, 3103405656u, 3238453171u, true},
        {2, 2887697667u, 2725088345u, 3928960389u, true},
        {2.3916666666666666, 3550121875u, 2326286210u, 4187215605u, true},
        {2.4, 4094330556u, 1124388582u, 834675050u, true},
        {2.408333333333333, 384213569u, 720840830u, 1764421122u, true},
        {3, 3922243637u, 2140871721u, 2041254653u, true},
        {4, 2109611675u, 3773933209u, 1597241162u, true},
        {4.091666666666667, 2258347092u, 1538599537u, 1062017399u, true},
        {4.116666666666666, 1387342181u, 1714330916u, 86174286u, true},
        {5, 2380109545u, 837912916u, 2512631379u, true},
    };
    PixelFx fx(PixelFxKind::Charmed);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.charmedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Charmed);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.charmedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Charmed), reference(PixelFxKind::Charmed);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.charmedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Charmed);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Charmed, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Charmed, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Charmed);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Charmed, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Charmed, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Charmed, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.cursedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 3224715050u, 624805485u, 1932183833u, true},
        {0.008333333333333333, 1529381450u, 3498701809u, 1374996838u, true},
        {0.1, 3733666157u, 1685221173u, 2109733492u, true},
        {0.2, 2772226348u, 3982601973u, 4286148525u, true},
        {0.2916666666666667, 1305414538u, 3207743233u, 82106867u, true},
        {0.3, 674055691u, 1550640875u, 438306535u, true},
        {0.30833333333333335, 2838380717u, 2755017633u, 3432539996u, true},
        {0.5916666666666667, 1481425879u, 3891508007u, 991229178u, true},
        {0.6, 1540625934u, 1158788270u, 2553667390u, true},
        {0.6083333333333333, 2771225259u, 2528402266u, 641053485u, true},
        {0.7916666666666666, 1561726474u, 1827661422u, 4206100909u, true},
        {1, 356201570u, 200951463u, 1556359324u, true},
        {1.1916666666666667, 566674231u, 3979810406u, 3738677700u, true},
        {1.2, 1664696017u, 4011457255u, 2117016972u, true},
        {1.2083333333333333, 1423997545u, 3613844189u, 880571925u, true},
        {1.5, 735694381u, 3916757178u, 62744391u, true},
        {2, 1610635386u, 3247213165u, 4091160467u, true},
        {2.3916666666666666, 16270363u, 3218265975u, 226676373u, true},
        {2.4, 3224715050u, 624805485u, 1932183833u, true},
        {2.408333333333333, 1529381450u, 3498701809u, 1374996838u, true},
        {3, 667865951u, 1158788270u, 2553667390u, true},
        {4, 302606197u, 3229653280u, 4116939659u, true},
        {4.091666666666667, 2838311211u, 3121142811u, 1955944461u, true},
        {4.116666666666666, 561325501u, 944600820u, 839790607u, true},
        {5, 3232974024u, 3982601973u, 4286148525u, true},
    };
    PixelFx fx(PixelFxKind::Cursed);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.cursedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Cursed);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.cursedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Cursed), reference(PixelFxKind::Cursed);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.cursedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Cursed);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Cursed, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Cursed, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Cursed);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Cursed, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Cursed, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Cursed, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}

TEST_CASE("stylize.pixelfx.status.eyesOwnHeroAutoAndDummyDeclaredSourceComponents") {
    {
        PixelFx fx(PixelFxKind::Haste);
        auto    settings      = fx.settings();
        settings.actor.source = PixelFxActorSource::Hero;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Haste, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto       a    = fx.settings().actor;
        const auto list = pixelfx::status::eyes(a, pixelfx::status::Shape(a));
        a.mask.clear();
        a.pixels.clear();
        a.eyes.clear();
        REQUIRE(list.size() == 2);
        REQUIRE(list[0].x0 == 4);
        REQUIRE(list[0].x1 == 4);
        REQUIRE(list[0].y0 == 5);
        REQUIRE(list[0].y1 == 6);
        REQUIRE(list[0].skin == 4288529651u);
        REQUIRE(list[0].pixels == (std::vector<std::array<int, 2>>{{{4, 5}}, {{4, 6}}}));
        REQUIRE(list[1].x0 == 7);
        REQUIRE(list[1].x1 == 7);
        REQUIRE(list[1].y0 == 5);
        REQUIRE(list[1].y1 == 6);
        REQUIRE(list[1].skin == 4288529651u);
        REQUIRE(list[1].pixels == (std::vector<std::array<int, 2>>{{{7, 5}}, {{7, 6}}}));
    }
    {
        PixelFx fx(PixelFxKind::Haste);
        auto    settings      = fx.settings();
        settings.actor.source = PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Haste, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto       a    = fx.settings().actor;
        const auto list = pixelfx::status::eyes(a, pixelfx::status::Shape(a));
        a.mask.clear();
        a.pixels.clear();
        a.eyes.clear();
        REQUIRE(list.size() == 2);
        REQUIRE(list[0].x0 == 13);
        REQUIRE(list[0].x1 == 13);
        REQUIRE(list[0].y0 == 3);
        REQUIRE(list[0].y1 == 4);
        REQUIRE(list[0].skin == 4281543466u);
        REQUIRE(list[0].pixels == (std::vector<std::array<int, 2>>{{{13, 3}}, {{13, 4}}}));
        REQUIRE(list[1].x0 == 15);
        REQUIRE(list[1].x1 == 15);
        REQUIRE(list[1].y0 == 3);
        REQUIRE(list[1].y1 == 4);
        REQUIRE(list[1].skin == 4279503886u);
        REQUIRE(list[1].pixels == (std::vector<std::array<int, 2>>{{{15, 3}}, {{15, 4}}}));
    }
    PixelFx fx(PixelFxKind::Haste);
    auto    a = fx.settings().actor;
    a.source  = PixelFxActorSource::Custom;
    a.eyes.clear();
    REQUIRE(pixelfx::status::eyes(a, pixelfx::status::Shape(a)).empty());
    a.width        = 1;
    a.height       = 1;
    a.mask         = {1};
    a.pixels       = {0};
    a.eyes         = {{{0, 0}}};
    const auto one = pixelfx::status::eyes(a, pixelfx::status::Shape(a));
    REQUIRE(one.size() == 1);
    REQUIRE(one[0].y1 == 1);
    REQUIRE(one[0].skin == 0u);
    REQUIRE(one[0].pixels == (std::vector<std::array<int, 2>>{{{0, 0}}, {{0, 1}}}));
}
TEST_CASE("stylize.pixelfx.status.iconSourceGlyphTransformPaintAndDither") {
    using namespace pixelfx;
    const PixelFxInfo                         info{PixelFxKind::Haste, "probe", 64, 64, 32, 32, 1, false};
    constexpr std::array<std::string_view, 3> rows{".e.", "b#w", ".#."};
    const std::array<uint32_t, 4>             ramp{0xff110c08u, 0xff503828u, 0xffa07850u, 0xffeacfa0u};
    constexpr uint32_t expected[] = {56718091u, 3153704429u, 543681080u, 2117492829u, 946445765u, 946445765u};
    for (int i = 0; i < 6; ++i) {
        std::vector<uint32_t> colors(4096), lights(4096);
        Paint                 p{info, colors, lights, 1};
        if (i == 0)
            status::icon(
                p, -.5, .5, 8, [&](int x, int y) { return status::bitmap(rows, x, y); }, ramp, .55,
                [](char c) -> std::optional<uint32_t> {
                    if (c == 'e') return 0u;
                    if (c == 'b') return 0x80112233u;
                    if (c == 'w') return 0xffffffffu;
                    return std::nullopt;
                });
        if (i == 1)
            status::icon(p, -.5, .5, 8, [&](int x, int y) { return status::bitmap(rows, x, y, 2, true); }, ramp);
        if (i == 2)
            status::icon(p, -.5, .5, 8,
                         status::shaped([](double u, double v) { return status::isInsideStar(u, v, .44); }, 4.8, .37),
                         ramp);
        if (i == 3) status::icon(p, -.5, .5, 8, status::shaped(status::isInsideHeart, 5.6, .2), ramp);
        if (i == 4) status::icon(p, -.5, .5, 8, [](int, int) -> char { return 0; }, ramp);
        if (i == 5) status::icon(p, -.5, .5, 8, [&](int x, int y) { return status::bitmap(rows, x, y); }, ramp, 0);
        REQUIRE(checksum(colors) == expected[i]);
        REQUIRE(std::all_of(lights.begin(), lights.end(), [](auto c) { return c == 0; }));
    }
}

TEST_CASE("stylize.pixelfx.status.birdAndMarkRampsOwnValidateAndDescribe") {
    for (auto kind : {PixelFxKind::Stunned, PixelFxKind::Confused}) {
        PixelFx fx(kind);
        auto    settings = fx.settings();
        auto&   ramp     = kind == PixelFxKind::Stunned ? settings.status.birds : settings.status.marks;
        ramp             = {0xff123456u, 0xffabcdefu};
        auto prepared    = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(prepared);
        ramp.assign(2, 0xff000000u);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        auto        actual     = fx.settings();
        const auto& normalized = kind == PixelFxKind::Stunned ? actual.status.birds : actual.status.marks;
        REQUIRE(normalized == (std::vector<uint32_t>{0xff123456u, 0xff123456u, 0xffabcdefu, 0xffabcdefu}));
        fx.render(.3);
        const auto config = fx.configurationValue();
        const auto time   = fx.effectTime();
        const auto c = checksum(fx.color()), l = checksum(fx.light());
        for (const auto& invalid :
             std::vector<std::vector<uint32_t>>{{}, {0u}, {0x80112233u}, std::vector<uint32_t>(65, 0xffffffffu)}) {
            auto  bad   = fx.settings();
            auto& field = kind == PixelFxKind::Stunned ? bad.status.birds : bad.status.marks;
            field       = invalid;
            REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
            REQUIRE(fx.configurationValue() == config);
            REQUIRE(fx.effectTime() == time);
            REQUIRE(checksum(fx.color()) == c);
            REQUIRE(checksum(fx.light()) == l);
        }
        auto schema = PixelFx::describeParameters(kind);
        REQUIRE(schema);
        bool found = false;
        for (const auto& param : *schema.value().find("parameters")->getIf<eve::Value::Array>())
            if (param.find("name")->asString() == (kind == PixelFxKind::Stunned ? "birds" : "marks")) {
                found = true;
                REQUIRE(param.find("rampSteps")->asInt() == 4);
                REQUIRE(param.find("minimum")->asInt() == 1);
                REQUIRE(param.find("maximum")->asInt() == 64);
            }
        REQUIRE(found);
    }
}
TEST_CASE("stylize.pixelfx.status.stunnedBeakScalarRawBitsAndTypedRejection") {
    PixelFx    fx(PixelFxKind::Stunned);
    const auto original = fx.configurationValue();
    for (uint32_t c : {0u, 0x00112233u, 0x80112233u, 0xffffffffu}) {
        auto settings        = fx.settings();
        settings.status.beak = c;
        auto p               = PixelFx::prepareConfiguration(PixelFxKind::Stunned, settings);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        REQUIRE(fx.settings().status.beak == c);
        const auto config = fx.configurationValue();
        auto       copy   = PixelFx::prepareConfigurationValue(PixelFxKind::Stunned, config);
        REQUIRE(copy);
    }
    for (const auto& bad : std::vector<eve::Value>{eve::Value(int64_t(-1)), eve::Value(int64_t(4294967296LL)),
                                                   eve::Value(1.5), eve::Value("blue")}) {
        auto config = original;
        config.find("options")->set("beak", bad);
        const auto previous = fx.configurationValue();
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Stunned, config));
        REQUIRE(fx.configurationValue() == previous);
    }
}

TEST_CASE("stylize.pixelfx.status.stunnedStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 3919117829u, 171253073u, 2069664508u},
        {1, 0.008333333333333333, 1214523003u, 3914140848u, 2982792841u},
        {1, 0.2, 1289711223u, 3563480399u, 1522143700u},
        {1, 0.4, 474563593u, 146673686u, 791210494u},
        {1, 0.5, 259145693u, 2808477816u, 3137931516u},
        {1, 0.9916666666666667, 2351820414u, 2306091385u, 775986275u},
        {1, 1, 3720912842u, 1473170272u, 1588892246u},
        {1, 1.5, 3859024003u, 2135389578u, 2942649639u},
        {1, 1.9916666666666667, 1827970656u, 3586007166u, 3982424191u},
        {1, 2, 3919117829u, 171253073u, 2069664508u},
        {1, 2.0083333333333333, 1214523003u, 3914140848u, 2982792841u},
        {1, 4, 3919117829u, 171253073u, 2069664508u},
        {2, 0, 2254855494u, 3054661593u, 4088677257u},
        {2, 0.008333333333333333, 2217839269u, 852061607u, 1933758541u},
        {2, 0.2, 542472534u, 2454367729u, 3540519330u},
        {2, 0.4, 169035047u, 922441356u, 1827879250u},
        {2, 0.5, 3969801954u, 3824771005u, 4049474766u},
        {2, 0.9916666666666667, 2495957112u, 4113724949u, 3980289617u},
        {2, 1, 3045555418u, 903678921u, 3891260666u},
        {2, 1.5, 3637228840u, 1628560551u, 753979899u},
        {2, 1.9916666666666667, 190443845u, 2823835230u, 2560673323u},
        {2, 2, 2254855494u, 3054661593u, 4088677257u},
        {2, 2.0083333333333333, 2217839269u, 852061607u, 1933758541u},
        {2, 4, 2254855494u, 3054661593u, 4088677257u},
    };
    PixelFx fx(PixelFxKind::Stunned);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Stunned, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.sleepStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 1633566430u, 1797368604u, 3794622779u},
        {1, 0.008333333333333333, 3775452115u, 1190099797u, 3063745163u},
        {1, 0.2, 1626147121u, 721825427u, 3644175294u},
        {1, 0.43333333333333335, 114640249u, 736169793u, 1408713723u},
        {1, 0.6, 893652163u, 1998651436u, 3504224661u},
        {1, 0.7916666666666666, 1786551139u, 3910322239u, 3365196028u},
        {1, 1, 234255902u, 1656773037u, 827163831u},
        {1, 1.2, 170837078u, 2496251598u, 3552262072u},
        {1, 1.8, 2802614912u, 860034636u, 276409281u},
        {1, 2.3916666666666666, 3356988487u, 3867881443u, 3541721581u},
        {1, 2.4, 1633566430u, 1797368604u, 3794622779u},
        {1, 4.8, 1633566430u, 1797368604u, 3794622779u},
        {2, 0, 245892795u, 2797770857u, 4054921533u},
        {2, 0.008333333333333333, 4158699925u, 3251552694u, 1739358571u},
        {2, 0.2, 3853583131u, 2528518826u, 1175667891u},
        {2, 0.43333333333333335, 3610799474u, 224540841u, 3925617359u},
        {2, 0.6, 1895619227u, 3782582824u, 3086121436u},
        {2, 0.7916666666666666, 1399239549u, 4229324633u, 3397621215u},
        {2, 1, 3414741726u, 2699906646u, 2396502363u},
        {2, 1.2, 698379233u, 2722959847u, 2792547783u},
        {2, 1.8, 3728152674u, 1182195398u, 72983470u},
        {2, 2.3916666666666666, 2474211736u, 1891651620u, 261642348u},
        {2, 2.4, 245892795u, 2797770857u, 4054921533u},
        {2, 4.8, 245892795u, 2797770857u, 4054921533u},
    };
    PixelFx fx(PixelFxKind::Sleep);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Sleep, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.confusedStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 1753249085u, 2089784533u, 779085899u},
        {1, 0.008333333333333333, 4207594242u, 3316650514u, 3797300650u},
        {1, 0.1, 2118924216u, 743858892u, 4280685545u},
        {1, 0.2, 1383890953u, 2922224273u, 1460592310u},
        {1, 0.5, 562997496u, 2685263071u, 2559409015u},
        {1, 0.7916666666666666, 4078460676u, 3980669830u, 2367755832u},
        {1, 0.8, 4414631u, 2194006106u, 2240077876u},
        {1, 0.8083333333333333, 58388852u, 3897446630u, 2509557266u},
        {1, 1.6, 442741363u, 3729820606u, 3177056577u},
        {1, 1.9, 1032901083u, 2674817989u, 2795286030u},
        {1, 2.3916666666666666, 519324347u, 2555258218u, 1546687442u},
        {1, 2.4, 1753249085u, 2089784533u, 779085899u},
        {2, 0, 3972073998u, 3640401281u, 3797373131u},
        {2, 0.008333333333333333, 4154322812u, 3973955627u, 1585274090u},
        {2, 0.1, 1612741359u, 1232583678u, 2770686313u},
        {2, 0.2, 1583573152u, 236274855u, 1823193974u},
        {2, 0.5, 187508131u, 1631781395u, 2279539758u},
        {2, 0.7916666666666666, 3701623061u, 706463943u, 3423139448u},
        {2, 0.8, 4047791118u, 263732147u, 2535271412u},
        {2, 0.8083333333333333, 3773340005u, 249923943u, 163063506u},
        {2, 1.6, 1879096486u, 2562975935u, 2658240449u},
        {2, 1.9, 3297206038u, 2117898711u, 685613390u},
        {2, 2.3916666666666666, 1711448456u, 715514618u, 3219230866u},
        {2, 2.4, 3972073998u, 3640401281u, 3797373131u},
    };
    PixelFx fx(PixelFxKind::Confused);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Confused, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.charmedStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 3683035597u, 2801096593u, 1790217770u},
        {1, 0.008333333333333333, 3216635988u, 3110118777u, 3016884066u},
        {1, 0.03333333333333333, 2819493134u, 3148948369u, 3446769030u},
        {1, 0.041666666666666664, 3489013526u, 2714473281u, 1148164705u},
        {1, 0.25, 1958343462u, 3853916087u, 3540703209u},
        {1, 0.25833333333333336, 1631050713u, 3692553098u, 378788684u},
        {1, 0.26666666666666666, 380676981u, 2170324923u, 4148543570u},
        {1, 0.5416666666666666, 9378761u, 2290812152u, 1915446592u},
        {1, 1, 34579334u, 1239598606u, 4185527687u},
        {1, 1.2, 749392319u, 1082116963u, 1818359561u},
        {1, 2.3916666666666666, 236552618u, 1078631193u, 3585304565u},
        {1, 2.4, 3683035597u, 2801096593u, 1790217770u},
        {2, 0, 2530273682u, 605660152u, 834675050u},
        {2, 0.008333333333333333, 3629242127u, 3885713808u, 1764421122u},
        {2, 0.03333333333333333, 961134301u, 2909286032u, 470948898u},
        {2, 0.041666666666666664, 270821701u, 535729120u, 379204957u},
        {2, 0.25, 1460797279u, 4168801618u, 434242005u},
        {2, 0.25833333333333336, 843577234u, 2381971815u, 2439174400u},
        {2, 0.26666666666666666, 2109901753u, 2354255217u, 3177813742u},
        {2, 0.5416666666666666, 3284784677u, 2711810799u, 3745579625u},
        {2, 1, 2331399091u, 3474273262u, 2603879130u},
        {2, 1.2, 3271897568u, 2797826248u, 2054186633u},
        {2, 2.3916666666666666, 619970685u, 2000559356u, 4187215605u},
        {2, 2.4, 2530273682u, 605660152u, 834675050u},
    };
    PixelFx fx(PixelFxKind::Charmed);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Charmed, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.cursedStaticStockBodyComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, alpha, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 3560422136u, 4139770144u, 4230092054u},
        {1, 0.008333333333333333, 2471559694u, 1358646509u, 1219474197u},
        {1, 0.49166666666666664, 2415914732u, 297664266u, 3338018533u},
        {1, 0.5, 1427470398u, 1172012708u, 462441485u},
        {1, 0.5083333333333333, 1398688250u, 4255551949u, 3374491903u},
        {1, 0.7416666666666667, 493191215u, 2869433882u, 421579704u},
        {1, 0.75, 1667576310u, 1638786626u, 2000843400u},
        {1, 0.7583333333333333, 1224600646u, 424989931u, 2783875260u},
        {1, 1.8416666666666666, 4080381597u, 154918723u, 2597474462u},
        {1, 1.85, 3352264402u, 220060663u, 2293152765u},
        {1, 2.091666666666667, 1891282188u, 1044552049u, 407097750u},
        {1, 2.1, 1008573015u, 2718065583u, 341949412u},
        {1, 2.3916666666666666, 4237410735u, 2564515694u, 3859007240u},
        {1, 2.4, 3560422136u, 4139770144u, 4230092054u},
        {2, 0, 2438385080u, 448242294u, 1932183833u},
        {2, 0.008333333333333333, 1360965168u, 364109610u, 1374996838u},
        {2, 0.49166666666666664, 3248780090u, 1621217577u, 1685329869u},
        {2, 0.5, 889637051u, 2314579690u, 172318516u},
        {2, 0.5083333333333333, 3384243196u, 1528850735u, 3037285608u},
        {2, 0.7416666666666667, 4243202081u, 2771356106u, 333284580u},
        {2, 0.75, 580661997u, 145768411u, 1308748829u},
        {2, 0.7583333333333333, 2660973679u, 2865198005u, 355490293u},
        {2, 1.8416666666666666, 695578934u, 1185190523u, 3576205220u},
        {2, 1.85, 41691512u, 989820602u, 4184808461u},
        {2, 2.091666666666667, 2310997288u, 1993242657u, 1980783272u},
        {2, 2.1, 199962878u, 780320826u, 2755501756u},
        {2, 2.3916666666666666, 1221910559u, 2519853122u, 226676373u},
        {2, 2.4, 2438385080u, 448242294u, 1932183833u},
    };
    PixelFx fx(PixelFxKind::Cursed);
    for (const auto& f : frames) {
        auto settings         = fx.settings();
        settings.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto config           = PixelFx::prepareConfiguration(PixelFxKind::Cursed, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(alphaChecksum(color) == f.alpha);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(color) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.declaredEyesAtSixtyFourAndHeightOneStayOwned") {
    for (auto kind : {PixelFxKind::Sleep, PixelFxKind::Confused, PixelFxKind::Charmed, PixelFxKind::Cursed}) {
        PixelFx fx(kind);
        auto    settings = fx.settings();
        auto&   a        = settings.actor;
        a.source         = PixelFxActorSource::Custom;
        a.width          = 1;
        a.height         = 1;
        a.scale          = 1;
        a.x              = 0;
        a.y              = -1;
        a.head           = {0, -1};
        a.chest          = {0, -.5};
        a.mask           = {1};
        a.pixels         = {0};
        a.eyes.assign(64, std::array<int, 2>{0, 0});
        auto prepared = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(prepared);
        a.eyes.clear();
        a.mask.clear();
        a.pixels.clear();
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        const auto owned = fx.settings().actor;
        auto       list  = pixelfx::status::eyes(owned, pixelfx::status::Shape(owned));
        REQUIRE(list.size() == 64);
        REQUIRE(std::all_of(list.begin(), list.end(), [](const auto& e) {
            return e.skin == 0u && e.y0 == 0 && e.y1 == 1 && e.pixels.size() == 2;
        }));
        fx.render(.3);
        const auto config = fx.configurationValue();
        const auto c = checksum(fx.color()), l = checksum(fx.light());
        auto       bad = fx.settings();
        bad.actor.eyes.push_back({0, 0});
        REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
        REQUIRE(fx.configurationValue() == config);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
        REQUIRE(fx.effectTime() == .3);
    }
}
}  // namespace test_pixelfx_status_unit

namespace test_pixelfx_status_unit {
TEST_CASE("stylize.pixelfx.status.bodyGeometryRulesArePreciseAndTransactional") {
    for (auto kind : {PixelFxKind::Burning, PixelFxKind::Poisoned}) {
        PixelFx fx(kind);
        fx.render(.3);
        const auto before = fx.configurationValue();
        const auto color = checksum(fx.color()), light = checksum(fx.light());
        auto       settings = fx.settings();
        auto&      a        = settings.actor;
        a.source            = PixelFxActorSource::Custom;
        a.scale             = 1;
        a.eyes.clear();
        a.head  = {0, -1};
        a.chest = {0, -.5};
        if (kind == PixelFxKind::Burning) {
            // No lower-body cells: source still accepts the near-mid-tone fallback.
            a.width  = 5;
            a.height = 33;
            a.x      = -2.5;
            a.y      = -33;
            a.mask.assign(165, 0);
            a.pixels.assign(165, 0x006b6b6bu);
            for (int x = 0; x < 5; ++x) a.mask[size_t(70 + x)] = 1;
        } else {
            // Smallest source-valid blister neighborhood, including raw-zero pixels.
            a.width  = 3;
            a.height = 6;
            a.x      = -1.5;
            a.y      = -6;
            a.mask.assign(18, 0);
            a.pixels.assign(18, 0);
            for (int i : {13, 15, 16, 17}) a.mask[size_t(i)] = 1;
        }
        auto valid = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(valid);
        if (kind == PixelFxKind::Burning)
            std::fill(a.pixels.begin(), a.pixels.end(), 0xffffffffu);
        else
            a.mask[13] = 0;
        REQUIRE(!PixelFx::prepareConfiguration(kind, settings));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(checksum(fx.color()) == color);
        REQUIRE(checksum(fx.light()) == light);
        REQUIRE(fx.effectTime() == .3);
        REQUIRE(fx.commitConfiguration(std::move(valid).takeValue()));
        fx.render(.3);
        REQUIRE(fx.settings().actor.mask != a.mask || fx.settings().actor.pixels != a.pixels);
        auto metadata = PixelFx::describeParameters(kind);
        REQUIRE(metadata);
        bool found = false;
        for (const auto& p : *metadata.value().find("parameters")->getIf<eve::Value::Array>()) {
            if (p.find("name")->asString() != "actor") continue;
            const auto* rule = p.find("constraints")->find("geometryRule");
            REQUIRE(rule);
            REQUIRE(rule->find("id")->asString() ==
                    (kind == PixelFxKind::Burning ? "status/burning-hotspots" : "status/poisoned-blisters"));
            found = true;
        }
        REQUIRE(found);
    }
    auto generic = PixelFx::describeParameters(PixelFxKind::Cursed);
    REQUIRE(generic);
    for (const auto& p : *generic.value().find("parameters")->getIf<eve::Value::Array>())
        if (p.find("name")->asString() == "actor") REQUIRE(!p.find("constraints")->find("geometryRule"));
}
}  // namespace test_pixelfx_status_unit

namespace test_pixelfx_status_unit {

TEST_CASE("stylize.pixelfx.status.burningPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact, visible;
    };
    constexpr Frame frames[] = {
        {0, 908009012u, 2568639342u, 3581257266u, true, false},
        {0.008333333333333333, 2747335317u, 824422033u, 3565769082u, true, false},
        {0.1, 97148236u, 580642826u, 3953674435u, true, true},
        {0.2, 3888358328u, 256937014u, 295622996u, true, true},
        {0.2916666666666667, 2071491544u, 2627795028u, 1954138073u, true, true},
        {0.3, 4195261183u, 3989935848u, 1504281171u, true, true},
        {0.30833333333333335, 2232850229u, 3823784699u, 1212049476u, true, true},
        {0.5916666666666667, 3942269834u, 4105937186u, 253578561u, true, true},
        {0.6, 3291980873u, 986575326u, 871867077u, true, false},
        {0.6083333333333333, 285093359u, 629908929u, 254585998u, true, false},
        {0.7916666666666666, 1707635786u, 3951869301u, 1652526773u, true, true},
        {1, 184003585u, 1063107002u, 1751416524u, true, true},
        {1.1916666666666667, 4183725923u, 2833513228u, 2847061677u, true, true},
        {1.2, 908009012u, 2568639342u, 3581257266u, true, false},
        {1.2083333333333333, 2747335317u, 824422033u, 3565769082u, true, false},
        {1.5, 3735237047u, 2446258215u, 1504281171u, true, true},
        {2, 1024144304u, 4030763607u, 2953136462u, true, true},
        {2.3916666666666666, 4183725923u, 2833513228u, 2847061677u, true, true},
        {2.4, 908009012u, 2568639342u, 3581257266u, true, false},
        {2.408333333333333, 2747335317u, 824422033u, 3565769082u, true, false},
        {3, 3291980873u, 986575326u, 871867077u, true, false},
        {4, 2398982656u, 1904130222u, 1865726604u, true, true},
        {4.091666666666667, 1451951532u, 2133210349u, 2515540862u, true, true},
        {4.116666666666666, 95796377u, 951681505u, 881664899u, true, true},
        {5, 3888358328u, 256937014u, 295622996u, true, true},
    };
    PixelFx fx(PixelFxKind::Burning);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.burningLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Burning);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.burningPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Burning), reference(PixelFxKind::Burning);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.burningOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Burning);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Burning, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Burning, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Burning);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Burning, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Burning, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Burning, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.poisonedPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact, visible;
    };
    constexpr Frame frames[] = {
        {0, 92170831u, 1837307926u, 3765959629u, true, false},
        {0.008333333333333333, 414923259u, 1000281615u, 3121361323u, true, false},
        {0.1, 3253814275u, 750940936u, 3043025919u, true, false},
        {0.2, 543005698u, 2464525800u, 1014904592u, true, false},
        {0.2916666666666667, 2596200521u, 3003582871u, 3327196224u, true, false},
        {0.3, 4069949681u, 171916831u, 3657532622u, true, false},
        {0.30833333333333335, 400973188u, 670763977u, 2176748952u, true, false},
        {0.5916666666666667, 449009526u, 1250251649u, 502169465u, true, false},
        {0.6, 3286046407u, 592194394u, 860748856u, true, false},
        {0.6083333333333333, 3122089704u, 712392120u, 336811161u, true, false},
        {0.7916666666666666, 1428978494u, 4077284687u, 4192444016u, true, false},
        {1, 3671766421u, 1031148216u, 3015057982u, true, false},
        {1.1916666666666667, 1752990784u, 3194559325u, 291906339u, true, false},
        {1.2, 1890207157u, 2290986404u, 2565236845u, true, false},
        {1.2083333333333333, 1293773407u, 559336006u, 4124805027u, true, false},
        {1.5, 3868079199u, 2498869046u, 4164125467u, true, false},
        {2, 791140920u, 3595266145u, 3840596317u, true, false},
        {2.3916666666666666, 1428978494u, 4077284687u, 4192444016u, true, false},
        {2.4, 3930089735u, 4046097908u, 535502234u, true, false},
        {2.408333333333333, 801280882u, 1000281615u, 670423643u, true, false},
        {3, 2562806717u, 1580615722u, 860748856u, true, false},
        {4, 3930089735u, 4046097908u, 535502234u, true, false},
        {4.091666666666667, 2392068796u, 1856319028u, 89554710u, true, false},
        {4.116666666666666, 1009889559u, 3922692392u, 125418968u, true, false},
        {5, 543005698u, 2464525800u, 1014904592u, true, false},
    };
    PixelFx fx(PixelFxKind::Poisoned);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.poisonedLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Poisoned);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.poisonedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Poisoned), reference(PixelFxKind::Poisoned);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.poisonedOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Poisoned);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Poisoned, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Poisoned, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Poisoned);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Poisoned, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Poisoned, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Poisoned, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
TEST_CASE("stylize.pixelfx.status.ragePinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact, visible;
    };
    constexpr Frame frames[] = {
        {0, 1733033545u, 2931878162u, 1634637784u, true, true},
        {0.008333333333333333, 1829172022u, 1745779241u, 369299296u, true, true},
        {0.1, 791562565u, 1316614054u, 3062835030u, true, false},
        {0.2, 3460022355u, 1630850079u, 1665389177u, true, true},
        {0.2916666666666667, 309366053u, 1795855757u, 961940953u, true, false},
        {0.3, 4076062661u, 2391657205u, 4137383436u, true, false},
        {0.30833333333333335, 1990752089u, 4017356953u, 2984644976u, true, false},
        {0.5916666666666667, 3827847544u, 3155386823u, 960102198u, true, true},
        {0.6, 3988166711u, 3135362821u, 917481393u, true, true},
        {0.6083333333333333, 505622947u, 180436349u, 1866973667u, true, true},
        {0.7916666666666666, 157307204u, 3867778082u, 1245624337u, true, true},
        {1, 3462403529u, 315746480u, 2384510058u, true, true},
        {1.1916666666666667, 1279904886u, 1479115653u, 362273916u, true, true},
        {1.2, 1733033545u, 2931878162u, 1634637784u, true, true},
        {1.2083333333333333, 1829172022u, 1745779241u, 369299296u, true, true},
        {1.5, 4076062661u, 2391657205u, 4137383436u, true, false},
        {2, 1168092945u, 77997386u, 2725434179u, true, true},
        {2.3916666666666666, 1279904886u, 1479115653u, 362273916u, true, true},
        {2.4, 1733033545u, 2931878162u, 1634637784u, true, true},
        {2.408333333333333, 1829172022u, 1745779241u, 369299296u, true, true},
        {3, 2905805195u, 3135362821u, 917481393u, true, true},
        {4, 1899972208u, 4026126066u, 3070685854u, true, true},
        {4.091666666666667, 4135699161u, 1855285160u, 3068800072u, true, true},
        {4.116666666666666, 3933596517u, 4094608353u, 3030513292u, true, true},
        {5, 3460022355u, 1630850079u, 1665389177u, true, true},
    };
    PixelFx fx(PixelFxKind::Rage);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.rageLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Rage);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.ragePartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Rage), reference(PixelFxKind::Rage);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.rageOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Rage);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Rage, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Rage, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Rage);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Rage, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Rage, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Rage, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
}  // namespace test_pixelfx_status_unit

namespace test_pixelfx_status_unit {
TEST_CASE("stylize.pixelfx.status.petrifiedPinnedCreepCrackShatterAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
        bool     visible;
    };
    {
        constexpr Frame frames[] = {{0, 3023772441u, 625588349u, true},
                                    {0.008333333333333333, 3246071661u, 625588349u, true},
                                    {0.2, 13354555u, 3569469093u, true},
                                    {0.6, 631213694u, 3928905509u, true},
                                    {0.9416666666666667, 1271897895u, 307694789u, true},
                                    {0.95, 2250980367u, 307694789u, false},
                                    {0.9583333333333334, 1880044801u, 2980867149u, false},
                                    {1.15, 3936522735u, 631522525u, false},
                                    {1.1583333333333334, 3261261613u, 3277901093u, false},
                                    {1.7416666666666667, 4128413634u, 307694789u, false},
                                    {1.75, 2004657093u, 307694789u, false},
                                    {1.7583333333333333, 3297555919u, 2090659685u, false},
                                    {2.15, 4168462148u, 1599815125u, false},
                                    {2.158333333333333, 1325139019u, 1599815125u, false},
                                    {2.1666666666666665, 2455957006u, 1599815125u, false},
                                    {2.191666666666667, 1987762400u, 1599815125u, false},
                                    {2.2, 107228728u, 1599815125u, false},
                                    {2.2083333333333335, 190550876u, 4268580869u, false},
                                    {2.5, 1985615221u, 3913088463u, false},
                                    {3, 3956110558u, 2997153795u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 3138678184u, 100133573u, true},
                                    {0.008333333333333333, 3138678184u, 1471250381u, true},
                                    {0.2, 1587894939u, 2536580069u, true},
                                    {0.6, 3720295054u, 2260933013u, true},
                                    {0.9416666666666667, 2490202391u, 307694789u, true},
                                    {0.95, 2490202391u, 307694789u, false},
                                    {0.9583333333333334, 1850930682u, 2652776701u, false},
                                    {1.15, 3567859343u, 2198582573u, false},
                                    {1.1583333333333334, 4176750750u, 1700990405u, false},
                                    {1.7416666666666667, 4284936007u, 307694789u, false},
                                    {1.75, 1356206614u, 307694789u, false},
                                    {1.7583333333333333, 3863082910u, 1090688517u, false},
                                    {2.15, 2578238100u, 3509235573u, false},
                                    {2.158333333333333, 3673007359u, 3509235573u, false},
                                    {2.1666666666666665, 140661706u, 3509235573u, false},
                                    {2.191666666666667, 3334077186u, 3509235573u, false},
                                    {2.2, 208490724u, 3509235573u, false},
                                    {2.2083333333333335, 586738840u, 3497157413u, false},
                                    {2.5, 450225836u, 2777721964u, false},
                                    {3, 3422389507u, 1575226096u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        value.find("options")->set("seed", eve::Value::fromJson(R"json(123)json").takeValue());
        value.find("options")->set(
            "palette", eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json")
                           .takeValue());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 3432721995u, 3204344473u, true},
                                    {0.008333333333333333, 3432721995u, 1321634281u, true},
                                    {0.2, 2071382371u, 2979726767u, true},
                                    {0.6, 3494355740u, 1955119069u, true},
                                    {0.9416666666666667, 3006970751u, 307694789u, true},
                                    {0.95, 3835107983u, 307694789u, false},
                                    {0.9583333333333334, 2335674531u, 2980867149u, false},
                                    {1.15, 4287736404u, 631522525u, false},
                                    {1.1583333333333334, 1742729550u, 3277901093u, false},
                                    {1.7416666666666667, 3536558682u, 307694789u, false},
                                    {1.75, 571869116u, 307694789u, false},
                                    {1.7583333333333333, 790399502u, 2090659685u, false},
                                    {2.15, 3437093195u, 1599815125u, false},
                                    {2.158333333333333, 4127353903u, 1599815125u, false},
                                    {2.1666666666666665, 2823998089u, 1599815125u, false},
                                    {2.191666666666667, 826538143u, 1599815125u, false},
                                    {2.2, 2798429784u, 1599815125u, false},
                                    {2.2083333333333335, 3364302276u, 4268580869u, false},
                                    {2.5, 256865747u, 3555775022u, false},
                                    {3, 891132782u, 307694789u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        value.find("options")->set("seed", eve::Value::fromJson(R"json(0)json").takeValue());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 1917053190u, 1567058981u, true},
                                    {0.008333333333333333, 1917053190u, 1567058981u, true},
                                    {0.2, 559604777u, 531657334u, true},
                                    {0.6, 3963119089u, 2103741997u, true},
                                    {0.9416666666666667, 453654070u, 307694789u, true},
                                    {0.95, 453654070u, 307694789u, false},
                                    {0.9583333333333334, 2721977506u, 2980867149u, false},
                                    {1.15, 3072206590u, 631522525u, false},
                                    {1.1583333333333334, 892938211u, 3277901093u, false},
                                    {1.7416666666666667, 1824465614u, 307694789u, false},
                                    {1.75, 2050320400u, 307694789u, false},
                                    {1.7583333333333333, 3041537645u, 2090659685u, false},
                                    {2.15, 3547336847u, 1599815125u, false},
                                    {2.158333333333333, 1961037015u, 1599815125u, false},
                                    {2.1666666666666665, 1193133541u, 1599815125u, false},
                                    {2.191666666666667, 3900509605u, 1599815125u, false},
                                    {2.2, 222992853u, 1599815125u, false},
                                    {2.2083333333333335, 1662866100u, 4268580869u, false},
                                    {2.5, 113131354u, 2411700486u, false},
                                    {3, 3490774219u, 3685708228u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        value.find("options")->set("seed", eve::Value::fromJson(R"json(4294967295)json").takeValue());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 160175229u, 4264224245u, true},
                                    {0.008333333333333333, 2365525941u, 4264224245u, true},
                                    {0.2, 3169082781u, 3763934925u, true},
                                    {0.6, 4288929658u, 3529170069u, true},
                                    {0.9416666666666667, 374045773u, 307694789u, true},
                                    {0.95, 1490231317u, 307694789u, false},
                                    {0.9583333333333334, 556462971u, 3218993733u, false},
                                    {1.15, 877641051u, 2307794437u, false},
                                    {1.1583333333333334, 607639361u, 2195171605u, false},
                                    {1.7416666666666667, 1674656282u, 307694789u, false},
                                    {1.75, 2149497549u, 307694789u, false},
                                    {1.7583333333333333, 414240871u, 2771955653u, false},
                                    {2.15, 3884771665u, 1098365477u, false},
                                    {2.158333333333333, 3884771665u, 1098365477u, false},
                                    {2.1666666666666665, 3930609297u, 1098365477u, false},
                                    {2.191666666666667, 449007277u, 1098365477u, false},
                                    {2.2, 3930609297u, 1098365477u, false},
                                    {2.2083333333333335, 4247931930u, 337139045u, false},
                                    {2.5, 2708150163u, 1018237853u, false},
                                    {3, 1968740264u, 3222879358u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        value.find("options")->set("palette", eve::Value::fromJson(R"json([4294967295])json").takeValue());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 265050901u, 2750992749u, true},
                                    {0.008333333333333333, 2256932517u, 2750992749u, true},
                                    {0.2, 2453110217u, 252138941u, true},
                                    {0.6, 152259011u, 2653419753u, true},
                                    {0.9416666666666667, 1536012368u, 307694789u, true},
                                    {0.95, 2176709220u, 307694789u, false},
                                    {0.9583333333333334, 699641490u, 2113606525u, false},
                                    {1.15, 2230341255u, 4001309085u, false},
                                    {1.1583333333333334, 817675269u, 1851383421u, false},
                                    {1.7416666666666667, 3480839186u, 307694789u, false},
                                    {1.75, 1884010881u, 307694789u, false},
                                    {1.7583333333333333, 3060537947u, 530889637u, false},
                                    {2.15, 4129809015u, 2092825461u, false},
                                    {2.158333333333333, 1122183002u, 2092825461u, false},
                                    {2.1666666666666665, 1973730812u, 2092825461u, false},
                                    {2.191666666666667, 3778189003u, 2092825461u, false},
                                    {2.2, 1712238251u, 2092825461u, false},
                                    {2.2083333333333335, 2055463470u, 2956416953u, false},
                                    {2.5, 1184546473u, 2214951487u, false},
                                    {3, 2610179556u, 2483993139u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        value.find("options")->set(
            "palette",
            eve::Value::fromJson(
                R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json")
                .takeValue());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
}
TEST_CASE("stylize.pixelfx.status.petrifiedPinnedActorSnapshotsAndInputOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
        bool     visible;
    };
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 307694789u, true},
                                    {0.2, 307694789u, 3976285269u, true},
                                    {0.6, 1385283263u, 1991824839u, true},
                                    {0.9416666666666667, 3602183723u, 307694789u, true},
                                    {0.95, 910682108u, 307694789u, false},
                                    {0.9583333333333334, 835839935u, 2873185905u, false},
                                    {1.15, 2203509278u, 484315769u, false},
                                    {1.1583333333333334, 3611604362u, 416154057u, false},
                                    {1.7416666666666667, 1140905274u, 307694789u, false},
                                    {1.75, 3651146410u, 307694789u, false},
                                    {1.7583333333333333, 1758632969u, 2357110117u, false},
                                    {2.15, 4056777501u, 325653973u, false},
                                    {2.158333333333333, 4056777501u, 325653973u, false},
                                    {2.1666666666666665, 3797026445u, 325653973u, false},
                                    {2.191666666666667, 2873730249u, 325653973u, false},
                                    {2.2, 3797026445u, 325653973u, false},
                                    {2.2083333333333335, 1926069324u, 3461649729u, false},
                                    {2.5, 3794024564u, 4256618600u, false},
                                    {3, 1010353677u, 3770984957u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [], "mask": [1], "pixels": [0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 2163299499u, 1843811613u, true},
                                    {0.008333333333333333, 2163299499u, 2175184301u, true},
                                    {0.2, 1192101239u, 3456573425u, true},
                                    {0.6, 2006789569u, 4068157405u, true},
                                    {0.9416666666666667, 3463214557u, 307694789u, true},
                                    {0.95, 3463214557u, 307694789u, false},
                                    {0.9583333333333334, 279024711u, 1153913297u, false},
                                    {1.15, 1514161679u, 2697274673u, false},
                                    {1.1583333333333334, 480794829u, 4038139049u, false},
                                    {1.7416666666666667, 2728504952u, 307694789u, false},
                                    {1.75, 282232102u, 307694789u, false},
                                    {1.7583333333333333, 2292938873u, 3714090341u, false},
                                    {2.15, 2578841849u, 1390429653u, false},
                                    {2.158333333333333, 2680125045u, 1390429653u, false},
                                    {2.1666666666666665, 503176764u, 1390429653u, false},
                                    {2.191666666666667, 1894692064u, 1390429653u, false},
                                    {2.2, 3164566724u, 1390429653u, false},
                                    {2.2083333333333335, 1389728663u, 2550548257u, false},
                                    {2.5, 3894048438u, 1764966554u, false},
                                    {3, 2948506413u, 3664959268u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 307694789u, true},
                                    {0.2, 1228035887u, 1218472925u, true},
                                    {0.6, 2222996639u, 4224283809u, true},
                                    {0.9416666666666667, 1104211350u, 307694789u, true},
                                    {0.95, 1104211350u, 307694789u, false},
                                    {0.9583333333333334, 1044822750u, 1461059225u, false},
                                    {1.15, 363528071u, 877334609u, false},
                                    {1.1583333333333334, 2844175184u, 1732004873u, false},
                                    {1.7416666666666667, 3000672043u, 307694789u, false},
                                    {1.75, 992222625u, 307694789u, false},
                                    {1.7583333333333333, 3880429480u, 3958684845u, false},
                                    {2.15, 427573063u, 3841019425u, false},
                                    {2.158333333333333, 3607558087u, 3841019425u, false},
                                    {2.1666666666666665, 1517504955u, 3841019425u, false},
                                    {2.191666666666667, 1769912893u, 3841019425u, false},
                                    {2.2, 687608153u, 3841019425u, false},
                                    {2.2083333333333335, 3885431070u, 532122749u, false},
                                    {2.5, 2884287115u, 3529498266u, false},
                                    {3, 291705974u, 3047393812u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 1482865809u, true},
                                    {0.2, 307694789u, 4136281408u, true},
                                    {0.6, 3367382735u, 3108525408u, true},
                                    {0.9416666666666667, 42186192u, 307694789u, true},
                                    {0.95, 42186192u, 307694789u, false},
                                    {0.9583333333333334, 3144216179u, 4008597553u, false},
                                    {1.15, 737978788u, 3316961441u, false},
                                    {1.1583333333333334, 2856327547u, 3140626537u, false},
                                    {1.7416666666666667, 4036518596u, 307694789u, false},
                                    {1.75, 3305640794u, 307694789u, false},
                                    {1.7583333333333333, 518401080u, 539573729u, false},
                                    {2.15, 140202072u, 541544441u, false},
                                    {2.158333333333333, 1925008583u, 541544441u, false},
                                    {2.1666666666666665, 1767243579u, 541544441u, false},
                                    {2.191666666666667, 1404689375u, 541544441u, false},
                                    {2.2, 1767243579u, 541544441u, false},
                                    {2.2083333333333335, 3469846335u, 428195061u, false},
                                    {2.5, 4064919923u, 3080453510u, false},
                                    {3, 3728357625u, 307694789u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 307694789u, true},
                                    {0.2, 307694789u, 307694789u, true},
                                    {0.6, 1253473311u, 108711570u, true},
                                    {0.9416666666666667, 1595097205u, 307694789u, true},
                                    {0.95, 566379045u, 307694789u, false},
                                    {0.9583333333333334, 2478950832u, 4009789561u, false},
                                    {1.15, 149039394u, 3219395889u, false},
                                    {1.1583333333333334, 161180522u, 1564574377u, false},
                                    {1.7416666666666667, 2349479929u, 307694789u, false},
                                    {1.75, 2739979545u, 307694789u, false},
                                    {1.7583333333333333, 4209590595u, 1464225093u, false},
                                    {2.15, 2675271395u, 181951393u, false},
                                    {2.158333333333333, 3462851779u, 181951393u, false},
                                    {2.1666666666666665, 3138491344u, 181951393u, false},
                                    {2.191666666666667, 4099009406u, 181951393u, false},
                                    {2.2, 2052190u, 181951393u, false},
                                    {2.2083333333333335, 59679397u, 1564865025u, false},
                                    {2.5, 3594736498u, 2269181701u, false},
                                    {3, 3898772260u, 2991335012u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 33, "scale": 1, "x": -2.5, "y": -33, "head": [0, -33], "chest": [0, -16.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4285229931, 4285229931, 4285229931, 4285229931, 4285229931, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 307694789u, true},
                                    {0.2, 307694789u, 307694789u, true},
                                    {0.6, 1253473311u, 108711570u, true},
                                    {0.9416666666666667, 1595097205u, 307694789u, true},
                                    {0.95, 566379045u, 307694789u, false},
                                    {0.9583333333333334, 2478950832u, 4009789561u, false},
                                    {1.15, 149039394u, 3219395889u, false},
                                    {1.1583333333333334, 161180522u, 1564574377u, false},
                                    {1.7416666666666667, 2349479929u, 307694789u, false},
                                    {1.75, 2739979545u, 307694789u, false},
                                    {1.7583333333333333, 4209590595u, 1464225093u, false},
                                    {2.15, 2675271395u, 181951393u, false},
                                    {2.158333333333333, 3462851779u, 181951393u, false},
                                    {2.1666666666666665, 3138491344u, 181951393u, false},
                                    {2.191666666666667, 4099009406u, 181951393u, false},
                                    {2.2, 2052190u, 181951393u, false},
                                    {2.2083333333333335, 59679397u, 1564865025u, false},
                                    {2.5, 3594736498u, 2269181701u, false},
                                    {3, 3898772260u, 2991335012u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 33, "scale": 1, "x": -2.5, "y": -33, "head": [0, -33], "chest": [0, -16.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7039851, 7039851, 7039851, 7039851, 7039851, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
    {
        constexpr Frame frames[] = {{0, 307694789u, 307694789u, true},
                                    {0.008333333333333333, 307694789u, 307694789u, true},
                                    {0.2, 307694789u, 4091295881u, true},
                                    {0.6, 1633839194u, 307694789u, true},
                                    {0.9416666666666667, 2181462035u, 307694789u, true},
                                    {0.95, 2181462035u, 307694789u, false},
                                    {0.9583333333333334, 3142509966u, 188099301u, false},
                                    {1.15, 4279151637u, 2836765857u, false},
                                    {1.1583333333333334, 3313606446u, 6479845u, false},
                                    {1.7416666666666667, 2082420187u, 307694789u, false},
                                    {1.75, 3998269718u, 307694789u, false},
                                    {1.7583333333333333, 1113573475u, 1591135557u, false},
                                    {2.15, 448033891u, 82884513u, false},
                                    {2.158333333333333, 23105027u, 82884513u, false},
                                    {2.1666666666666665, 1327042479u, 82884513u, false},
                                    {2.191666666666667, 2004640655u, 82884513u, false},
                                    {2.2, 1425572407u, 82884513u, false},
                                    {2.2083333333333335, 902075911u, 2028475981u, false},
                                    {2.5, 705713412u, 2360570242u, false},
                                    {3, 3132521448u, 1313910952u, false},
                                    {3.7916666666666665, 307694789u, 307694789u, false},
                                    {3.8, 307694789u, 307694789u, false},
                                    {4, 307694789u, 307694789u, false},
                                    {5, 307694789u, 307694789u, false}};
        PixelFx         fx(PixelFxKind::Petrified);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 2, "scale": 1, "x": -0.5, "y": -2, "head": [0, -2], "chest": [0, -1.0], "eyes": [], "mask": [0, 1], "pixels": [0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() ==
                    (f.visible ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
        }
    }
}
TEST_CASE("stylize.pixelfx.status.petrifiedReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Petrified);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {2.21, 5., .949, 1.75, 2.2, .1, 2.21, 0., 2.21}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == 2.21) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.petrifiedPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Petrified), reference(PixelFxKind::Petrified);
        double  elapsed = 0;
        for (double target : {.13, .949, .95, .951, 1.75, 2.2, 2.21, 3.8, 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(live.done() == (live.effectTime() >= 3.8));
        }
        auto seek = live.prepareSeek(2.3);
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        seek = live.prepareSeek(2.3);
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(std::any_of(live.color().begin(), live.color().end(), [](auto p) { return p != 0; }));
    }
}
TEST_CASE("stylize.pixelfx.status.petrifiedActorBoundsRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Petrified);
    auto    seek = fx.prepareSeek(2.3);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto config = fx.configurationValue();
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    auto       rt = PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, config);
    REQUIRE(rt);
    PixelFx other(PixelFxKind::Petrified);
    REQUIRE(other.commitConfiguration(std::move(rt).takeValue()));
    seek = other.prepareSeek(2.3);
    REQUIRE(seek);
    REQUIRE(other.commitSeek(std::move(seek).takeValue()));
    other.draw();
    REQUIRE(checksum(other.color()) == c);
    REQUIRE(checksum(other.light()) == l);
    auto bad         = fx.settings();
    bad.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Petrified, bad));
    bad              = fx.settings();
    bad.actor.source = PixelFxActorSource::Custom;
    std::fill(bad.actor.mask.begin(), bad.actor.mask.end(), uint8_t(0));
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Petrified, bad));
    bad = fx.settings();
    bad.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Petrified, bad));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Petrified, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == c);
    REQUIRE(checksum(fx.light()) == l);
    REQUIRE(fx.effectTime() == 2.3);
}
TEST_CASE("stylize.pixelfx.status.petrifiedPinnedStaticStockComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {1, 0, 127666557u, 1843811613u},
        {1, 0.008333333333333333, 127666557u, 2175184301u},
        {1, 0.2, 3495063115u, 3456573425u},
        {1, 0.6, 17451885u, 4068157405u},
        {1, 0.9416666666666667, 3463214557u, 307694789u},
        {1, 0.95, 3463214557u, 307694789u},
        {1, 0.9583333333333334, 279024711u, 1153913297u},
        {1, 1.15, 1514161679u, 2697274673u},
        {1, 1.1583333333333334, 480794829u, 4038139049u},
        {1, 1.7416666666666667, 2728504952u, 307694789u},
        {1, 1.75, 282232102u, 307694789u},
        {1, 1.7583333333333333, 2292938873u, 3714090341u},
        {1, 2.15, 2578841849u, 1390429653u},
        {1, 2.158333333333333, 2680125045u, 1390429653u},
        {1, 2.1666666666666665, 503176764u, 1390429653u},
        {1, 2.191666666666667, 1894692064u, 1390429653u},
        {1, 2.2, 3164566724u, 1390429653u},
        {1, 2.2083333333333335, 1389728663u, 2550548257u},
        {1, 2.5, 3894048438u, 1764966554u},
        {1, 3, 2948506413u, 3664959268u},
        {1, 3.7916666666666665, 307694789u, 307694789u},
        {1, 3.8, 307694789u, 307694789u},
        {1, 4, 307694789u, 307694789u},
        {1, 5, 307694789u, 307694789u},
        {2, 0, 659177947u, 625588349u},
        {2, 0.008333333333333333, 1206389935u, 625588349u},
        {2, 0.2, 3053902079u, 3569469093u},
        {2, 0.6, 2521086092u, 3928905509u},
        {2, 0.9416666666666667, 1271897895u, 307694789u},
        {2, 0.95, 2250980367u, 307694789u},
        {2, 0.9583333333333334, 1880044801u, 2980867149u},
        {2, 1.15, 3936522735u, 631522525u},
        {2, 1.1583333333333334, 3261261613u, 3277901093u},
        {2, 1.7416666666666667, 4128413634u, 307694789u},
        {2, 1.75, 2004657093u, 307694789u},
        {2, 1.7583333333333333, 3297555919u, 2090659685u},
        {2, 2.15, 4168462148u, 1599815125u},
        {2, 2.158333333333333, 1325139019u, 1599815125u},
        {2, 2.1666666666666665, 2455957006u, 1599815125u},
        {2, 2.191666666666667, 1987762400u, 1599815125u},
        {2, 2.2, 107228728u, 1599815125u},
        {2, 2.2083333333333335, 190550876u, 4268580869u},
        {2, 2.5, 1985615221u, 3913088463u},
        {2, 3, 3956110558u, 2997153795u},
        {2, 3.7916666666666665, 307694789u, 307694789u},
        {2, 3.8, 307694789u, 307694789u},
        {2, 4, 307694789u, 307694789u},
        {2, 5, 307694789u, 307694789u},
    };
    PixelFx fx(PixelFxKind::Petrified);
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Petrified, s);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> c(size_t(info.width) * size_t(info.height)), l(c.size()), body(c.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (fx.actorVisibility() == PixelFxActorVisibility::Visible) {
            REQUIRE(fx.drawActor(body));
            for (size_t i = 0; i < body.size(); ++i)
                if (body[i]) c[i] = body[i];
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(c) == f.color);
        REQUIRE(checksum(l) == f.light);
    }
}
}  // namespace test_pixelfx_status_unit


namespace test_pixelfx_status_unit {

TEST_CASE("stylize.pixelfx.status.frozenPinnedBodyLayersAndMultipleLoops") {
    struct Frame {
        double   time;
        uint32_t color, alpha, light;
        bool     exact;
    };
    constexpr Frame frames[] = {
        {0, 3450256781u, 444189331u, 3169862153u, true},
        {0.008333333333333333, 3205968534u, 4145540293u, 2886775897u, true},
        {0.1, 3831979486u, 3537980890u, 92548945u, true},
        {0.2, 3385916007u, 2714594275u, 3914139301u, true},
        {0.2916666666666667, 2258571773u, 122707677u, 2510753671u, true},
        {0.3, 3913734221u, 544715162u, 2213845519u, true},
        {0.30833333333333335, 3599850990u, 2402181400u, 904065007u, true},
        {0.5916666666666667, 1339263046u, 3661113266u, 3459781u, true},
        {0.6, 607367618u, 3538044891u, 355412023u, true},
        {0.6083333333333333, 4202610284u, 89051395u, 248910063u, true},
        {0.7916666666666666, 574460904u, 251637300u, 856933287u, true},
        {1, 3618253642u, 3130595219u, 1461308919u, true},
        {1.1916666666666667, 2635832392u, 775237350u, 3206601695u, true},
        {1.2, 2058557363u, 1107446471u, 2413345493u, true},
        {1.2083333333333333, 742727016u, 3066289970u, 4248406493u, true},
        {1.5, 3184556314u, 3920259979u, 1825979967u, true},
        {2, 3450256781u, 444189331u, 3169862153u, true},
        {2.3916666666666666, 331775984u, 4157326066u, 1506621131u, true},
        {2.4, 1574594792u, 1043180092u, 1109433753u, true},
        {2.408333333333333, 1607095101u, 3572776978u, 1158707561u, true},
        {3, 3618253642u, 3130595219u, 1461308919u, true},
        {4, 3450256781u, 444189331u, 3169862153u, true},
        {4.091666666666667, 3284964850u, 283315122u, 1706006593u, true},
        {4.116666666666666, 3367236873u, 311896055u, 3665548251u, true},
        {5, 3618253642u, 3130595219u, 1461308919u, true},
    };
    PixelFx fx(PixelFxKind::Frozen);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        if (f.exact) REQUIRE(checksum(fx.color()) == f.color);
    }
}
TEST_CASE("stylize.pixelfx.status.frozenLayersReverseAndDrawPurity") {
    PixelFx  fx(PixelFxKind::Frozen);
    uint32_t restoredColor = 0, restoredLight = 0;
    for (double t : {.3, 1., 2.4, 4.116666666666666, .3, 0., .3}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                              l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        std::vector<uint32_t> colors(c.size()), lights(l.size());
        const auto            info = fx.info();
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, colors, lights};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(colors == c);
        REQUIRE(lights == l);
        fx.draw();
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        REQUIRE(fx.effectTime() == t);
        if (t == .3) {
            if (restoredColor) {
                REQUIRE(checksum(fx.color()) == restoredColor);
                REQUIRE(checksum(fx.light()) == restoredLight);
            } else {
                restoredColor = checksum(fx.color());
                restoredLight = checksum(fx.light());
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.status.frozenPartitionedClockAndStopRestart") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Frozen), reference(PixelFxKind::Frozen);
        double  elapsed = 0;
        for (double target : {.13, .3, .599, .6, 1., 1.2, 1.3, 2.4, 2.5, 4., 5.}) {
            while (elapsed < target) {
                const auto dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
            }
            auto seek = reference.prepareSeek(live.effectTime());
            REQUIRE(seek);
            REQUIRE(reference.commitSeek(std::move(seek).takeValue()));
            reference.draw();
            live.draw();
            REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
            REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
            REQUIRE(!live.done());
        }
        const auto c = checksum(live.color()), l = checksum(live.light());
        live.stop();
        REQUIRE(live.advance(.31));
        live.draw();
        REQUIRE(live.done());
        REQUIRE(std::all_of(live.color().begin(), live.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(live.light().begin(), live.light().end(), [](auto p) { return p == 0; }));
        auto seek = live.prepareSeek(reference.effectTime());
        REQUIRE(seek);
        REQUIRE(live.commitSeek(std::move(seek).takeValue()));
        live.draw();
        REQUIRE(!live.done());
        REQUIRE(checksum(live.color()) == c);
        REQUIRE(checksum(live.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.status.frozenOwningActorRoundTripAndAtomicRejection") {
    PixelFx fx(PixelFxKind::Frozen);
    auto    s      = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.seed         = 123;
    s.palette      = {0xff123456u, 0xffabcdefu};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Frozen, s);
    REQUIRE(candidate);
    s.actor.mask.assign(s.actor.mask.size(), 0);
    s.actor.pixels.assign(s.actor.pixels.size(), 0);
    s.palette.assign(2, 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.3);
    const auto config = fx.configurationValue();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    auto       copy = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, config);
    REQUIRE(copy);
    PixelFx other(PixelFxKind::Frozen);
    REQUIRE(other.commitConfiguration(std::move(copy).takeValue()));
    other.render(.3);
    REQUIRE(checksum(other.color()) == color);
    REQUIRE(checksum(other.light()) == light);
    auto invalid         = fx.settings();
    invalid.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Frozen, invalid));
    invalid = fx.settings();
    invalid.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Frozen, invalid));
    auto unknown = config;
    unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, unknown));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
    REQUIRE(fx.effectTime() == .3);
}
}  // namespace test_pixelfx_status_unit

namespace test_pixelfx_status_unit {
TEST_CASE("stylize.pixelfx.status.frozenPinnedSparseEmptyRawAndDeclaredEyeActors") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {
            {0, 713202869u, 3214666255u},
            {0.008333333333333333, 2850316550u, 1608136549u},
            {0.21666666666666667, 4240343977u, 2611392073u},
            {0.225, 256550689u, 3598430217u},
            {0.35, 1007715611u, 2574035437u},
            {0.35833333333333334, 2979581810u, 1795797761u},
            {0.5, 2435814948u, 1166489729u},
            {0.7, 1330050907u, 201464425u},
            {0.8583333333333333, 710128172u, 3014664161u},
            {0.9916666666666667, 3799767110u, 2165937033u},
            {1, 4201125550u, 3214666255u},
            {1.0083333333333333, 2413432689u, 1608136549u},
            {1.9916666666666667, 2882561717u, 2165937033u},
            {2, 713202869u, 3214666255u},
            {2.0083333333333333, 2850316550u, 1608136549u},
            {4, 713202869u, 3214666255u},
            {5, 4201125550u, 3214666255u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2020536826u, 2954324759u},
            {0.008333333333333333, 571127178u, 3814468661u},
            {0.21666666666666667, 82074761u, 2729473277u},
            {0.225, 4121207301u, 948219849u},
            {0.35, 1343104273u, 1959631649u},
            {0.35833333333333334, 4094713124u, 1931947913u},
            {0.5, 317334318u, 2200069185u},
            {0.7, 1078916392u, 2631914541u},
            {0.8583333333333333, 2453786017u, 1865059453u},
            {0.9916666666666667, 679898589u, 2515568049u},
            {1, 2463803361u, 2954324759u},
            {1.0083333333333333, 1010286945u, 3814468661u},
            {1.9916666666666667, 1292720272u, 2515568049u},
            {2, 2020536826u, 2954324759u},
            {2.0083333333333333, 571127178u, 3814468661u},
            {4, 2020536826u, 2954324759u},
            {5, 2463803361u, 2954324759u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 3423808932u, 565207875u},
            {0.008333333333333333, 3043841448u, 2221177405u},
            {0.21666666666666667, 95457257u, 842095907u},
            {0.225, 3413829530u, 2445966187u},
            {0.35, 2654290546u, 2658087091u},
            {0.35833333333333334, 1804429175u, 3428459699u},
            {0.5, 1424547023u, 3537714311u},
            {0.7, 2205458592u, 4014569169u},
            {0.8583333333333333, 562058571u, 3390926627u},
            {0.9916666666666667, 3943475833u, 535512905u},
            {1, 2034439340u, 2816277495u},
            {1.0083333333333333, 1412363748u, 3047430295u},
            {1.9916666666666667, 1918664277u, 3698740159u},
            {2, 3423808932u, 565207875u},
            {2.0083333333333333, 3043841448u, 2221177405u},
            {4, 3423808932u, 565207875u},
            {5, 2034439340u, 2816277495u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 3937419694u, 3302560151u},
            {0.008333333333333333, 275271252u, 113117561u},
            {0.21666666666666667, 3865290548u, 354473401u},
            {0.225, 1630049825u, 164660013u},
            {0.35, 2051255185u, 777538625u},
            {0.35833333333333334, 255154863u, 3943467773u},
            {0.5, 3288214403u, 4117170285u},
            {0.7, 1719987817u, 1589601361u},
            {0.8583333333333333, 116445388u, 4150351053u},
            {0.9916666666666667, 488928369u, 1300307757u},
            {1, 4088016987u, 3302560151u},
            {1.0083333333333333, 3722973820u, 113117561u},
            {1.9916666666666667, 1067254037u, 1300307757u},
            {2, 3937419694u, 3302560151u},
            {2.0083333333333333, 275271252u, 113117561u},
            {4, 3937419694u, 3302560151u},
            {5, 4088016987u, 3302560151u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 989180027u, 1152099797u},
            {0.008333333333333333, 2643908480u, 760704327u},
            {0.21666666666666667, 780146061u, 2994981841u},
            {0.225, 3620701394u, 647184193u},
            {0.35, 3976728617u, 3893802089u},
            {0.35833333333333334, 1311731459u, 1479445093u},
            {0.5, 803340321u, 3892070409u},
            {0.7, 1870169744u, 2044148337u},
            {0.8583333333333333, 1248236497u, 3881009025u},
            {0.9916666666666667, 3774469436u, 3553474729u},
            {1, 282639168u, 1152099797u},
            {1.0083333333333333, 546544275u, 760704327u},
            {1.9916666666666667, 709854649u, 3553474729u},
            {2, 989180027u, 1152099797u},
            {2.0083333333333333, 2643908480u, 760704327u},
            {4, 989180027u, 1152099797u},
            {5, 282639168u, 1152099797u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 1181698561u, 767926671u},
            {0.008333333333333333, 3745955509u, 1039112473u},
            {0.21666666666666667, 3847791050u, 3443525261u},
            {0.225, 33539106u, 3213256377u},
            {0.35, 3351786669u, 4043409925u},
            {0.35833333333333334, 1939707084u, 1972536329u},
            {0.5, 2668280080u, 2474505589u},
            {0.7, 2196180170u, 1249135385u},
            {0.8583333333333333, 3916684475u, 1153119929u},
            {0.9916666666666667, 1135278199u, 3133596077u},
            {1, 1295313330u, 767926671u},
            {1.0083333333333333, 2701109090u, 1039112473u},
            {1.9916666666666667, 3473206668u, 3133596077u},
            {2, 1181698561u, 767926671u},
            {2.0083333333333333, 3745955509u, 1039112473u},
            {4, 1181698561u, 767926671u},
            {5, 1295313330u, 767926671u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 33, "scale": 1, "x": -2.5, "y": -33, "head": [0, -33], "chest": [0, -16.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4285229931, 4285229931, 4285229931, 4285229931, 4285229931, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 1181698561u, 767926671u},
            {0.008333333333333333, 3745955509u, 1039112473u},
            {0.21666666666666667, 3847791050u, 3443525261u},
            {0.225, 33539106u, 3213256377u},
            {0.35, 3351786669u, 4043409925u},
            {0.35833333333333334, 1939707084u, 1972536329u},
            {0.5, 2668280080u, 2474505589u},
            {0.7, 2196180170u, 1249135385u},
            {0.8583333333333333, 3916684475u, 1153119929u},
            {0.9916666666666667, 1135278199u, 3133596077u},
            {1, 1295313330u, 767926671u},
            {1.0083333333333333, 2701109090u, 1039112473u},
            {1.9916666666666667, 3473206668u, 3133596077u},
            {2, 1181698561u, 767926671u},
            {2.0083333333333333, 3745955509u, 1039112473u},
            {4, 1181698561u, 767926671u},
            {5, 1295313330u, 767926671u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 33, "scale": 1, "x": -2.5, "y": -33, "head": [0, -33], "chest": [0, -16.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7039851, 7039851, 7039851, 7039851, 7039851, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2012480073u, 290827212u},
            {0.008333333333333333, 2585967073u, 161592490u},
            {0.21666666666666667, 4201341769u, 496156429u},
            {0.225, 4214667837u, 2487879817u},
            {0.35, 3343319348u, 1464848761u},
            {0.35833333333333334, 2259619763u, 3881324425u},
            {0.5, 1731829017u, 3280792653u},
            {0.7, 1481195132u, 2162155233u},
            {0.8583333333333333, 1925350453u, 3682660541u},
            {0.9916666666666667, 2241281884u, 3895831353u},
            {1, 2540289986u, 290827212u},
            {1.0083333333333333, 2191323242u, 161592490u},
            {1.9916666666666667, 1113222830u, 3895831353u},
            {2, 2012480073u, 290827212u},
            {2.0083333333333333, 2585967073u, 161592490u},
            {4, 2012480073u, 290827212u},
            {5, 2540289986u, 290827212u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 2, "scale": 1, "x": -0.5, "y": -2, "head": [0, -2], "chest": [0, -1.0], "eyes": [], "mask": [0, 1], "pixels": [0, 0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2020536826u, 2954324759u},
            {0.008333333333333333, 571127178u, 3814468661u},
            {0.21666666666666667, 82074761u, 2729473277u},
            {0.225, 4121207301u, 948219849u},
            {0.35, 3815597948u, 1959631649u},
            {0.35833333333333334, 3353258631u, 1931947913u},
            {0.5, 1636480479u, 2200069185u},
            {0.7, 471435189u, 2631914541u},
            {0.8583333333333333, 829349725u, 1865059453u},
            {0.9916666666666667, 4161923701u, 2515568049u},
            {1, 2463803361u, 2954324759u},
            {1.0083333333333333, 1010286945u, 3814468661u},
            {1.9916666666666667, 1418809302u, 2515568049u},
            {2, 2020536826u, 2954324759u},
            {2.0083333333333333, 571127178u, 3814468661u},
            {4, 2020536826u, 2954324759u},
            {5, 2463803361u, 2954324759u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [[0, 0], [0, 0]], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2020536826u, 2954324759u},
            {0.008333333333333333, 571127178u, 3814468661u},
            {0.21666666666666667, 82074761u, 2729473277u},
            {0.225, 4121207301u, 948219849u},
            {0.35, 1343104273u, 1959631649u},
            {0.35833333333333334, 4094713124u, 1931947913u},
            {0.5, 317334318u, 2200069185u},
            {0.7, 1078916392u, 2631914541u},
            {0.8583333333333333, 2453786017u, 1865059453u},
            {0.9916666666666667, 679898589u, 2515568049u},
            {1, 2463803361u, 2954324759u},
            {1.0083333333333333, 1010286945u, 3814468661u},
            {1.9916666666666667, 1292720272u, 2515568049u},
            {2, 2020536826u, 2954324759u},
            {2.0083333333333333, 571127178u, 3814468661u},
            {4, 2020536826u, 2954324759u},
            {5, 2463803361u, 2954324759u},
        };
        PixelFx fx(PixelFxKind::Frozen);
        auto    value = fx.configurationValue();
        auto    actor = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [[0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0], [0, 0]], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        value.find("options")->set("actor", actor.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Frozen, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            fx.render(f.time);
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
        }
    }
}
TEST_CASE("stylize.pixelfx.status.frozenMaximumCheckerboardRetainsSourceRootBound") {
    PixelFx fx(PixelFxKind::Frozen);
    auto    s = fx.settings();
    auto&   a = s.actor;
    a.source  = PixelFxActorSource::Custom;
    a.width   = 256;
    a.height  = 256;
    a.scale   = 1;
    a.x       = -128.25;
    a.y       = -256.5;
    a.head    = {0, -240};
    a.chest   = {0, -140};
    a.eyes.clear();
    a.mask.resize(65536);
    a.pixels.resize(65536);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const size_t i = size_t(y * 256 + x);
            a.mask[i]      = uint8_t((x + y) % 2 == 0);
            a.pixels[i]    = a.mask[i] ? 0x806b8099u : 0u;
        }
    const pixelfx::status::Shape shape(a);
    REQUIRE(shape.side.size() == 65536);
    const auto roots = std::count_if(shape.side.begin(), shape.side.end(),
                                     [](const auto& q) { return q[1] > 256 * .2 && q[1] < 256 * .85; });
    REQUIRE(roots == 42496);
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Frozen, s);
    REQUIRE(candidate);
    a.mask.clear();
    a.pixels.clear();
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.5);
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    fx.render(2.5);
    REQUIRE(checksum(fx.color()) == c);
    REQUIRE(checksum(fx.light()) == l);
    REQUIRE(fx.settings().actor.mask.size() == 65536);
}
}  // namespace test_pixelfx_status_unit
