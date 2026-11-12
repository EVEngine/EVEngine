// Pinned-source final screen-transition and prepared-simulation contracts.
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <stdexcept>
#include "stylize/PixelFx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_transition_final_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
void sample(PixelFx& fx, double seconds) {
    auto candidate = fx.prepareSeek(seconds);
    if (!candidate) throw std::runtime_error(candidate.status().describe());
    auto applied = fx.commitSeek(std::move(candidate).takeValue());
    if (!applied) throw std::runtime_error(applied.status().describe());
    fx.draw();
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.denseSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 2427538324u, 4127348145u},
        {0.25, 2313771748u, 1378586530u},
        {0.35, 3496349937u, 3612146474u},
        {0.5, 2128066604u, 3692772141u},
        {0.5416666666666666, 3339749551u, 3609772727u},
        {0.55, 4033461253u, 1194628610u},
        {0.5583333333333333, 782555178u, 136177971u},
        {0.6333333333333333, 3976818017u, 1356547792u},
        {0.75, 796091276u, 3573088341u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1941848349u, 667191534u},
        {0.9083333333333333, 3062062149u, 2687381167u},
        {0.9166666666666666, 1387989326u, 2956193607u},
        {0.95, 2800442391u, 1280803796u},
        {0.9583333333333334, 2995456304u, 3621630241u},
        {0.9833333333333333, 895130438u, 3711579948u},
        {1, 2989683714u, 1866739446u},
        {1.075, 3278693441u, 3897104862u},
        {1.0833333333333333, 974978435u, 2576844629u},
        {1.175, 3376015622u, 2708515194u},
        {1.1833333333333333, 1533761773u, 3602688183u},
        {1.1916666666666667, 2966572518u, 974253655u},
        {1.3166666666666667, 2097661746u, 554718381u},
        {1.325, 2095423951u, 1327894803u},
        {1.3333333333333333, 1759768457u, 2531441064u},
        {1.3416666666666666, 304964050u, 3092232295u},
        {1.35, 2401035549u, 2148792214u},
        {1.3583333333333334, 118633237u, 1392316493u},
        {1.5, 2442810186u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    REQUIRE(fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.denseSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 13447963u},
        {0.008333333333333333, 2236050885u, 2239758153u},
        {0.025, 2236050885u, 417584573u},
        {0.058333333333333334, 2236050885u, 2327351338u},
        {0.06666666666666667, 2236050885u, 454188197u},
        {0.075, 2236050885u, 2657223073u},
        {0.125, 340521870u, 3565644921u},
        {0.25, 3107916416u, 204512828u},
        {0.35, 2725096789u, 2051173410u},
        {0.5, 49450194u, 3056349255u},
        {0.5416666666666666, 1567907076u, 1787789379u},
        {0.55, 2338198743u, 4189907801u},
        {0.5583333333333333, 4036072052u, 2548963609u},
        {0.6333333333333333, 3625882605u, 1810461703u},
        {0.75, 1643443418u, 419814969u},
        {0.8333333333333334, 582747917u, 3294195513u},
        {0.8416666666666667, 4026692460u, 4090578398u},
        {0.9083333333333333, 242727173u, 1420004205u},
        {0.9166666666666666, 2971984674u, 3253705778u},
        {0.95, 2588542592u, 3401051358u},
        {0.9583333333333334, 3343112293u, 3639249336u},
        {0.9833333333333333, 1578623561u, 2004998767u},
        {1, 1439303428u, 3024694553u},
        {1.075, 2633432459u, 4231676735u},
        {1.0833333333333333, 849444353u, 874457935u},
        {1.175, 1085524234u, 1863426866u},
        {1.1833333333333333, 19833701u, 4034392592u},
        {1.1916666666666667, 2194028220u, 3049646645u},
        {1.3166666666666667, 536757269u, 1065041122u},
        {1.325, 1947321308u, 2449443176u},
        {1.3333333333333333, 718394039u, 3288734661u},
        {1.3416666666666666, 906706094u, 488132429u},
        {1.35, 2672653251u, 2017465036u},
        {1.3583333333333334, 2007625302u, 480008674u},
        {1.5, 764180519u, 1801989267u},
        {1.6166666666666667, 2793525393u, 3377950162u},
        {1.625, 2023574978u, 57685606u},
        {1.6333333333333333, 3415796648u, 2211741670u},
        {1.75, 4176894863u, 1360561811u},
        {1.7916666666666667, 420101827u, 1025163279u},
        {1.8, 2498841763u, 1673822428u},
        {1.8083333333333333, 3787937838u, 3580709422u},
        {2, 1949602897u, 1850968355u},
        {2.2, 2183124792u, 182561742u},
        {2.5, 3682513732u, 570853590u},
        {2.6, 1366420333u, 2398191652u},
        {2.7, 191111662u, 2612956842u},
        {2.75, 1170373412u, 1368564915u},
        {2.8, 809829630u, 1597599367u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    REQUIRE(fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.denseSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 427258451u, 1545084428u},
        {0.25, 1664545383u, 3942623096u},
        {0.35, 4259688131u, 1392951900u},
        {0.5, 1835491051u, 2544697148u},
        {0.5416666666666666, 2728215403u, 3770940347u},
        {0.55, 2215470074u, 2365632871u},
        {0.5583333333333333, 3009476138u, 1694180609u},
        {0.6333333333333333, 3452105925u, 247258437u},
        {0.75, 3438655536u, 3557219044u},
        {0.8333333333333334, 3220974826u, 2347981963u},
        {0.8416666666666667, 2929736547u, 4274561281u},
        {0.9083333333333333, 1627627126u, 3389163785u},
        {0.9166666666666666, 896449224u, 200083394u},
        {0.95, 3242922727u, 73185466u},
        {0.9583333333333334, 3728296631u, 3495216587u},
        {0.9833333333333333, 1703556377u, 1035826118u},
        {1, 2794916528u, 2768988929u},
        {1.075, 1001183422u, 300907715u},
        {1.0833333333333333, 2889218959u, 1011270279u},
        {1.175, 657846712u, 1727483207u},
        {1.1833333333333333, 3144046232u, 678111065u},
        {1.1916666666666667, 2404375975u, 2927922796u},
        {1.3166666666666667, 2836036029u, 1787120613u},
        {1.325, 3124586089u, 588917966u},
        {1.3333333333333333, 2778847680u, 3167602416u},
        {1.3416666666666666, 1228722076u, 697109710u},
        {1.35, 1824610775u, 2964167107u},
        {1.3583333333333334, 2345505208u, 268313893u},
        {1.5, 1941402066u, 3780327918u},
        {1.6166666666666667, 2899659369u, 880663137u},
        {1.625, 2642134289u, 1763798365u},
        {1.6333333333333333, 244992729u, 1468505676u},
        {1.75, 2858012738u, 2113309669u},
        {1.7916666666666667, 1787142869u, 3707030549u},
        {1.8, 2794518812u, 4103261328u},
        {1.8083333333333333, 2778871614u, 4051818890u},
        {2, 3996463346u, 3183372669u},
        {2.2, 60338908u, 2398529717u},
        {2.5, 2196109917u, 3460542012u},
        {2.6, 3999422111u, 3223613345u},
        {2.7, 2805115825u, 1796607779u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    REQUIRE(fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.oldAndPreparedSimulationStrategiesRestoreAndCompose") {
    for (auto kind : {PixelFxKind::SlashWipe, PixelFxKind::BurnAway, PixelFxKind::ShatterWipe, PixelFxKind::DiamondWipe,
                      PixelFxKind::CrtOff}) {
        PixelFx fx(kind);
        REQUIRE(fx.isSimulated() ==
                (kind == PixelFxKind::SlashWipe || kind == PixelFxKind::BurnAway || kind == PixelFxKind::ShatterWipe));
        uint32_t oldColor = 0, oldLight = 0;
        for (double t :
             {.7, 5., .05, .2, .84, .91, .96, 1.08, 1.18, 1.32, 1.35, 1.5, 1.62, 1.8, 2.62, 2.8, 3., .7, 0., .7}) {
            auto seek = fx.prepareSeek(t);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            const auto            c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                                  l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
            std::vector<uint32_t> color(c.size()), light(l.size());
            const auto            info = fx.info();
            PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
            REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
            REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
            REQUIRE(color == c);
            REQUIRE(light == l);
            fx.draw();
            REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
            REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
            REQUIRE(fx.effectTime() == t);
            if (t == .7) {
                if (oldColor) {
                    REQUIRE(checksum(c) == oldColor);
                    REQUIRE(checksum(l) == oldLight);
                } else {
                    oldColor = checksum(c);
                    oldLight = checksum(l);
                }
            }
        }
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto c) { return c == 0; }));
        auto seek = fx.prepareSeek(.7);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        REQUIRE(checksum(fx.color()) == oldColor);
        REQUIRE(checksum(fx.light()) == oldLight);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.preparedSimulationGeometryLifetimeAndConfigurationIdentity") {
    for (auto kind : {PixelFxKind::SlashWipe, PixelFxKind::BurnAway, PixelFxKind::ShatterWipe}) {
        PixelFx fx(kind);
        sample(fx, .7);
        auto stale = fx.prepareSeek(.4);
        REQUIRE(stale);
        auto settings    = fx.settings();
        settings.seed    = 123;
        settings.palette = {0xff123456u};
        auto prepared    = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(prepared);
        settings.seed = 1;
        settings.palette.clear();
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        REQUIRE(fx.settings().seed == 123);
        sample(fx, .7);
        const auto config = fx.configurationValue();
        const auto c      = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
                   l      = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
        REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
        REQUIRE(fx.configurationValue() == config);
        REQUIRE(fx.effectTime() == .7);
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
        PixelFx mirror(kind);
        auto    other = PixelFx::prepareConfigurationValue(kind, config);
        REQUIRE(other);
        REQUIRE(mirror.commitConfiguration(std::move(other).takeValue()));
        sample(mirror, .7);
        REQUIRE(std::equal(c.begin(), c.end(), mirror.color().begin()));
        REQUIRE(std::equal(l.begin(), l.end(), mirror.light().begin()));
        // Replace a program with no outstanding seek, then move the owner. Kernels retain their geometry.
        auto replacement = PixelFx::prepareConfiguration(kind, PixelFx::defaults(kind));
        REQUIRE(replacement);
        REQUIRE(mirror.commitConfiguration(std::move(replacement).takeValue()));
        PixelFx moved(std::move(mirror));
        sample(moved, .7);
        PixelFx reference(kind);
        sample(reference, .7);
        REQUIRE(std::equal(moved.color().begin(), moved.color().end(), reference.color().begin()));
        REQUIRE(std::equal(moved.light().begin(), moved.light().end(), reference.light().begin()));
        std::optional<PixelFxPreparedSeek> survivor;
        {
            PixelFx temporary(kind);
            auto    pending = temporary.prepareSeek(1.4);
            REQUIRE(pending);
            survivor.emplace(std::move(pending).takeValue());
        }
        REQUIRE(!fx.commitSeek(std::move(*survivor)));
        REQUIRE(fx.configurationValue() == config);
        survivor.reset();
        auto bad = config;
        bad.find("options")->set("seed", eve::Value(int64_t(-1)));
        REQUIRE(!PixelFx::prepareConfigurationValue(kind, bad));
        REQUIRE(fx.configurationValue() == config);
        REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.partitionedClocksKeepPreparedFieldsAndRngOrder") {
    for (auto kind : {PixelFxKind::SlashWipe, PixelFxKind::BurnAway, PixelFxKind::ShatterWipe})
        for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
            PixelFx live(kind), reference(kind);
            double  elapsed = 0;
            for (double target : {.01, .034, .05, .075, .12, .13, .14, .5, .9, 1.5, 2.5, 3.2}) {
                while (elapsed < target) {
                    const double dt = std::min(step, target - elapsed);
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
                REQUIRE(live.done() == (live.effectTime() >= live.info().duration));
            }
        }
}
TEST_CASE("stylize.pixelfx.transitionFinal.authoredCoverEndAndIndependentSparks") {
    for (auto kind : {PixelFxKind::SlashWipe, PixelFxKind::BurnAway, PixelFxKind::ShatterWipe}) {
        PixelFx fx(kind);
        sample(fx, kind == PixelFxKind::SlashWipe ? .8 : kind == PixelFxKind::BurnAway ? 1.4 : 1.2);
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto c) { return (c >> 24) == 255; }));
        sample(fx, fx.info().duration);
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto c) { return c == 0; }));
    }
    PixelFx    fx(PixelFxKind::SlashWipe);
    auto       settings        = fx.settings();
    const auto primary         = settings.palette;
    settings.transition.sparks = {0xff123456u};
    auto prepared              = PixelFx::prepareConfiguration(PixelFxKind::SlashWipe, settings);
    REQUIRE(prepared);
    settings.transition.sparks[0] = 0xffffffffu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().transition.sparks == std::vector<uint32_t>(4, 0xff123456u));
    REQUIRE(fx.settings().palette == primary);
    sample(fx, .4);
    const auto before = fx.configurationValue();
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    for (const char* json : {"null", "true", "[]", "[1]", "[4294967295,0]", "[4294967296]", "[-1]"}) {
        auto value = eve::Value::fromJson(json);
        REQUIRE(value);
        auto bad = before;
        bad.find("options")->set("sparks", value.value());
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, bad));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
        REQUIRE(fx.effectTime() == .4);
    }
    for (auto kind : {PixelFxKind::BurnAway, PixelFxKind::ShatterWipe}) {
        PixelFx other(kind);
        auto    bad = other.configurationValue();
        bad.find("options")->set("sparks", eve::Value::array({eve::Value(int64_t(0xffffffffu))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kind, bad));
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.varSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2050555559u},
        {0.008333333333333333, 2236050885u, 2050555559u},
        {0.025, 2236050885u, 2050555559u},
        {0.058333333333333334, 2236050885u, 2050555559u},
        {0.06666666666666667, 1621248899u, 3106358196u},
        {0.075, 4045799241u, 4058087743u},
        {0.125, 877603461u, 101547166u},
        {0.25, 2065676490u, 2762626640u},
        {0.35, 1585010262u, 2042455978u},
        {0.5, 205759748u, 3901034947u},
        {0.5416666666666666, 3682181918u, 2320539342u},
        {0.55, 3845979639u, 336948101u},
        {0.5583333333333333, 1510805360u, 616631356u},
        {0.6333333333333333, 2594783448u, 3942661702u},
        {0.75, 2033942977u, 3645251568u},
        {0.8333333333333334, 1677592005u, 3648520560u},
        {0.8416666666666667, 3897002291u, 675933703u},
        {0.9083333333333333, 299799414u, 655233988u},
        {0.9166666666666666, 2810445046u, 1866124029u},
        {0.95, 4276134051u, 560253017u},
        {0.9583333333333334, 1211010261u, 3597995843u},
        {0.9833333333333333, 443496115u, 3177754587u},
        {1, 3954062382u, 3704132485u},
        {1.075, 4288433057u, 2774762657u},
        {1.0833333333333333, 3109375625u, 1281588321u},
        {1.175, 1870868796u, 743975103u},
        {1.1833333333333333, 512580793u, 2490427720u},
        {1.1916666666666667, 4202756786u, 875950146u},
        {1.3166666666666667, 1811040362u, 3240712680u},
        {1.325, 4142995355u, 1895564700u},
        {1.3333333333333333, 2115330060u, 3681600274u},
        {1.3416666666666666, 948953902u, 307158037u},
        {1.35, 898846011u, 2839457470u},
        {1.3583333333333334, 2667835051u, 502924691u},
        {1.5, 3240764224u, 4064828206u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(4293980400)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.zeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 2267550321u, 483183731u},
        {0.25, 1060691159u, 3965201606u},
        {0.35, 2261804346u, 1434754685u},
        {0.5, 2061648839u, 892086119u},
        {0.5416666666666666, 3896644253u, 2840411664u},
        {0.55, 4089369037u, 991683514u},
        {0.5583333333333333, 2046379978u, 3134907207u},
        {0.6333333333333333, 3138027718u, 3319697662u},
        {0.75, 1387448959u, 1722605579u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 389747958u, 2844369330u},
        {0.9083333333333333, 355706252u, 1041192152u},
        {0.9166666666666666, 4207798908u, 575787434u},
        {0.95, 3961018287u, 1237483761u},
        {0.9583333333333334, 311051758u, 3099721359u},
        {0.9833333333333333, 2178563218u, 1134549731u},
        {1, 4036066619u, 3242983542u},
        {1.075, 351532276u, 4238055375u},
        {1.0833333333333333, 3034970563u, 579771955u},
        {1.175, 2950760189u, 1620467891u},
        {1.1833333333333333, 880673598u, 3913139679u},
        {1.1916666666666667, 4091640395u, 961036513u},
        {1.3166666666666667, 3131630123u, 2903555987u},
        {1.325, 1754760898u, 3641905485u},
        {1.3333333333333333, 2567658097u, 3421868638u},
        {1.3416666666666666, 290141823u, 2573529080u},
        {1.35, 615291176u, 2770549980u},
        {1.3583333333333334, 2432162351u, 3229107237u},
        {1.5, 2492441996u, 585159551u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.maxSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 240609350u, 2142761548u},
        {0.25, 3352600546u, 2653008490u},
        {0.35, 1302762111u, 3612733703u},
        {0.5, 234693732u, 1867040488u},
        {0.5416666666666666, 4000180391u, 2715860946u},
        {0.55, 875051323u, 729093300u},
        {0.5583333333333333, 979661846u, 1892718135u},
        {0.6333333333333333, 4234333030u, 2218205501u},
        {0.75, 1491770821u, 826023365u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1918989333u, 1810135832u},
        {0.9083333333333333, 4133901544u, 1308942235u},
        {0.9166666666666666, 3340828824u, 2296629046u},
        {0.95, 531968202u, 4203615684u},
        {0.9583333333333334, 582255646u, 3224456143u},
        {0.9833333333333333, 3288012636u, 1015553452u},
        {1, 856599248u, 3728253323u},
        {1.075, 1556672071u, 4138071815u},
        {1.0833333333333333, 2707629580u, 1072321430u},
        {1.175, 454548691u, 1287902152u},
        {1.1833333333333333, 940114845u, 267989285u},
        {1.1916666666666667, 1379147203u, 4291486927u},
        {1.3166666666666667, 3813043380u, 2537489216u},
        {1.325, 1470575247u, 1724555538u},
        {1.3333333333333333, 2049209849u, 4006720622u},
        {1.3416666666666666, 3628805022u, 1699201220u},
        {1.35, 2980481064u, 2278525100u},
        {1.3583333333333334, 1831219182u, 3799730386u},
        {1.5, 1013209719u, 1658309666u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.singleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 1891613436u},
        {0.008333333333333333, 2236050885u, 1891613436u},
        {0.025, 2236050885u, 1891613436u},
        {0.058333333333333334, 2236050885u, 1891613436u},
        {0.06666666666666667, 4219261993u, 985665134u},
        {0.075, 1743351097u, 425607627u},
        {0.125, 3318764469u, 2675474431u},
        {0.25, 2506571059u, 2741848769u},
        {0.35, 4033992552u, 2263194225u},
        {0.5, 3337812038u, 3577120578u},
        {0.5416666666666666, 3745373379u, 550553507u},
        {0.55, 1016682634u, 1462933815u},
        {0.5583333333333333, 713655302u, 2632032954u},
        {0.6333333333333333, 2109211153u, 1036862066u},
        {0.75, 3681414769u, 2580455277u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 3972659089u, 2041837275u},
        {0.9083333333333333, 2225944165u, 2425753994u},
        {0.9166666666666666, 972261680u, 2101934613u},
        {0.95, 2577786037u, 833154793u},
        {0.9583333333333334, 93253069u, 1167704492u},
        {0.9833333333333333, 931595449u, 4095115741u},
        {1, 2058478380u, 1676039136u},
        {1.075, 3598064020u, 2120433979u},
        {1.0833333333333333, 2196846652u, 778995742u},
        {1.175, 2280584628u, 2980425503u},
        {1.1833333333333333, 863813602u, 2427316797u},
        {1.1916666666666667, 4098736731u, 3461156096u},
        {1.3166666666666667, 1878776256u, 767595202u},
        {1.325, 1044670529u, 2867547195u},
        {1.3333333333333333, 2868330326u, 3547597329u},
        {1.3416666666666666, 1654779342u, 3504693623u},
        {1.35, 3037038740u, 2191848424u},
        {1.3583333333333334, 1793394751u, 36384243u},
        {1.5, 566902473u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.manySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 1260382879u},
        {0.008333333333333333, 2236050885u, 1260382879u},
        {0.025, 2236050885u, 1260382879u},
        {0.058333333333333334, 2236050885u, 1260382879u},
        {0.06666666666666667, 144346986u, 1397486117u},
        {0.075, 885470405u, 3002938758u},
        {0.125, 1791267809u, 1037828296u},
        {0.25, 912016592u, 127799234u},
        {0.35, 1884178149u, 1730646306u},
        {0.5, 1254249025u, 22977987u},
        {0.5416666666666666, 3484160223u, 4262197369u},
        {0.55, 2087651391u, 615453620u},
        {0.5583333333333333, 2322815823u, 3720725932u},
        {0.6333333333333333, 43542533u, 1473612418u},
        {0.75, 2444303206u, 983424637u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1444256008u, 3001062530u},
        {0.9083333333333333, 3922531427u, 2451010917u},
        {0.9166666666666666, 1936644513u, 1677448320u},
        {0.95, 2562222737u, 2303883750u},
        {0.9583333333333334, 2062002559u, 3193117063u},
        {0.9833333333333333, 1808865790u, 1404781613u},
        {1, 3609757932u, 2143554235u},
        {1.075, 2721397211u, 1990554845u},
        {1.0833333333333333, 3089585384u, 2506188845u},
        {1.175, 2340441749u, 104022947u},
        {1.1833333333333333, 1173892383u, 3795453366u},
        {1.1916666666666667, 3012818560u, 3733980468u},
        {1.3166666666666667, 2350871845u, 1210559900u},
        {1.325, 328599959u, 38169074u},
        {1.3333333333333333, 1639975992u, 160829173u},
        {1.3416666666666666, 3437431295u, 1728766455u},
        {1.35, 1137244658u, 231205432u},
        {1.3583333333333334, 3890882225u, 2589571211u},
        {1.5, 3889531541u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.colorzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 3000338297u, 2539518595u},
        {0.125, 3653831596u, 4127348145u},
        {0.25, 257427484u, 1378586530u},
        {0.35, 2206113073u, 3612146474u},
        {0.5, 1320339076u, 3692772141u},
        {0.5416666666666666, 3945320603u, 3609772727u},
        {0.55, 2978862541u, 1194628610u},
        {0.5583333333333333, 312401346u, 136177971u},
        {0.6333333333333333, 145920001u, 1356547792u},
        {0.75, 3278691384u, 3573088341u},
        {0.8333333333333334, 2236050885u, 2236050885u},
        {0.8416666666666667, 2905910885u, 667191534u},
        {0.9083333333333333, 1806756193u, 2687381167u},
        {0.9166666666666666, 2991525238u, 2956193607u},
        {0.95, 3369834119u, 1280803796u},
        {0.9583333333333334, 1327173512u, 3621630241u},
        {0.9833333333333333, 3784293658u, 3711579948u},
        {1, 2151620186u, 1866739446u},
        {1.075, 3087607889u, 3897104862u},
        {1.0833333333333333, 941239379u, 2576844629u},
        {1.175, 2230136330u, 2708515194u},
        {1.1833333333333333, 1058536001u, 3602688183u},
        {1.1916666666666667, 498094174u, 974253655u},
        {1.3166666666666667, 4190250898u, 554718381u},
        {1.325, 1564473703u, 1327894803u},
        {1.3333333333333333, 1878997197u, 2531441064u},
        {1.3416666666666666, 2576654786u, 3092232295u},
        {1.35, 651118825u, 2148792214u},
        {1.3583333333333334, 4049106553u, 1392316493u},
        {1.5, 2431450142u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.colortransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 904253849u, 2539518595u},
        {0.125, 469324324u, 4127348145u},
        {0.25, 341488092u, 1378586530u},
        {0.35, 1709673765u, 3612146474u},
        {0.5, 1887704548u, 3692772141u},
        {0.5416666666666666, 3820120543u, 3609772727u},
        {0.55, 809574317u, 1194628610u},
        {0.5583333333333333, 2603737086u, 136177971u},
        {0.6333333333333333, 2581387313u, 1356547792u},
        {0.75, 2261500328u, 3573088341u},
        {0.8333333333333334, 1021096389u, 2236050885u},
        {0.8416666666666667, 3205649533u, 667191534u},
        {0.9083333333333333, 3318903301u, 2687381167u},
        {0.9166666666666666, 1677808630u, 2956193607u},
        {0.95, 2042704631u, 1280803796u},
        {0.9583333333333334, 1161314728u, 3621630241u},
        {0.9833333333333333, 981054250u, 3711579948u},
        {1, 1830772506u, 1866739446u},
        {1.075, 412059313u, 3897104862u},
        {1.0833333333333333, 2731555335u, 2576844629u},
        {1.175, 192596770u, 2708515194u},
        {1.1833333333333333, 2872834029u, 3602688183u},
        {1.1916666666666667, 3918538330u, 974253655u},
        {1.3166666666666667, 2382887134u, 554718381u},
        {1.325, 408781299u, 1327894803u},
        {1.3333333333333333, 1548043585u, 2531441064u},
        {1.3416666666666666, 1617297218u, 3092232295u},
        {1.35, 1994492189u, 2148792214u},
        {1.3583333333333334, 3611926161u, 1392316493u},
        {1.5, 2124475926u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.colorpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 1098308057u, 2539518595u},
        {0.125, 3894305636u, 4127348145u},
        {0.25, 3641723676u, 1378586530u},
        {0.35, 176754405u, 3612146474u},
        {0.5, 4023505924u, 3692772141u},
        {0.5416666666666666, 1271480479u, 3609772727u},
        {0.55, 2730833549u, 1194628610u},
        {0.5583333333333333, 352189182u, 136177971u},
        {0.6333333333333333, 1199242801u, 1356547792u},
        {0.75, 175523144u, 3573088341u},
        {0.8333333333333334, 1441378757u, 2236050885u},
        {0.8416666666666667, 3613558877u, 667191534u},
        {0.9083333333333333, 3114381701u, 2687381167u},
        {0.9166666666666666, 3983342454u, 2956193607u},
        {0.95, 4179646807u, 1280803796u},
        {0.9583333333333334, 133587240u, 3621630241u},
        {0.9833333333333333, 2389919402u, 3711579948u},
        {1, 2501526810u, 1866739446u},
        {1.075, 2622828177u, 3897104862u},
        {1.0833333333333333, 3137712231u, 2576844629u},
        {1.175, 123211746u, 2708515194u},
        {1.1833333333333333, 3670775309u, 3602688183u},
        {1.1916666666666667, 3053564602u, 974253655u},
        {1.3166666666666667, 4059462078u, 554718381u},
        {1.325, 786773843u, 1327894803u},
        {1.3333333333333333, 1645974721u, 2531441064u},
        {1.3416666666666666, 379123010u, 3092232295u},
        {1.35, 4113647965u, 2148792214u},
        {1.3583333333333334, 1271664177u, 1392316493u},
        {1.5, 3332018198u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.colorwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2928033177u, 2539518595u},
        {0.125, 2157136176u, 4127348145u},
        {0.25, 2572974748u, 1378586530u},
        {0.35, 344525353u, 3612146474u},
        {0.5, 3444260140u, 3692772141u},
        {0.5416666666666666, 2590096367u, 3609772727u},
        {0.55, 3572378765u, 1194628610u},
        {0.5583333333333333, 2685259162u, 136177971u},
        {0.6333333333333333, 1067177857u, 1356547792u},
        {0.75, 3769579580u, 3573088341u},
        {0.8333333333333334, 2123157957u, 2236050885u},
        {0.8416666666666667, 3530774889u, 667191534u},
        {0.9083333333333333, 4249541329u, 2687381167u},
        {0.9166666666666666, 2055311130u, 2956193607u},
        {0.95, 3130001003u, 1280803796u},
        {0.9583333333333334, 400493748u, 3621630241u},
        {0.9833333333333333, 1483464026u, 3711579948u},
        {1, 1062441022u, 1866739446u},
        {1.075, 2786926605u, 3897104862u},
        {1.0833333333333333, 369722367u, 2576844629u},
        {1.175, 2367748250u, 2708515194u},
        {1.1833333333333333, 3584907917u, 3602688183u},
        {1.1916666666666667, 184897714u, 974253655u},
        {1.3166666666666667, 3389145866u, 554718381u},
        {1.325, 3758324791u, 1327894803u},
        {1.3333333333333333, 4024871309u, 2531441064u},
        {1.3416666666666666, 3882460662u, 3092232295u},
        {1.35, 3430184773u, 2148792214u},
        {1.3583333333333334, 3841507969u, 1392316493u},
        {1.5, 3333633862u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.sparkssingleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 3160602088u, 3489347344u},
        {0.25, 1499658346u, 2418036530u},
        {0.35, 29036020u, 3949987308u},
        {0.5, 4062860691u, 2843389615u},
        {0.5416666666666666, 2953625249u, 471235881u},
        {0.55, 2792108683u, 2170704335u},
        {0.5583333333333333, 1891753336u, 1659909102u},
        {0.6333333333333333, 780247665u, 1713747098u},
        {0.75, 796091276u, 3573088341u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1941848349u, 667191534u},
        {0.9083333333333333, 3062062149u, 2687381167u},
        {0.9166666666666666, 4287348899u, 616910375u},
        {0.95, 260567838u, 3061037330u},
        {0.9583333333333334, 2995456304u, 3621630241u},
        {0.9833333333333333, 895130438u, 3711579948u},
        {1, 2989683714u, 1866739446u},
        {1.075, 3278693441u, 3897104862u},
        {1.0833333333333333, 2173676739u, 4116668777u},
        {1.175, 4020925634u, 2582825082u},
        {1.1833333333333333, 453019838u, 2714393060u},
        {1.1916666666666667, 3889417068u, 2759047831u},
        {1.3166666666666667, 3616134891u, 1431233818u},
        {1.325, 1212850334u, 3411374319u},
        {1.3333333333333333, 1046674603u, 2882352315u},
        {1.3416666666666666, 2343522529u, 4204039709u},
        {1.35, 39645961u, 1593933269u},
        {1.3583333333333334, 93352138u, 2684245889u},
        {1.5, 2442810186u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("sparks", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.sparksvarSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 2596131716u, 2084846911u},
        {0.25, 1302617690u, 3083305729u},
        {0.35, 2551251539u, 3639985215u},
        {0.5, 822691512u, 1406249749u},
        {0.5416666666666666, 842854108u, 2701628135u},
        {0.55, 2596937074u, 3545949835u},
        {0.5583333333333333, 1409696585u, 392113342u},
        {0.6333333333333333, 3504950265u, 2530421276u},
        {0.75, 796091276u, 3573088341u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1941848349u, 667191534u},
        {0.9083333333333333, 3062062149u, 2687381167u},
        {0.9166666666666666, 1325422757u, 3195053138u},
        {0.95, 64915880u, 2576817519u},
        {0.9583333333333334, 2995456304u, 3621630241u},
        {0.9833333333333333, 895130438u, 3711579948u},
        {1, 2989683714u, 1866739446u},
        {1.075, 3278693441u, 3897104862u},
        {1.0833333333333333, 1982754067u, 3121829242u},
        {1.175, 3205572170u, 1448463241u},
        {1.1833333333333333, 852016285u, 2483477377u},
        {1.1916666666666667, 4237325893u, 169296062u},
        {1.3166666666666667, 237649212u, 2222847488u},
        {1.325, 2437250597u, 2068566463u},
        {1.3333333333333333, 505931791u, 365294912u},
        {1.3416666666666666, 3197445891u, 649141238u},
        {1.35, 2440230476u, 2748754248u},
        {1.3583333333333334, 2461242425u, 154966336u},
        {1.5, 2442810186u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("sparks", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.sparksmanySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 72247684u},
        {0.008333333333333333, 2236050885u, 72247684u},
        {0.025, 2236050885u, 72247684u},
        {0.058333333333333334, 2236050885u, 72247684u},
        {0.06666666666666667, 238483491u, 2803845054u},
        {0.075, 2741584181u, 2539518595u},
        {0.125, 3181555940u, 2379561059u},
        {0.25, 3859954612u, 438912589u},
        {0.35, 4188139130u, 4064151053u},
        {0.5, 1245031859u, 2050834255u},
        {0.5416666666666666, 3691351195u, 3541060565u},
        {0.55, 3673121429u, 3365120141u},
        {0.5583333333333333, 1056866922u, 3599180382u},
        {0.6333333333333333, 4219759889u, 2107391979u},
        {0.75, 796091276u, 3573088341u},
        {0.8333333333333334, 1491770821u, 2236050885u},
        {0.8416666666666667, 1941848349u, 667191534u},
        {0.9083333333333333, 3062062149u, 2687381167u},
        {0.9166666666666666, 3000941658u, 3644860180u},
        {0.95, 3624805215u, 247220849u},
        {0.9583333333333334, 2995456304u, 3621630241u},
        {0.9833333333333333, 895130438u, 3711579948u},
        {1, 2989683714u, 1866739446u},
        {1.075, 3278693441u, 3897104862u},
        {1.0833333333333333, 804186151u, 76253206u},
        {1.175, 2802285238u, 913477568u},
        {1.1833333333333333, 2797054558u, 3032434817u},
        {1.1916666666666667, 1700601685u, 1659317640u},
        {1.3166666666666667, 364004693u, 2185777383u},
        {1.325, 104246728u, 1518281977u},
        {1.3333333333333333, 42846153u, 1510232154u},
        {1.3416666666666666, 4236003489u, 3152651663u},
        {1.35, 3360986217u, 652284431u},
        {1.3583333333333334, 1909737866u, 2403216706u},
        {1.5, 2442810186u, 2236050885u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("sparks", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.slashwipe.sparkscombinedSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2050555559u},
        {0.008333333333333333, 2236050885u, 2050555559u},
        {0.025, 2236050885u, 2050555559u},
        {0.058333333333333334, 2236050885u, 2050555559u},
        {0.06666666666666667, 1621248899u, 3106358196u},
        {0.075, 561285977u, 4058087743u},
        {0.125, 192451575u, 2436629736u},
        {0.25, 894349830u, 3897439254u},
        {0.35, 1508129521u, 3625829666u},
        {0.5, 3163444859u, 1904360491u},
        {0.5416666666666666, 2060494013u, 961532822u},
        {0.55, 4106157510u, 1170497630u},
        {0.5583333333333333, 533189698u, 1934122140u},
        {0.6333333333333333, 118415934u, 4270356562u},
        {0.75, 1441378757u, 2033360076u},
        {0.8333333333333334, 1441378757u, 2236050885u},
        {0.8416666666666667, 420749321u, 2834063615u},
        {0.9083333333333333, 1190410787u, 1845678569u},
        {0.9166666666666666, 411995719u, 309023297u},
        {0.95, 2202473156u, 2266214015u},
        {0.9583333333333334, 3022312094u, 2362118104u},
        {0.9833333333333333, 1138744342u, 2607446342u},
        {1, 1932068483u, 3597269488u},
        {1.075, 601207283u, 3642920734u},
        {1.0833333333333333, 1457544995u, 907435685u},
        {1.175, 1071906624u, 4285774981u},
        {1.1833333333333333, 1276572941u, 2211712740u},
        {1.1916666666666667, 2198477315u, 983225266u},
        {1.3166666666666667, 2395178935u, 3405227348u},
        {1.325, 3539366302u, 1029028487u},
        {1.3333333333333333, 2146181814u, 3487238728u},
        {1.3416666666666666, 378295756u, 3641109835u},
        {1.35, 1804946624u, 1010769373u},
        {1.3583333333333334, 3458109049u, 2856427030u},
        {1.5, 4058573390u, 3647061946u},
        {1.6166666666666667, 2236050885u, 2236050885u},
        {1.625, 2236050885u, 2236050885u},
        {1.6333333333333333, 2236050885u, 2236050885u},
        {1.75, 2236050885u, 2236050885u},
        {1.7916666666666667, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.8083333333333333, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {2.6, 2236050885u, 2236050885u},
        {2.7, 2236050885u, 2236050885u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::SlashWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279383126, 4291015329, 4288252023])json");
        REQUIRE(input);
        value.find("options")->set("sparks", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SlashWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.varSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 3232957383u},
        {0.008333333333333333, 2236050885u, 842973965u},
        {0.025, 2236050885u, 2243772717u},
        {0.058333333333333334, 2236050885u, 367163930u},
        {0.06666666666666667, 2236050885u, 2292048865u},
        {0.075, 2236050885u, 97295063u},
        {0.125, 4082081064u, 472558634u},
        {0.25, 59068245u, 1142318394u},
        {0.35, 441726364u, 1967684747u},
        {0.5, 306578481u, 2057716935u},
        {0.5416666666666666, 36678891u, 1836089512u},
        {0.55, 1737433725u, 3092967562u},
        {0.5583333333333333, 1251067130u, 648568020u},
        {0.6333333333333333, 2500328812u, 4266181776u},
        {0.75, 2037198629u, 1159636874u},
        {0.8333333333333334, 1503839520u, 3341672470u},
        {0.8416666666666667, 1959141407u, 428728241u},
        {0.9083333333333333, 3827535436u, 2053013462u},
        {0.9166666666666666, 2812484088u, 707816907u},
        {0.95, 513454418u, 166931078u},
        {0.9583333333333334, 111534821u, 2653306890u},
        {0.9833333333333333, 993998305u, 1292559512u},
        {1, 920564784u, 1022144190u},
        {1.075, 3057064385u, 1360659816u},
        {1.0833333333333333, 509267557u, 805217950u},
        {1.175, 1114607479u, 1714466389u},
        {1.1833333333333333, 247880558u, 4258118961u},
        {1.1916666666666667, 3607914500u, 3828976664u},
        {1.3166666666666667, 2736393488u, 3835553443u},
        {1.325, 1865897353u, 4052434789u},
        {1.3333333333333333, 2181273438u, 4047415451u},
        {1.3416666666666666, 2461572818u, 2234290200u},
        {1.35, 3904494033u, 667315365u},
        {1.3583333333333334, 2747568241u, 2744774936u},
        {1.5, 3944905146u, 2427221225u},
        {1.6166666666666667, 2266270268u, 451471706u},
        {1.625, 4027831373u, 1524079466u},
        {1.6333333333333333, 1645782803u, 1673076254u},
        {1.75, 2306218458u, 1964416438u},
        {1.7916666666666667, 2629697640u, 2633654694u},
        {1.8, 541653770u, 1546256910u},
        {1.8083333333333333, 3005970155u, 2504366693u},
        {2, 2548532481u, 3703345888u},
        {2.2, 570118991u, 1289160098u},
        {2.5, 4063967176u, 131888202u},
        {2.6, 796197158u, 1438812051u},
        {2.7, 2899281302u, 1056416168u},
        {2.75, 910683257u, 1085487132u},
        {2.8, 551740475u, 1348128495u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(4293980400)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.zeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2782726369u},
        {0.008333333333333333, 2236050885u, 865982433u},
        {0.025, 2236050885u, 2548672247u},
        {0.058333333333333334, 2236050885u, 2350124104u},
        {0.06666666666666667, 2236050885u, 1863685926u},
        {0.075, 2236050885u, 3506029795u},
        {0.125, 3942347684u, 3167561818u},
        {0.25, 1555147621u, 86984755u},
        {0.35, 642515161u, 3979349174u},
        {0.5, 1672663486u, 1475822143u},
        {0.5416666666666666, 934042348u, 2193267u},
        {0.55, 3944211795u, 1603301141u},
        {0.5583333333333333, 2997834905u, 113966132u},
        {0.6333333333333333, 2607180200u, 3512882137u},
        {0.75, 481078414u, 3305929762u},
        {0.8333333333333334, 1269398495u, 2967030547u},
        {0.8416666666666667, 2392817502u, 771338876u},
        {0.9083333333333333, 1364104625u, 1971287552u},
        {0.9166666666666666, 213559890u, 3287033897u},
        {0.95, 1298227426u, 2447713991u},
        {0.9583333333333334, 4257739438u, 2314790283u},
        {0.9833333333333333, 4060799329u, 4120055059u},
        {1, 3370453805u, 441211234u},
        {1.075, 1577998768u, 1409677351u},
        {1.0833333333333333, 2302236320u, 2003413781u},
        {1.175, 999840510u, 25519743u},
        {1.1833333333333333, 1968655195u, 1355292738u},
        {1.1916666666666667, 2327770652u, 2170723416u},
        {1.3166666666666667, 2472639846u, 2262533839u},
        {1.325, 1190678136u, 2579188430u},
        {1.3333333333333333, 3044141154u, 489290142u},
        {1.3416666666666666, 44922188u, 1307977768u},
        {1.35, 4170055366u, 1027006011u},
        {1.3583333333333334, 908812693u, 267415741u},
        {1.5, 937765149u, 3739620765u},
        {1.6166666666666667, 1783236997u, 3222602260u},
        {1.625, 743106556u, 2296946322u},
        {1.6333333333333333, 2695677890u, 1600965050u},
        {1.75, 1586737220u, 3944661457u},
        {1.7916666666666667, 700305956u, 1052413581u},
        {1.8, 2792481014u, 1034447429u},
        {1.8083333333333333, 3117571775u, 32585071u},
        {2, 1760666688u, 711206015u},
        {2.2, 3364908036u, 3563153692u},
        {2.5, 3665972024u, 3487438956u},
        {2.6, 1746870507u, 2686553108u},
        {2.7, 1155743054u, 841975450u},
        {2.75, 200804484u, 966175038u},
        {2.8, 1589078726u, 150177482u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.maxSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 1157112169u},
        {0.008333333333333333, 2236050885u, 320514222u},
        {0.025, 2236050885u, 1867400689u},
        {0.058333333333333334, 2236050885u, 2013635104u},
        {0.06666666666666667, 2236050885u, 2736702389u},
        {0.075, 2236050885u, 1528926685u},
        {0.125, 23819027u, 2768329104u},
        {0.25, 1626560612u, 727832576u},
        {0.35, 3424930537u, 1976652532u},
        {0.5, 1710557800u, 1398241469u},
        {0.5416666666666666, 1185201765u, 2742491384u},
        {0.55, 2669429870u, 1910027774u},
        {0.5583333333333333, 2918208825u, 526944857u},
        {0.6333333333333333, 2477379426u, 1537114623u},
        {0.75, 1826821503u, 2620500783u},
        {0.8333333333333334, 397403721u, 1641431316u},
        {0.8416666666666667, 2146136080u, 2726375845u},
        {0.9083333333333333, 337817624u, 2444727198u},
        {0.9166666666666666, 964267978u, 2023815391u},
        {0.95, 4083828959u, 3416673516u},
        {0.9583333333333334, 2302421747u, 1191272750u},
        {0.9833333333333333, 3938528209u, 1289970336u},
        {1, 3436099074u, 355142838u},
        {1.075, 3318907532u, 1077895161u},
        {1.0833333333333333, 3560317350u, 3630360889u},
        {1.175, 198256871u, 3148134784u},
        {1.1833333333333333, 2173466650u, 1674458674u},
        {1.1916666666666667, 388360393u, 4032057082u},
        {1.3166666666666667, 3462958905u, 2681575331u},
        {1.325, 1337047804u, 647017250u},
        {1.3333333333333333, 3308585383u, 859943972u},
        {1.3416666666666666, 1404144514u, 3656743964u},
        {1.35, 9871751u, 2433538266u},
        {1.3583333333333334, 1169936413u, 3078502680u},
        {1.5, 107467551u, 2271019941u},
        {1.6166666666666667, 3786525613u, 1009087996u},
        {1.625, 1145574207u, 1999548635u},
        {1.6333333333333333, 2653633232u, 1615312368u},
        {1.75, 456688700u, 1638248785u},
        {1.7916666666666667, 3478234664u, 3475114956u},
        {1.8, 2383250140u, 1517689862u},
        {1.8083333333333333, 3368654893u, 1906239879u},
        {2, 3075822042u, 719270439u},
        {2.2, 925713954u, 2954098377u},
        {2.5, 1323945973u, 649264022u},
        {2.6, 2444313917u, 728373373u},
        {2.7, 2381692677u, 159393924u},
        {2.75, 1993660226u, 1289077174u},
        {2.8, 2848530815u, 4142291518u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.singleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2240428417u},
        {0.008333333333333333, 2236050885u, 1066618865u},
        {0.025, 2236050885u, 3033160708u},
        {0.058333333333333334, 2236050885u, 224728399u},
        {0.06666666666666667, 2236050885u, 901858140u},
        {0.075, 2236050885u, 3909531326u},
        {0.125, 340521870u, 955436965u},
        {0.25, 2447814119u, 406857863u},
        {0.35, 4149292367u, 1796710547u},
        {0.5, 898487105u, 2070239709u},
        {0.5416666666666666, 2878608281u, 407389061u},
        {0.55, 2620793640u, 1472035838u},
        {0.5583333333333333, 935645080u, 3648280205u},
        {0.6333333333333333, 316672862u, 3568675570u},
        {0.75, 2948712965u, 487579213u},
        {0.8333333333333334, 3048769512u, 2648644608u},
        {0.8416666666666667, 2858574774u, 3813559100u},
        {0.9083333333333333, 2226127364u, 1565275722u},
        {0.9166666666666666, 2860411694u, 967310998u},
        {0.95, 144374712u, 3610434791u},
        {0.9583333333333334, 2119035888u, 111905382u},
        {0.9833333333333333, 3879837356u, 118759805u},
        {1, 3995266752u, 1432296911u},
        {1.075, 3355978617u, 841975901u},
        {1.0833333333333333, 1723508664u, 4222670237u},
        {1.175, 59944715u, 1243134612u},
        {1.1833333333333333, 3301399614u, 707306051u},
        {1.1916666666666667, 1754767788u, 3978218408u},
        {1.3166666666666667, 4235370614u, 711153813u},
        {1.325, 2735647790u, 1062374088u},
        {1.3333333333333333, 127573318u, 3068755979u},
        {1.3416666666666666, 184857013u, 2906109632u},
        {1.35, 603546299u, 582145624u},
        {1.3583333333333334, 2997589366u, 4221441428u},
        {1.5, 2104224775u, 1974881085u},
        {1.6166666666666667, 2432500985u, 3453730667u},
        {1.625, 1792398058u, 167780633u},
        {1.6333333333333333, 1561509303u, 2646099112u},
        {1.75, 1068407816u, 1908444223u},
        {1.7916666666666667, 558390640u, 1153738145u},
        {1.8, 3785990880u, 853947888u},
        {1.8083333333333333, 3026402375u, 979669639u},
        {2, 2931743897u, 3881607820u},
        {2.2, 2001287598u, 12608277u},
        {2.5, 2593009906u, 2192188234u},
        {2.6, 3113589646u, 4287233138u},
        {2.7, 1015270404u, 2165922328u},
        {2.75, 3546373232u, 3367282981u},
        {2.8, 1542734032u, 2944859554u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.manySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 1543094216u},
        {0.008333333333333333, 2236050885u, 868969366u},
        {0.025, 2236050885u, 882146162u},
        {0.058333333333333334, 2236050885u, 3435033961u},
        {0.06666666666666667, 2236050885u, 857882397u},
        {0.075, 2236050885u, 3377262859u},
        {0.125, 340521870u, 3084337742u},
        {0.25, 2073438289u, 3877359019u},
        {0.35, 854717211u, 2219648348u},
        {0.5, 3799460325u, 1142010217u},
        {0.5416666666666666, 1129893435u, 1446655611u},
        {0.55, 3339638157u, 2668988487u},
        {0.5583333333333333, 2042057884u, 507416529u},
        {0.6333333333333333, 678697658u, 1547876483u},
        {0.75, 3525122988u, 526782444u},
        {0.8333333333333334, 2821694133u, 1318553271u},
        {0.8416666666666667, 2327121742u, 2163420105u},
        {0.9083333333333333, 2543331138u, 3155344321u},
        {0.9166666666666666, 2273771246u, 3574170347u},
        {0.95, 1751639971u, 1862259204u},
        {0.9583333333333334, 1850170282u, 1325779662u},
        {0.9833333333333333, 2451212660u, 3630854884u},
        {1, 1425035367u, 4157196958u},
        {1.075, 1702437518u, 300969681u},
        {1.0833333333333333, 523929651u, 3249092161u},
        {1.175, 158289011u, 3281560667u},
        {1.1833333333333333, 2169029545u, 1425034547u},
        {1.1916666666666667, 2259439038u, 582144182u},
        {1.3166666666666667, 2201409178u, 2179941098u},
        {1.325, 1552880183u, 1436737603u},
        {1.3333333333333333, 3909483218u, 4249940883u},
        {1.3416666666666666, 2318541523u, 845286807u},
        {1.35, 2340530608u, 1314732427u},
        {1.3583333333333334, 3104658082u, 4121040239u},
        {1.5, 697270861u, 1106824126u},
        {1.6166666666666667, 566977195u, 4073710161u},
        {1.625, 1052766102u, 1478699492u},
        {1.6333333333333333, 3053599546u, 921017918u},
        {1.75, 902420491u, 1840077560u},
        {1.7916666666666667, 1956699145u, 4294319591u},
        {1.8, 2577929030u, 2907622093u},
        {1.8083333333333333, 3882551311u, 1718508958u},
        {2, 3605479623u, 3520240350u},
        {2.2, 2203297226u, 1168155229u},
        {2.5, 857509411u, 3922799834u},
        {2.6, 291102538u, 2536210625u},
        {2.7, 1354183579u, 3397392039u},
        {2.75, 1178039445u, 1896623799u},
        {2.8, 3760489628u, 2242166176u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.colorzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 13447963u},
        {0.008333333333333333, 2236050885u, 2239758153u},
        {0.025, 2236050885u, 417584573u},
        {0.058333333333333334, 2236050885u, 2327351338u},
        {0.06666666666666667, 2236050885u, 454188197u},
        {0.075, 2236050885u, 2657223073u},
        {0.125, 3286883046u, 3565644921u},
        {0.25, 666304065u, 204512828u},
        {0.35, 3320099306u, 2051173410u},
        {0.5, 4104923643u, 3056349255u},
        {0.5416666666666666, 1995189793u, 1787789379u},
        {0.55, 282212171u, 4189907801u},
        {0.5583333333333333, 3760021150u, 2548963609u},
        {0.6333333333333333, 2918768743u, 1810461703u},
        {0.75, 4051644152u, 419814969u},
        {0.8333333333333334, 2459962410u, 3294195513u},
        {0.8416666666666667, 1631451820u, 4090578398u},
        {0.9083333333333333, 1297988898u, 1420004205u},
        {0.9166666666666666, 4229623594u, 3253705778u},
        {0.95, 990682441u, 3401051358u},
        {0.9583333333333334, 3566380598u, 3639249336u},
        {0.9833333333333333, 367405370u, 2004998767u},
        {1, 316322114u, 3024694553u},
        {1.075, 3718591521u, 4231676735u},
        {1.0833333333333333, 1808237400u, 874457935u},
        {1.175, 12791309u, 1863426866u},
        {1.1833333333333333, 3809938147u, 4034392592u},
        {1.1916666666666667, 328687913u, 3049646645u},
        {1.3166666666666667, 4040180745u, 1065041122u},
        {1.325, 2115610224u, 2449443176u},
        {1.3333333333333333, 3573005171u, 3288734661u},
        {1.3416666666666666, 1459719162u, 488132429u},
        {1.35, 150322809u, 2017465036u},
        {1.3583333333333334, 2826644568u, 480008674u},
        {1.5, 2245137771u, 1801989267u},
        {1.6166666666666667, 2404675889u, 3377950162u},
        {1.625, 2316845073u, 57685606u},
        {1.6333333333333333, 2715435974u, 2211741670u},
        {1.75, 2116356650u, 1360561811u},
        {1.7916666666666667, 948334403u, 1025163279u},
        {1.8, 348863858u, 1673822428u},
        {1.8083333333333333, 4089385688u, 3580709422u},
        {2, 3759799743u, 1850968355u},
        {2.2, 1816832637u, 182561742u},
        {2.5, 3682513732u, 570853590u},
        {2.6, 1366420333u, 2398191652u},
        {2.7, 191111662u, 2612956842u},
        {2.75, 1170373412u, 1368564915u},
        {2.8, 809829630u, 1597599367u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.colortransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 13447963u},
        {0.008333333333333333, 2236050885u, 2239758153u},
        {0.025, 2236050885u, 417584573u},
        {0.058333333333333334, 2236050885u, 2327351338u},
        {0.06666666666666667, 2236050885u, 454188197u},
        {0.075, 2236050885u, 2657223073u},
        {0.125, 3474372506u, 3565644921u},
        {0.25, 822853357u, 204512828u},
        {0.35, 1479394599u, 2051173410u},
        {0.5, 4200992588u, 3056349255u},
        {0.5416666666666666, 4130345600u, 1787789379u},
        {0.55, 3839423816u, 4189907801u},
        {0.5583333333333333, 4246854548u, 2548963609u},
        {0.6333333333333333, 1817027044u, 1810461703u},
        {0.75, 2968883866u, 419814969u},
        {0.8333333333333334, 2099277997u, 3294195513u},
        {0.8416666666666667, 2044556439u, 4090578398u},
        {0.9083333333333333, 1375380281u, 1420004205u},
        {0.9166666666666666, 1716154025u, 3253705778u},
        {0.95, 1903999342u, 3401051358u},
        {0.9583333333333334, 2259497659u, 3639249336u},
        {0.9833333333333333, 736080119u, 2004998767u},
        {1, 2635142304u, 3024694553u},
        {1.075, 956880309u, 4231676735u},
        {1.0833333333333333, 4270237320u, 874457935u},
        {1.175, 3295101884u, 1863426866u},
        {1.1833333333333333, 2174269787u, 4034392592u},
        {1.1916666666666667, 517878941u, 3049646645u},
        {1.3166666666666667, 2483038093u, 1065041122u},
        {1.325, 3520288472u, 2449443176u},
        {1.3333333333333333, 74946499u, 3288734661u},
        {1.3416666666666666, 3860137158u, 488132429u},
        {1.35, 1621625905u, 2017465036u},
        {1.3583333333333334, 1106305932u, 480008674u},
        {1.5, 892789779u, 1801989267u},
        {1.6166666666666667, 2918063045u, 3377950162u},
        {1.625, 2944048581u, 57685606u},
        {1.6333333333333333, 2313352706u, 2211741670u},
        {1.75, 690051334u, 1360561811u},
        {1.7916666666666667, 1421069382u, 1025163279u},
        {1.8, 249331921u, 1673822428u},
        {1.8083333333333333, 3194431435u, 3580709422u},
        {2, 3736285717u, 1850968355u},
        {2.2, 364675645u, 182561742u},
        {2.5, 3682513732u, 570853590u},
        {2.6, 1366420333u, 2398191652u},
        {2.7, 191111662u, 2612956842u},
        {2.75, 1170373412u, 1368564915u},
        {2.8, 809829630u, 1597599367u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.colorpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 13447963u},
        {0.008333333333333333, 2236050885u, 2239758153u},
        {0.025, 2236050885u, 417584573u},
        {0.058333333333333334, 2236050885u, 2327351338u},
        {0.06666666666666667, 2236050885u, 454188197u},
        {0.075, 2236050885u, 2657223073u},
        {0.125, 2967991570u, 3565644921u},
        {0.25, 754416261u, 204512828u},
        {0.35, 3502101312u, 2051173410u},
        {0.5, 1848978698u, 3056349255u},
        {0.5416666666666666, 4215423121u, 1787789379u},
        {0.55, 1165742063u, 4189907801u},
        {0.5583333333333333, 635226075u, 2548963609u},
        {0.6333333333333333, 3136661621u, 1810461703u},
        {0.75, 2861860052u, 419814969u},
        {0.8333333333333334, 1996824193u, 3294195513u},
        {0.8416666666666667, 4126277569u, 4090578398u},
        {0.9083333333333333, 1608408832u, 1420004205u},
        {0.9166666666666666, 1325822635u, 3253705778u},
        {0.95, 565837318u, 3401051358u},
        {0.9583333333333334, 3858861941u, 3639249336u},
        {0.9833333333333333, 1660557323u, 2004998767u},
        {1, 2869929568u, 3024694553u},
        {1.075, 537022531u, 4231676735u},
        {1.0833333333333333, 3733461760u, 874457935u},
        {1.175, 2803855983u, 1863426866u},
        {1.1833333333333333, 2038702113u, 4034392592u},
        {1.1916666666666667, 2586655345u, 3049646645u},
        {1.3166666666666667, 971275533u, 1065041122u},
        {1.325, 3824029420u, 2449443176u},
        {1.3333333333333333, 688824507u, 3288734661u},
        {1.3416666666666666, 534951807u, 488132429u},
        {1.35, 3402522943u, 2017465036u},
        {1.3583333333333334, 4026842542u, 480008674u},
        {1.5, 3956622483u, 1801989267u},
        {1.6166666666666667, 1327175175u, 3377950162u},
        {1.625, 405426247u, 57685606u},
        {1.6333333333333333, 2126481410u, 2211741670u},
        {1.75, 748935186u, 1360561811u},
        {1.7916666666666667, 538843361u, 1025163279u},
        {1.8, 2085704643u, 1673822428u},
        {1.8083333333333333, 3222367411u, 3580709422u},
        {2, 1003054701u, 1850968355u},
        {2.2, 4118156065u, 182561742u},
        {2.5, 3682513732u, 570853590u},
        {2.6, 1366420333u, 2398191652u},
        {2.7, 191111662u, 2612956842u},
        {2.75, 1170373412u, 1368564915u},
        {2.8, 809829630u, 1597599367u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.burnaway.colorwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 13447963u},
        {0.008333333333333333, 2236050885u, 2239758153u},
        {0.025, 2236050885u, 417584573u},
        {0.058333333333333334, 2236050885u, 2327351338u},
        {0.06666666666666667, 2236050885u, 454188197u},
        {0.075, 2236050885u, 2657223073u},
        {0.125, 3113764386u, 3565644921u},
        {0.25, 2893559773u, 204512828u},
        {0.35, 2507526508u, 2051173410u},
        {0.5, 3523197765u, 3056349255u},
        {0.5416666666666666, 3391104642u, 1787789379u},
        {0.55, 2336054449u, 4189907801u},
        {0.5583333333333333, 2354514104u, 2548963609u},
        {0.6333333333333333, 1321749518u, 1810461703u},
        {0.75, 2891357870u, 419814969u},
        {0.8333333333333334, 2377849803u, 3294195513u},
        {0.8416666666666667, 2481367715u, 4090578398u},
        {0.9083333333333333, 3959981314u, 1420004205u},
        {0.9166666666666666, 1982430190u, 3253705778u},
        {0.95, 2236157559u, 3401051358u},
        {0.9583333333333334, 2131902511u, 3639249336u},
        {0.9833333333333333, 2646153269u, 2004998767u},
        {1, 2765797215u, 3024694553u},
        {1.075, 1121507925u, 4231676735u},
        {1.0833333333333333, 1285763400u, 874457935u},
        {1.175, 99989609u, 1863426866u},
        {1.1833333333333333, 3632738538u, 4034392592u},
        {1.1916666666666667, 482033805u, 3049646645u},
        {1.3166666666666667, 2530337507u, 1065041122u},
        {1.325, 776744201u, 2449443176u},
        {1.3333333333333333, 1990309689u, 3288734661u},
        {1.3416666666666666, 181359725u, 488132429u},
        {1.35, 617536747u, 2017465036u},
        {1.3583333333333334, 881502107u, 480008674u},
        {1.5, 4088389631u, 1801989267u},
        {1.6166666666666667, 573052431u, 3377950162u},
        {1.625, 3791060654u, 57685606u},
        {1.6333333333333333, 722554134u, 2211741670u},
        {1.75, 2141838120u, 1360561811u},
        {1.7916666666666667, 3763301748u, 1025163279u},
        {1.8, 4040019833u, 1673822428u},
        {1.8083333333333333, 1893896012u, 3580709422u},
        {2, 4215522898u, 1850968355u},
        {2.2, 3373968617u, 182561742u},
        {2.5, 3682513732u, 570853590u},
        {2.6, 1366420333u, 2398191652u},
        {2.7, 191111662u, 2612956842u},
        {2.75, 1170373412u, 1368564915u},
        {2.8, 809829630u, 1597599367u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::BurnAway);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::BurnAway, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.varSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 2136066941u, 2014099411u},
        {0.06666666666666667, 2780346093u, 566415830u},
        {0.075, 3554186435u, 2706366666u},
        {0.125, 2948731649u, 2534454274u},
        {0.25, 2866760589u, 3941091882u},
        {0.35, 2569272060u, 1901394524u},
        {0.5, 3595446164u, 902142332u},
        {0.5416666666666666, 1531673724u, 3872470092u},
        {0.55, 821012530u, 3522860329u},
        {0.5583333333333333, 1576633761u, 3967954799u},
        {0.6333333333333333, 1558720303u, 3542962921u},
        {0.75, 3005207937u, 1807293017u},
        {0.8333333333333334, 659684342u, 143352076u},
        {0.8416666666666667, 2304320105u, 2369837864u},
        {0.9083333333333333, 404280513u, 3455538460u},
        {0.9166666666666666, 1019115341u, 1048573037u},
        {0.95, 1181856751u, 1467318118u},
        {0.9583333333333334, 878660261u, 2659939818u},
        {0.9833333333333333, 4292816823u, 331123038u},
        {1, 4152735468u, 3460602169u},
        {1.075, 1551557720u, 3518422891u},
        {1.0833333333333333, 246954424u, 1185214273u},
        {1.175, 3126942201u, 2222071215u},
        {1.1833333333333333, 2416651983u, 2963090971u},
        {1.1916666666666667, 3908027273u, 4018919258u},
        {1.3166666666666667, 1677592005u, 2236050885u},
        {1.325, 2670013893u, 684424334u},
        {1.3333333333333333, 2666082736u, 254535492u},
        {1.3416666666666666, 4020407184u, 4279670424u},
        {1.35, 253284371u, 638944669u},
        {1.3583333333333334, 4089521134u, 904452124u},
        {1.5, 3876204133u, 2425942747u},
        {1.6166666666666667, 3599692308u, 2331344148u},
        {1.625, 2778894885u, 3161298272u},
        {1.6333333333333333, 745473593u, 2515749193u},
        {1.75, 3686966832u, 69776852u},
        {1.7916666666666667, 3240923727u, 735409095u},
        {1.8, 234876978u, 2200761882u},
        {1.8083333333333333, 3632363350u, 2593939904u},
        {2, 3151526997u, 76584176u},
        {2.2, 847760274u, 3350536007u},
        {2.5, 2062387586u, 4025380231u},
        {2.6, 2731995270u, 1826607890u},
        {2.7, 3344666537u, 696734696u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(4293980400)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.zeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 427258451u, 1545084428u},
        {0.25, 1664545383u, 3942623096u},
        {0.35, 4259688131u, 1392951900u},
        {0.5, 1835491051u, 2544697148u},
        {0.5416666666666666, 2728215403u, 3770940347u},
        {0.55, 126757687u, 1123787506u},
        {0.5583333333333333, 1985247210u, 1720641034u},
        {0.6333333333333333, 3756112563u, 1750949166u},
        {0.75, 2617238487u, 2793589377u},
        {0.8333333333333334, 846379490u, 2528286465u},
        {0.8416666666666667, 2783752888u, 2442338183u},
        {0.9083333333333333, 1064908308u, 2217355968u},
        {0.9166666666666666, 572033466u, 2740606574u},
        {0.95, 4226384083u, 549576029u},
        {0.9583333333333334, 1686357877u, 992996970u},
        {0.9833333333333333, 2029877155u, 1127458477u},
        {1, 1061183550u, 3348746620u},
        {1.075, 236677399u, 3984255710u},
        {1.0833333333333333, 2120393729u, 1992729689u},
        {1.175, 2734507360u, 553743004u},
        {1.1833333333333333, 1528019521u, 2064436316u},
        {1.1916666666666667, 3822483815u, 1020382911u},
        {1.3166666666666667, 1678891761u, 946761742u},
        {1.325, 4230351320u, 3848530140u},
        {1.3333333333333333, 2108822694u, 3665666246u},
        {1.3416666666666666, 465312677u, 1677467930u},
        {1.35, 2210680573u, 1385787788u},
        {1.3583333333333334, 2869636852u, 1558016955u},
        {1.5, 4069436015u, 1746039751u},
        {1.6166666666666667, 1330392896u, 2905826816u},
        {1.625, 1486899128u, 1228350822u},
        {1.6333333333333333, 422857443u, 2678276542u},
        {1.75, 746045562u, 2953433114u},
        {1.7916666666666667, 754733356u, 1094554340u},
        {1.8, 1147494376u, 2016501423u},
        {1.8083333333333333, 328199996u, 3772096449u},
        {2, 2345831222u, 691009578u},
        {2.2, 3236157823u, 3767343700u},
        {2.5, 2440669959u, 791009144u},
        {2.6, 2471565257u, 1309044052u},
        {2.7, 2972547639u, 18144966u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.maxSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 427258451u, 1545084428u},
        {0.25, 1664545383u, 3942623096u},
        {0.35, 4259688131u, 1392951900u},
        {0.5, 1835491051u, 2544697148u},
        {0.5416666666666666, 2728215403u, 3770940347u},
        {0.55, 3559484582u, 2712450103u},
        {0.5583333333333333, 4268419343u, 3977707644u},
        {0.6333333333333333, 1206499907u, 3253692578u},
        {0.75, 2390291004u, 2720637026u},
        {0.8333333333333334, 1796836920u, 764230923u},
        {0.8416666666666667, 2985703917u, 723999918u},
        {0.9083333333333333, 419890022u, 1707078573u},
        {0.9166666666666666, 3222893345u, 1066176915u},
        {0.95, 3599022794u, 3840072565u},
        {0.9583333333333334, 3114612845u, 3088893737u},
        {0.9833333333333333, 20591421u, 4233996827u},
        {1, 936739647u, 1838977694u},
        {1.075, 3469962665u, 2695705999u},
        {1.0833333333333333, 1087017171u, 3885494466u},
        {1.175, 2349833429u, 1356989277u},
        {1.1833333333333333, 944366693u, 3223968861u},
        {1.1916666666666667, 3011802788u, 2490894821u},
        {1.3166666666666667, 1707440289u, 921439656u},
        {1.325, 3323559321u, 895159137u},
        {1.3333333333333333, 1040806862u, 1463916943u},
        {1.3416666666666666, 2821156352u, 2962988211u},
        {1.35, 2000875679u, 2994897316u},
        {1.3583333333333334, 3781541596u, 3662437588u},
        {1.5, 1463705210u, 2888644642u},
        {1.6166666666666667, 490044257u, 2092838485u},
        {1.625, 1170372593u, 1389991146u},
        {1.6333333333333333, 3236740932u, 2336927156u},
        {1.75, 3125849239u, 2428564980u},
        {1.7916666666666667, 2401298881u, 587725738u},
        {1.8, 3048739715u, 1320967110u},
        {1.8083333333333333, 63045867u, 3479647109u},
        {2, 1496969064u, 15334350u},
        {2.2, 3611569600u, 4003130974u},
        {2.5, 3416555539u, 4207347844u},
        {2.6, 118098122u, 1536854264u},
        {2.7, 709271468u, 3027722649u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.singleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 3104284637u, 3168124989u},
        {0.06666666666666667, 3077671389u, 1686294134u},
        {0.075, 2786971869u, 3513666907u},
        {0.125, 2373217957u, 4031686878u},
        {0.25, 2234940837u, 1329299950u},
        {0.35, 948292066u, 201903470u},
        {0.5, 3991709594u, 170706222u},
        {0.5416666666666666, 3884910378u, 647143828u},
        {0.55, 2850196657u, 2243897461u},
        {0.5583333333333333, 45594045u, 679005242u},
        {0.6333333333333333, 1643144006u, 820606333u},
        {0.75, 115163413u, 979103498u},
        {0.8333333333333334, 1313907153u, 985829756u},
        {0.8416666666666667, 3484915994u, 2266428070u},
        {0.9083333333333333, 297985425u, 665560906u},
        {0.9166666666666666, 4095966146u, 2772010156u},
        {0.95, 233838109u, 2434528653u},
        {0.9583333333333334, 1607447397u, 3401348734u},
        {0.9833333333333333, 2482057745u, 3289335179u},
        {1, 3248531951u, 1698878517u},
        {1.075, 1631294573u, 2315641248u},
        {1.0833333333333333, 3429183662u, 1984780827u},
        {1.175, 4280685860u, 2637068790u},
        {1.1833333333333333, 3170358373u, 4086518757u},
        {1.1916666666666667, 785687113u, 2763915879u},
        {1.3166666666666667, 802096957u, 768369458u},
        {1.325, 1347557421u, 1325801899u},
        {1.3333333333333333, 2038774853u, 1173522213u},
        {1.3416666666666666, 1999227413u, 2721868074u},
        {1.35, 944814455u, 781240406u},
        {1.3583333333333334, 3888152678u, 3426290313u},
        {1.5, 2840801808u, 3591499228u},
        {1.6166666666666667, 3282687563u, 1578866686u},
        {1.625, 3806908209u, 3212487650u},
        {1.6333333333333333, 1001182615u, 1941648513u},
        {1.75, 1713162047u, 1800732373u},
        {1.7916666666666667, 910341460u, 1171342488u},
        {1.8, 1270058278u, 3680010625u},
        {1.8083333333333333, 213182689u, 3719204960u},
        {2, 1515806510u, 2887913681u},
        {2.2, 1029033424u, 3040745934u},
        {2.5, 3085469500u, 2893180756u},
        {2.6, 2964552707u, 914293262u},
        {2.7, 1797218593u, 369829584u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.manySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 815488069u, 1924583527u},
        {0.06666666666666667, 2103552925u, 3734107046u},
        {0.075, 844525887u, 709685047u},
        {0.125, 1092056512u, 3696744391u},
        {0.25, 950517896u, 3716321775u},
        {0.35, 881628664u, 2069026385u},
        {0.5, 4082249184u, 1895415025u},
        {0.5416666666666666, 3124719112u, 1087550288u},
        {0.55, 836462200u, 1202044914u},
        {0.5583333333333333, 1880369830u, 429648253u},
        {0.6333333333333333, 3563628585u, 3055550402u},
        {0.75, 610658251u, 1306946707u},
        {0.8333333333333334, 2709435670u, 1111872755u},
        {0.8416666666666667, 3657651261u, 2038825970u},
        {0.9083333333333333, 108932561u, 2888535821u},
        {0.9166666666666666, 1395549262u, 760262504u},
        {0.95, 3074483994u, 3420101548u},
        {0.9583333333333334, 2371953413u, 3897156482u},
        {0.9833333333333333, 751725871u, 2921974817u},
        {1, 2709000343u, 4289236499u},
        {1.075, 2113499323u, 2704776970u},
        {1.0833333333333333, 621158464u, 722680484u},
        {1.175, 1947995523u, 2449223244u},
        {1.1833333333333333, 716216032u, 1295148416u},
        {1.1916666666666667, 4219022673u, 3484621895u},
        {1.3166666666666667, 2912324765u, 712979503u},
        {1.325, 410458589u, 1117884943u},
        {1.3333333333333333, 4164142962u, 1740215445u},
        {1.3416666666666666, 1733619507u, 1208497613u},
        {1.35, 431587926u, 1760178101u},
        {1.3583333333333334, 388713786u, 3942197989u},
        {1.5, 2962882046u, 402854962u},
        {1.6166666666666667, 42830249u, 755633357u},
        {1.625, 1418918784u, 68593087u},
        {1.6333333333333333, 1176043910u, 1701388036u},
        {1.75, 4143093642u, 3692715574u},
        {1.7916666666666667, 4062842452u, 971260827u},
        {1.8, 1206731179u, 2303298003u},
        {1.8083333333333333, 1253591460u, 4022004933u},
        {2, 123973138u, 2037149861u},
        {2.2, 406906749u, 3648171902u},
        {2.5, 3728970915u, 273566712u},
        {2.6, 3324928066u, 3766424893u},
        {2.7, 4249959065u, 3801673208u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.colorzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 1335012959u, 1545084428u},
        {0.25, 3875356431u, 3942623096u},
        {0.35, 3781671016u, 1392951900u},
        {0.5, 1520644184u, 2544697148u},
        {0.5416666666666666, 4017441016u, 3770940347u},
        {0.55, 817411590u, 2365632871u},
        {0.5583333333333333, 1454894791u, 1694180609u},
        {0.6333333333333333, 3472630790u, 247258437u},
        {0.75, 3598586276u, 3557219044u},
        {0.8333333333333334, 2635930599u, 2347981963u},
        {0.8416666666666667, 827081156u, 4274561281u},
        {0.9083333333333333, 288219483u, 3389163785u},
        {0.9166666666666666, 3543389850u, 200083394u},
        {0.95, 457510159u, 73185466u},
        {0.9583333333333334, 2062691170u, 3495216587u},
        {0.9833333333333333, 1302264441u, 1035826118u},
        {1, 1390322262u, 2768988929u},
        {1.075, 3290913662u, 300907715u},
        {1.0833333333333333, 1062059973u, 1011270279u},
        {1.175, 3978014038u, 1727483207u},
        {1.1833333333333333, 262897452u, 678111065u},
        {1.1916666666666667, 91184323u, 2927922796u},
        {1.3166666666666667, 3239634013u, 1787120613u},
        {1.325, 2025867621u, 588917966u},
        {1.3333333333333333, 2528944056u, 3167602416u},
        {1.3416666666666666, 1507482776u, 697109710u},
        {1.35, 1185083173u, 2964167107u},
        {1.3583333333333334, 2500907974u, 268313893u},
        {1.5, 1397217831u, 3780327918u},
        {1.6166666666666667, 242704320u, 880663137u},
        {1.625, 889150755u, 1763798365u},
        {1.6333333333333333, 1684122886u, 1468505676u},
        {1.75, 3909959400u, 2113309669u},
        {1.7916666666666667, 778606520u, 3707030549u},
        {1.8, 3931510300u, 4103261328u},
        {1.8083333333333333, 432720602u, 4051818890u},
        {2, 2949948889u, 3183372669u},
        {2.2, 1273616996u, 2398529717u},
        {2.5, 3654480391u, 3460542012u},
        {2.6, 3839252497u, 3223613345u},
        {2.7, 2805115825u, 1796607779u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.colortransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 3848931079u, 1545084428u},
        {0.25, 1403507067u, 3942623096u},
        {0.35, 1532351180u, 1392951900u},
        {0.5, 3218490640u, 2544697148u},
        {0.5416666666666666, 990418728u, 3770940347u},
        {0.55, 95946778u, 2365632871u},
        {0.5583333333333333, 2518553131u, 1694180609u},
        {0.6333333333333333, 2010115358u, 247258437u},
        {0.75, 2915952568u, 3557219044u},
        {0.8333333333333334, 544136283u, 2347981963u},
        {0.8416666666666667, 3689381176u, 4274561281u},
        {0.9083333333333333, 675154231u, 3389163785u},
        {0.9166666666666666, 399137914u, 200083394u},
        {0.95, 2424769007u, 73185466u},
        {0.9583333333333334, 3389022266u, 3495216587u},
        {0.9833333333333333, 824820625u, 1035826118u},
        {1, 2028526782u, 2768988929u},
        {1.075, 3096419822u, 300907715u},
        {1.0833333333333333, 1703847457u, 1011270279u},
        {1.175, 3765922530u, 1727483207u},
        {1.1833333333333333, 1556817792u, 678111065u},
        {1.1916666666666667, 597399179u, 2927922796u},
        {1.3166666666666667, 543701141u, 1787120613u},
        {1.325, 332761765u, 588917966u},
        {1.3333333333333333, 600099580u, 3167602416u},
        {1.3416666666666666, 1410420656u, 697109710u},
        {1.35, 3540788897u, 2964167107u},
        {1.3583333333333334, 3990522790u, 268313893u},
        {1.5, 827360415u, 3780327918u},
        {1.6166666666666667, 114384176u, 880663137u},
        {1.625, 2414171642u, 1763798365u},
        {1.6333333333333333, 3507548959u, 1468505676u},
        {1.75, 132254190u, 2113309669u},
        {1.7916666666666667, 51112158u, 3707030549u},
        {1.8, 3234484391u, 4103261328u},
        {1.8083333333333333, 4061636040u, 4051818890u},
        {2, 880311253u, 3183372669u},
        {2.2, 2746992288u, 2398529717u},
        {2.5, 1766445124u, 3460542012u},
        {2.6, 1656965098u, 3223613345u},
        {2.7, 2805115825u, 1796607779u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.colorpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 3589643687u, 1545084428u},
        {0.25, 102476687u, 3942623096u},
        {0.35, 3123766719u, 1392951900u},
        {0.5, 1504789071u, 2544697148u},
        {0.5416666666666666, 2038203239u, 3770940347u},
        {0.55, 157844806u, 2365632871u},
        {0.5583333333333333, 2585150052u, 1694180609u},
        {0.6333333333333333, 3576265385u, 247258437u},
        {0.75, 3341936572u, 3557219044u},
        {0.8333333333333334, 2977299600u, 2347981963u},
        {0.8416666666666667, 3355822219u, 4274561281u},
        {0.9083333333333333, 4227945332u, 3389163785u},
        {0.9166666666666666, 2964110554u, 200083394u},
        {0.95, 3596024547u, 73185466u},
        {0.9583333333333334, 2171232465u, 3495216587u},
        {0.9833333333333333, 2798575973u, 1035826118u},
        {1, 4089949919u, 2768988929u},
        {1.075, 1137562298u, 300907715u},
        {1.0833333333333333, 3223884810u, 1011270279u},
        {1.175, 2516697132u, 1727483207u},
        {1.1833333333333333, 301316028u, 678111065u},
        {1.1916666666666667, 75711775u, 2927922796u},
        {1.3166666666666667, 4132117365u, 1787120613u},
        {1.325, 2296685445u, 588917966u},
        {1.3333333333333333, 603951100u, 3167602416u},
        {1.3416666666666666, 1743639632u, 697109710u},
        {1.35, 1986060283u, 2964167107u},
        {1.3583333333333334, 1055057667u, 268313893u},
        {1.5, 1212492469u, 3780327918u},
        {1.6166666666666667, 4080785567u, 880663137u},
        {1.625, 4195922973u, 1763798365u},
        {1.6333333333333333, 2012959256u, 1468505676u},
        {1.75, 2374907917u, 2113309669u},
        {1.7916666666666667, 4273352799u, 3707030549u},
        {1.8, 3920452679u, 4103261328u},
        {1.8083333333333333, 1389570669u, 4051818890u},
        {2, 2973114404u, 3183372669u},
        {2.2, 3545096750u, 2398529717u},
        {2.5, 2285743505u, 3460542012u},
        {2.6, 3514944802u, 3223613345u},
        {2.7, 2805115825u, 1796607779u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionFinal.shatterwipe.colorwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.058333333333333334, 500671461u, 4057676837u},
        {0.06666666666666667, 1992187725u, 1990854480u},
        {0.075, 3555003604u, 149116779u},
        {0.125, 456945579u, 1545084428u},
        {0.25, 4181074275u, 3942623096u},
        {0.35, 3440838874u, 1392951900u},
        {0.5, 536482962u, 2544697148u},
        {0.5416666666666666, 2051658374u, 3770940347u},
        {0.55, 2581279422u, 2365632871u},
        {0.5583333333333333, 2736613883u, 1694180609u},
        {0.6333333333333333, 1765728756u, 247258437u},
        {0.75, 1084978188u, 3557219044u},
        {0.8333333333333334, 3554554835u, 2347981963u},
        {0.8416666666666667, 2430722134u, 4274561281u},
        {0.9083333333333333, 4265586247u, 3389163785u},
        {0.9166666666666666, 3744570472u, 200083394u},
        {0.95, 1647264079u, 73185466u},
        {0.9583333333333334, 1775648094u, 3495216587u},
        {0.9833333333333333, 4075727841u, 1035826118u},
        {1, 4025147427u, 2768988929u},
        {1.075, 1464540718u, 300907715u},
        {1.0833333333333333, 1143061654u, 1011270279u},
        {1.175, 841966716u, 1727483207u},
        {1.1833333333333333, 4033797172u, 678111065u},
        {1.1916666666666667, 828942999u, 2927922796u},
        {1.3166666666666667, 280229477u, 1787120613u},
        {1.325, 1683604957u, 588917966u},
        {1.3333333333333333, 4246346192u, 3167602416u},
        {1.3416666666666666, 4053529192u, 697109710u},
        {1.35, 3657432924u, 2964167107u},
        {1.3583333333333334, 3481456330u, 268313893u},
        {1.5, 1401742016u, 3780327918u},
        {1.6166666666666667, 1047153746u, 880663137u},
        {1.625, 4238502143u, 1763798365u},
        {1.6333333333333333, 2469357013u, 1468505676u},
        {1.75, 2846187765u, 2113309669u},
        {1.7916666666666667, 3068403866u, 3707030549u},
        {1.8, 2271836180u, 4103261328u},
        {1.8083333333333333, 779313417u, 4051818890u},
        {2, 1670482548u, 3183372669u},
        {2.2, 2029245796u, 2398529717u},
        {2.5, 1519635930u, 3460542012u},
        {2.6, 4148590246u, 3223613345u},
        {2.7, 2805115825u, 1796607779u},
        {2.75, 2236050885u, 2236050885u},
        {2.8, 2236050885u, 2236050885u},
        {2.95, 2236050885u, 2236050885u},
        {3, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::ShatterWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterWipe, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
}  // namespace test_pixelfx_transition_final_unit
