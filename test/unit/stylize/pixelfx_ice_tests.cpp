// Ice-family projection and independently layered motes; the core suite exceeds 5000 lines.
#include <algorithm>
#include <array>
#include "PixelFxColorReference.h"
#include "PixelFxFrameDiagnostic.h"
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Ice.h"
#include "stylize/pixelfx/IceFractures.h"
#include "stylize/pixelfx/IcePanes.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_ice_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.ice.crystalReferenceAndConfiguration") {
    constexpr std::array<int, 9>       times{{0, 1, 3, 15, 30, 45, 60, 89, 90}};
    constexpr std::array<uint32_t, 18> expected{{1729050598u, 692977207u, 3233957426u, 446953948u, 1824529328u,
                                                 3836781054u, 3038899278u, 934709952u, 3302865699u, 230805031u,
                                                 4024399917u, 163513178u, 3726110101u, 3667200961u, 2379276463u,
                                                 3077429313u, 1729050598u, 692977207u}};
    PixelFx                            fx(PixelFxKind::IceCrystal), other(PixelFxKind::IceCrystal);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i] / 30.);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.5);
    other.render(.5);
    const auto original = checksum(fx.color());
    auto       old      = fx.prepareSeek(.5);
    REQUIRE(old);
    auto settings       = fx.settings();
    settings.ice.height = 47.5;
    settings.seed       = 123;
    settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared       = PixelFx::prepareConfiguration(PixelFxKind::IceCrystal, settings);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.5);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(checksum(other.color()) == original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().ice.height == 47.5);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(-.5));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IceCrystal, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::IceCrystal, settings));
    fx.render(1e12);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    other.render(.75);
    // Restore the original configuration before comparing injected advancement.
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::IceCrystal, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.75));
    fx.draw();
    REQUIRE(checksum(fx.color()) == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.ice.snowflakeBurstReferenceAndTransactions") {
    constexpr std::array<int, 9>       times{{0, 3, 5, 6, 12, 30, 60, 90, 96}};
    constexpr std::array<uint32_t, 18> expected{{2995109921u, 2701180965u, 1809874826u, 3417266275u, 2971738361u,
                                                 1002214407u, 672842248u, 367599582u, 64177667u, 3774802988u,
                                                 419896618u, 2865839290u, 1854954309u, 1381931711u, 2543633573u,
                                                 126224329u, 1973972933u, 1973972933u}};
    PixelFx                            fx(PixelFxKind::SnowflakeBurst), other(PixelFxKind::SnowflakeBurst);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        auto seek = fx.prepareSeek(times[i] / 30.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(fx.advance(.75));
    for (int i = 0; i < 90; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto old = fx.prepareSeek(.75);
    REQUIRE(old);
    auto config = fx.configurationValue();
    config.find("options")->set("count", eve::Value(int64_t(13)));
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::SnowflakeBurst, config);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().ice.count == 13);
    REQUIRE(fx.advance(.75));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    for (double count : {0., -1., 129., 1.5}) {
        auto invalid = fx.configurationValue();
        invalid.find("options")->set("count", eve::Value(count));
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::SnowflakeBurst, invalid));
        REQUIRE(checksum(fx.color()) == changed);
    }
    auto upper      = fx.settings();
    upper.ice.count = 128;
    auto maximum    = PixelFx::prepareConfiguration(PixelFxKind::SnowflakeBurst, upper);
    REQUIRE(maximum);
    upper.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SnowflakeBurst, upper));
    auto one      = fx.settings();
    one.ice.count = 1;
    auto minimum  = PixelFx::prepareConfiguration(PixelFxKind::SnowflakeBurst, one);
    REQUIRE(minimum);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

// Pinned source blade fixtures cover optional looks used by other ice effects.
TEST_CASE("stylize.pixelfx.ice.bladeSourceShadingAndTransforms") {
    using namespace eve::stylize::pixelfx;
    constexpr std::array<uint32_t, 32> expected{
        {3856245406u, 1261520325u, 3094064485u, 1261520325u, 525107942u,  1261520325u, 9307750u,    1261520325u,
         450550916u,  1261520325u, 3038698305u, 1261520325u, 2551850987u, 1261520325u, 539614688u,  1261520325u,
         3523466815u, 1261520325u, 2056527229u, 1261520325u, 1978714666u, 1261520325u, 1700334943u, 1261520325u,
         50327216u,   1261520325u, 557152546u,  1261520325u, 3252366364u, 1261520325u, 3016544251u, 1261520325u}};
    const PixelFxInfo                info{PixelFxKind::IceLance, "fixture", 120, 112, 60, 96, .8, true};
    PixelFx                          source(PixelFxKind::IceLance);
    std::array<ice::BladeOptions, 8> options{};
    options[1].jag   = .45;
    options[2].dim   = 1;
    options[3].glint = .6;
    options[4].alpha = .45;
    options[5].white = true;
    options[6].jag   = .45;
    options[6].dim   = 2;
    options[6].glint = .35;
    options[7].jag   = .7;
    options[7].alpha = .7;
    size_t index     = 0;
    for (bool transform : {false, true})
        for (const auto& o : options) {
            std::vector<uint32_t> colors(120 * 112), lights(120 * 112);
            Paint                 p{info, colors, lights, 1};
            if (transform) {
                p.tx = 7;
                p.ty = -3;
                p.fx = -1;
                p.cs = std::cos(.2);
                p.sn = std::sin(.2);
            }
            ice::blade(p, -3.5, -2.25, 12.75, -49.5, 16.3, source.settings().palette, o);
            REQUIRE(checksum(colors) == expected[index++]);
            REQUIRE(checksum(lights) == expected[index++]);
        }
}
TEST_CASE("stylize.pixelfx.ice.lanceReferenceAndTransactions") {
    constexpr std::array<int, 7>       ticks{{0, 24, 36, 60, 72, 84, 96}};
    constexpr std::array<uint32_t, 14> expected{{212754883u, 1774025249u, 96430811u, 3446763282u, 2612427852u,
                                                 368173021u, 213211692u, 2182656769u, 4214589946u, 2272111273u,
                                                 3174113421u, 3152284223u, 212754883u, 1774025249u}};
    PixelFx                            fx(PixelFxKind::IceLance), other(PixelFxKind::IceLance);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < ticks.size(); ++i) {
        fx.render(ticks[i] / 120.);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    // Identical exp inputs differ by one ULP on V8 and the Windows CRT. Preserve
    // the source pixel's coverage and alpha, and bound only RGB quantization.
    fx.render(1. / 120);
    const uint32_t               pixel = fx.color()[31 * 176 + 100];
    constexpr std::array<int, 4> sourcePixel{{220, 251, 255, 183}};
    for (int channel = 0; channel < 4; ++channel) {
        const int actual = int((pixel >> (channel * 8)) & 255u);
        REQUIRE(std::abs(actual - sourcePixel[size_t(channel)]) <= (channel == 3 ? 0 : 1));
    }
    fx.render(.5);
    other.render(.5);
    const auto original = checksum(fx.color());
    auto       old      = fx.prepareSeek(.5);
    REQUIRE(old);
    auto settings    = fx.settings();
    settings.length  = 55.5;
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::IceLance, settings);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.5);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().length == 55.5);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    fx.render(.5);
    REQUIRE(checksum(fx.color()) == changed);
    for (double length : {-1., 0., 15.5, 256.5}) {
        auto invalid = fx.configurationValue();
        invalid.find("options")->set("length", eve::Value(length));
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IceLance, invalid));
        REQUIRE(checksum(fx.color()) == changed);
    }
    fx.render(1e12);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    const auto atLimit = checksum(fx.color());
    REQUIRE(!fx.advance(.31));
    REQUIRE(checksum(fx.color()) == atLimit);
    fx.render(.5);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.ice.panesSourcePhysicsEventsAndRaster") {
    using namespace eve::stylize::pixelfx;
    constexpr std::array<uint32_t, 28> expected{
        {385839905u,  2739302316u, 4285237274u, 2909584803u, 4128849691u, 1528131844u, 1856876512u,
         2421252990u, 3634321067u, 4093142073u, 4268017514u, 3256012270u, 4268017514u, 1212138674u,
         1037742229u, 161752658u,  1738214734u, 2759216877u, 3481959209u, 3971698882u, 1043307175u,
         3365075172u, 1718652429u, 2968339211u, 2596682791u, 4034971627u, 2596682791u, 2997341443u}};
    constexpr std::array<std::array<double, 6>, 26> events{
        {{{1.0, 0.0, 30.0415625, 0.0, 187.44687499999998, 1.0}},
         {{1.0, 2.0, 12.0, 0.0, 22.0, 0.0}},
         {{2.0, 2.0, 12.183333333333334, 0.0, 20.71666666666667, 0.0}},
         {{3.0, 2.0, 12.355972222222222, 0.0, 19.508194444444445, 0.0}},
         {{4.0, 2.0, 12.518540509259259, 0.0, 18.370216435185185, 0.0}},
         {{5.0, 2.0, 12.671625646219136, 0.0, 17.29862047646605, 0.0}},
         {{6.0, 2.0, 12.815780816856353, 0.0, 16.28953428200553, 0.0}},
         {{7.0, 2.0, 12.951526935873066, 0.0, 15.339311448888541, 0.0}},
         {{8.0, 1.0, 13.07935453128047, -7.007652138234898, 14.444518281036709, 0.0}},
         {{30.0, 0.0, -27.60813858440727, 0.0, 235.93669463300682, 1.0}},
         {{32.0, 0.0, -35.60136046066435, 0.0, 247.76342192095998, 1.0}},
         {{38.0, 0.0, -2.983053687188736, 0.0, 146.03274417617612, 1.0}},
         {{50.0, 2.0, -29.219829908489157, 0.0, -9.9514149586805, 1.0}},
         {{51.0, 2.0, -29.302758366478162, 0.0, -9.37091575275747, 1.0}},
         {{52.0, 2.0, -29.380849331084473, 0.0, -8.824279000513284, 1.0}},
         {{52.0, 0.0, -32.79144214616575, 0.0, 71.75937045176272, 2.0}},
         {{53.0, 2.0, -29.454384989422085, 0.0, -8.30952939215001, 1.0}},
         {{59.0, 2.0, -32.17951080064885, 0.0, 12.162167722626416, 2.0}},
         {{60.0, 2.0, -32.078159402960296, 0.0, 11.452707938806542, 2.0}},
         {{61.0, 2.0, -31.982720170136908, 0.0, 10.784633309042826, 2.0}},
         {{62.0, 2.0, -31.892848225894884, 0.0, 10.155529699348662, 2.0}},
         {{63.0, 2.0, -31.808218811733646, 0.0, 9.56312380021999, 2.0}},
         {{64.0, 2.0, -31.72852611339848, 0.0, 9.005274911873824, 2.0}},
         {{65.0, 2.0, -31.653482155799534, 0.0, 8.479967208681185, 2.0}},
         {{68.0, 1.0, -30.15958344827688, -5.167012739073753, -3.373140180166456, 1.0}},
         {{73.0, 1.0, -31.19104429019939, -6.585979257190951, 5.242902149480211, 2.0}}}};
    const PixelFxInfo info{PixelFxKind::IceSpikes, "fixture", 192, 128, 96, 112, 1, true};
    PixelFx           source(PixelFxKind::IceSpikes);
    for (bool transform : {false, true}) {
        ice::Panes ps;
        const auto initial = [](int i) {
            ice::Pane q;
            q.pts = {{-5, -3}, {6, -2}, {0, 7}};
            if (i == 0) {
                q.x       = -42;
                q.y       = -40;
                q.vx      = 25;
                q.vy      = 5;
                q.a       = .2;
                q.va      = 3;
                q.f       = .6;
                q.vf      = 4;
                q.life    = 1.8;
                q.crumble = .12;
            }
            if (i == 1) {
                q.x       = -24;
                q.y       = -35;
                q.vx      = -15;
                q.vy      = 8;
                q.a       = -.5;
                q.va      = -2;
                q.f       = .3;
                q.vf      = 3;
                q.lit     = .75;
                q.solid   = true;
                q.ridge   = -1.2;
                q.life    = 1.8;
                q.crumble = .15;
            }
            if (i == 2) {
                q.x     = -6;
                q.y     = -30;
                q.vx    = 10;
                q.vy    = 3;
                q.a     = .3;
                q.va    = 2;
                q.f     = 1.4;
                q.vf    = 2;
                q.g     = .5;
                q.solid = true;
                q.tone  = .4;
                q.life  = 1.8;
            }
            if (i == 3) {
                q.x       = 12;
                q.y       = -.5;
                q.vx      = 22;
                q.a       = 0;
                q.va      = 4;
                q.f       = .2;
                q.vf      = 3;
                q.rest    = true;
                q.crumble = .06;
                q.life    = 1;
            }
            if (i == 4) {
                q.x       = 30;
                q.y       = -5;
                q.vx      = 5;
                q.vy      = 180;
                q.a       = .2;
                q.va      = 2;
                q.f       = 0;
                q.vf      = 2;
                q.brittle = 1;
                q.bounce  = 1;
                q.life    = 1.4;
            }
            if (i == 5) {
                q.x    = 48;
                q.y    = -30;
                q.vx   = -5;
                q.vy   = 4;
                q.a    = .3;
                q.va   = 1;
                q.f    = .3;
                q.vf   = 1;
                q.g    = 0;
                q.life = .3;
            }
            return q;
        };
        for (int i = 0; i < 6; ++i) ps.add(initial(i));
        int        tick  = 0;
        size_t     event = 0, row = transform ? 14 : 0;
        const auto observe = [&](int kind, const ice::Pane& q, double x, double y, double v) {
            REQUIRE(event < events.size());
            const std::array<double, 6> actual{{double(tick), double(kind), x, y, v, double(q.hits)}};
            for (size_t j = 0; j < actual.size(); ++j) REQUIRE(std::abs(actual[j] - events[event][j]) < 1e-9);
            ++event;
        };
        for (int target : {0, 1, 12, 30, 60, 90, 120}) {
            while (tick < target) {
                ++tick;
                ps.update(
                    1. / 120, [](double) { return 0.; },
                    [&](const ice::Pane& q, double x, double y, double v) { observe(0, q, x, y, v); },
                    [&](const ice::Pane& q) { observe(1, q, q.x, q.y, q.vx); },
                    [&](const ice::Pane& q, double x, double y) { observe(2, q, x, y, q.vx); });
            }
            std::vector<uint32_t> colors(192 * 128), lights(192 * 128);
            Paint                 p{info, colors, lights, 1};
            if (transform) {
                p.tx = 7;
                p.ty = -3;
                p.fx = -1;
                p.cs = std::cos(.2);
                p.sn = std::sin(.2);
            }
            ps.draw(p, source.settings().palette);
            REQUIRE(checksum(colors) == expected[row++]);
            REQUIRE(checksum(lights) == expected[row++]);
        }
        REQUIRE(event == 26u);
        REQUIRE(ps.list.size() == 1u);
        const auto& q = ps.list.front();
        REQUIRE(std::abs(q.x - (-.8212998774448362)) < 1e-9);
        REQUIRE(std::abs(q.y - (-6.646956142803891)) < 1e-9);
        REQUIRE(q.rest);
        REQUIRE(q.hits == 1);
        const auto snapshot = ps.corners(q);
        ps.list.clear();
        REQUIRE(snapshot.size() == 3u);
        REQUIRE(std::all_of(snapshot.begin(), snapshot.end(),
                            [](Point p) { return std::isfinite(p.x) && std::isfinite(p.y); }));
    }
    const std::array<Point, 3> polygon{{{-5, -3}, {6, -2}, {0, 7}}};
    REQUIRE(ice::isInsidePoly(polygon, 0, 0));
    REQUIRE(!ice::isInsidePoly(polygon, 12, 0));
    const auto                                     pieces = ice::splitPoly(polygon, 1, -1, .37);
    constexpr std::array<std::array<double, 2>, 7> points{{{{-5.0, -3.0}},
                                                           {{-3.8982169225283605, -2.899837902048033}},
                                                           {{4.443046155050155, 0.33543076742476785}},
                                                           {{0.0, 7.0}},
                                                           {{-3.8982169225283605, -2.899837902048033}},
                                                           {{6.0, -2.0}},
                                                           {{4.443046155050155, 0.33543076742476785}}}};
    REQUIRE(pieces.size() == 2u);
    REQUIRE(pieces[0].size() == 4u);
    REQUIRE(pieces[1].size() == 3u);
    size_t index = 0;
    for (const auto& part : pieces)
        for (const auto& q : part) {
            REQUIRE(std::abs(q.x - points[index][0]) < 1e-12);
            REQUIRE(std::abs(q.y - points[index][1]) < 1e-12);
            ++index;
        }
}
TEST_CASE("stylize.pixelfx.ice.spikesSourceWaveAndTransactions") {
    constexpr std::array<int, 9>       times{{0, 1, 4, 12, 24, 41, 43, 60, 90}};
    constexpr std::array<uint32_t, 18> expected{{2968548805u, 2968548805u, 2968548805u, 2968548805u, 2837923810u,
                                                 3822089583u, 2872123830u, 284753847u, 1856522487u, 1352233373u,
                                                 682015565u, 1548605913u, 2157471020u, 4085084746u, 4104136376u,
                                                 679143984u, 2968548805u, 2968548805u}};
    PixelFx                            fx(PixelFxKind::IceSpikes), other(PixelFxKind::IceSpikes);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        auto seek = fx.prepareSeek(times[i] / 30.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(fx.advance(.7));
    for (int i = 0; i < 84; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto old = fx.prepareSeek(.7);
    REQUIRE(old);
    auto settings       = fx.settings();
    settings.ice.reach  = 141.5;
    settings.ice.height = 43.5;
    settings.seed       = 123;
    settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto config         = PixelFx::prepareConfiguration(PixelFxKind::IceSpikes, settings);
    REQUIRE(config);
    REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
    REQUIRE(fx.advance(.7));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().ice.reach == 141.5);
    REQUIRE(fx.settings().ice.height == 43.5);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    for (const auto& bad : std::array<std::pair<const char*, double>, 4>{
             {{"reach", 15.5}, {"reach", 512.5}, {"height", 3.5}, {"height", 128.5}}}) {
        auto invalid = fx.configurationValue();
        invalid.find("options")->set(bad.first, eve::Value(bad.second));
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IceSpikes, invalid));
        REQUIRE(checksum(fx.color()) == changed);
    }
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::IceSpikes, settings));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.ice.centroidSourceOrientationAndDegeneracy") {
    using namespace eve::stylize::pixelfx;
    const std::array<std::vector<Point>, 4>        polygons{{{{0, 0}, {6, 0}, {4, 4}, {0, 2}},
                                                             {{0, 2}, {4, 4}, {6, 0}, {0, 0}},
                                                             {{1, 2}, {3, 4}, {5, 6}},
                                                             {{0, 0}, {.0001, 0}, {0, .0001}}}};
    constexpr std::array<std::array<double, 3>, 4> expected{{{{2.8333333333333335, 1.5, 16}},
                                                             {{2.8333333333333335, 1.5, 16}},
                                                             {{3, 4, 0}},
                                                             {{3.3333333333333335e-05, 3.3333333333333335e-05, 0}}}};
    for (size_t i = 0; i < polygons.size(); ++i) {
        const auto m = ice::centroid(polygons[i]);
        REQUIRE(std::abs(m.x - expected[i][0]) < 1e-12);
        REQUIRE(std::abs(m.y - expected[i][1]) < 1e-12);
        REQUIRE(std::abs(m.area - expected[i][2]) < 1e-12);
    }
}
TEST_CASE("stylize.pixelfx.ice.icicleSourceImpactAndTransactions") {
    constexpr std::array<int, 10>      ticks{{0, 19, 20, 36, 72, 77, 113, 114, 180, 312}};
    constexpr std::array<uint32_t, 20> expected{{3040236179u, 748404704u,  73559183u,   748404704u,  1813971064u,
                                                 748404704u,  1329284467u, 748404704u,  1471270479u, 595738068u,
                                                 3845731752u, 4294590393u, 3729086169u, 3799778722u, 3367682253u,
                                                 672309747u,  2960243815u, 693014391u,  2202501573u, 2202501573u}};
    PixelFx                            fx(PixelFxKind::IcicleFall), other(PixelFxKind::IcicleFall);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < ticks.size(); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(fx.advance(1.2));
    for (int i = 0; i < 144; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto old = fx.prepareSeek(1.2);
    REQUIRE(old);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto config      = PixelFx::prepareConfiguration(PixelFxKind::IcicleFall, settings);
    REQUIRE(config);
    settings.palette.clear();
    REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
    REQUIRE(fx.advance(1.2));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IcicleFall, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    invalid = fx.configurationValue();
    invalid.find("options")->set("seed", eve::Value(int64_t(-1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IcicleFall, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::IcicleFall, settings));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.ice.fracturesSourcePathsRngAndGrowingRaster") {
    using namespace eve::stylize::pixelfx;
    std::array<std::vector<ice::Crack>, 3> expected{};
    expected[0] = {{{1.7743601177493742, -17.409165214997316, 15.90125773968175},
                    {0.3229373243383069, -19.345330100516666, 18.321043389104307}},
                   {{0, -2, 0},
                    {0.06597346129007933, -3.8301732542424025, 1.8313619626220317},
                    {-0.17840241512380708, -6.251224959837276, 4.264715806627646},
                    {0.6869186453953323, -8.472951579856371, 6.649008094053716},
                    {1.9227750771165057, -10.74992758939877, 9.239753328124062},
                    {1.8331215747365182, -12.938901781159725, 11.430562713416293},
                    {1.794269142138319, -15.1283079664791, 13.620313602406531},
                    {1.7743601177493742, -17.409165214997316, 15.90125773968175},
                    {2.8033311306483872, -19.47543194247269, 18.209555713180453}},
                   {{0, -2, 0},
                    {1.0252742927870608, -0.20396788926553877, 2.06807125560008},
                    {1.8515831740125082, 1.3922964134283733, 3.8655267046298833},
                    {2.5340166061205984, 3.086106006675442, 5.691644534189255},
                    {3.631290498117738, 4.829589400163682, 7.7516795672476295},
                    {3.787513007233264, 6.919339883979972, 9.847261242335662},
                    {4.789868486668857, 8.580663616455132, 11.787548090005293},
                    {6.679064442015706, 9.6148076354946, 13.941269337013363},
                    {7.961865054076592, 11.01823177075827, 15.842631142074241},
                    {9.893173394150713, 11.988106342183677, 18.003790226625277},
                    {11.961568177568088, 12.501046641020007, 20.134838027646765},
                    {13.700237414715664, 12.29743178235661, 21.885389291355388}},
                   {{-3.191303446753441, -0.4787035990497954, 3.6197706622071566},
                    {-3.7294179975475097, 2.0106105904986884, 6.166582866525278},
                    {-4.255610729186714, 4.0459636066182645, 8.268853232357651},
                    {-5.30322567969027, 5.870179945018569, 10.37248398270458},
                    {-6.617584811744621, 7.285064702773657, 12.303659481136128},
                    {-7.937178689126803, 8.926099090756672, 14.409442438557743},
                    {-8.47103691693036, 11.249408911371688, 16.793299214867876},
                    {-8.256093322950361, 13.718796921509444, 19.272024268843236},
                    {-8.357773529795427, 16.04481149685969, 21.60026022535749},
                    {-7.878474006158373, 17.9013776221708, 23.51769752837718}},
                   {{0, -2, 0},
                    {-1.4425537385304092, -0.8818762998893603, 1.8251470892224462},
                    {-3.191303446753441, -0.4787035990497954, 3.6197706622071566},
                    {-4.429929010048802, 0.5358908126106257, 5.220893738744781},
                    {-4.9747086538740914, 2.08795072407153, 6.865787300374359},
                    {-4.879887236372231, 4.444883093693354, 9.224626272451133},
                    {-5.210146638582421, 6.012057847130496, 10.826221720920875},
                    {-4.992651635702124, 7.975651158572459, 12.801823596050962},
                    {-5.747660252586418, 9.886024539229195, 14.855981444194912},
                    {-5.579233088950138, 12.135920194010628, 17.112172519182785},
                    {-4.809154166370886, 14.098253760520791, 19.22019874607213},
                    {-3.9056741195673608, 15.803826535483566, 21.15029063136317},
                    {-2.5806044953536103, 16.81700920286982, 22.818327989010143},
                    {-1.1321641712225687, 18.121764649882653, 24.767780770966787},
                    {0.21378817012519025, 19.428232040235997, 26.643532552849507}}};
    expected[1] = {{{7.039723268374779, 1.092498802874272, 9.379436201276258},
                    {9.099265015376568, 0.7420562137717062, 11.468580096727237}},
                   {{4.4600425747630394, -2.81497400384513, 4.695824938174337},
                    {5.841813600604984, -0.6186555042727422, 7.2906478838529445},
                    {7.039723268374779, 1.092498802874272, 9.379436201276258}},
                   {{6.237708125406135, -8.044116441891711, 11.116510425694287},
                    {4.37719334385973, -9.493508514360931, 13.474953266233205}},
                   {{6.283348456240691, -3.6237149257170413, 6.690444254269824},
                    {6.153262637362211, -5.646919135472466, 8.7178262100555},
                    {6.237708125406135, -8.044116441891711, 11.116510425694287}},
                   {{12.175977243451753, -5.284436288961102, 13.291724899690598},
                    {13.88273171012768, -6.63478857348629, 15.468066348508}},
                   {{7.97301143821191, -5.214889768680769, 9.011391986021772},
                    {10.30190159047369, -4.851949334339896, 11.368393280077726},
                    {12.175977243451753, -5.284436288961102, 13.291724899690598}},
                   {{0, -2, 0},
                    {1.8960446928613686, -2.964906954325961, 2.1274470399599523},
                    {4.4600425747630394, -2.81497400384513, 4.695824938174337},
                    {6.283348456240691, -3.6237149257170413, 6.690444254269824},
                    {7.97301143821191, -5.214889768680769, 9.011391986021772}},
                   {{-7.2063360602067785, -3.2066204860777487, 8.569499818701297},
                    {-5.537968954579364, -5.113683062023915, 11.103338106302544}},
                   {{-3.7783375974951845, -0.39380300920333877, 4.114495530538261},
                    {-5.374377482865016, -1.4290223774254844, 6.016868379386143},
                    {-7.2063360602067785, -3.2066204860777487, 8.569499818701297}},
                   {{-3.9931223920325944, 4.916297395264426, 10.056094964267686},
                    {-5.573003426990572, 5.698723176196922, 11.819107779674232}},
                   {{-4.953233657856538, 0.749616866425238, 5.7539435366168625},
                    {-4.158663527447098, 3.157814939561121, 8.289837787486613},
                    {-3.9931223920325944, 4.916297395264426, 10.056094964267686}},
                   {{-3.696789187430288, 5.841161786905171, 12.011075941985473},
                    {-1.2204094450776002, 5.545441501089922, 14.505050102993845}},
                   {{-6.028118045114665, 2.0893053235909322, 7.471541163604707},
                    {-5.381132345235095, 4.120287696769817, 9.603085180511698},
                    {-3.696789187430288, 5.841161786905171, 12.011075941985473}},
                   {{0, -2, 0},
                    {-1.4604630872128437, -1.234927040336896, 1.6487234645988793},
                    {-3.7783375974951845, -0.39380300920333877, 4.114495530538261},
                    {-4.953233657856538, 0.749616866425238, 5.7539435366168625},
                    {-6.028118045114665, 2.0893053235909322, 7.471541163604707}}};
    expected[2] = {};
    constexpr std::array<uint32_t, 3>      states{{3016143479u, 1632165923u, 4231026098u}};
    std::array<std::vector<ice::Crack>, 3> paths;
    const std::array<uint32_t, 3>          seeds{{123, 0, 4294967295u}};
    for (size_t i = 0; i < 3; ++i) {
        Rng                  r(seeds[i]);
        ice::FractureOptions o;
        if (i == 1) {
            o.length = 4;
            o.fork   = 1;
            o.angle  = 0;
        }
        paths[i] = ice::fractures(
            r, [&](double x, double y) { return i != 2 && x * x + y * y < 400; }, 0, -2, i == 0 ? 3 : 2, o);
        REQUIRE(r.state == states[i]);
        REQUIRE(paths[i].size() == expected[i].size());
        for (size_t j = 0; j < paths[i].size(); ++j) {
            REQUIRE(paths[i][j].size() == expected[i][j].size());
            for (size_t k = 0; k < paths[i][j].size(); ++k) {
                const auto a = paths[i][j][k], b = expected[i][j][k];
                REQUIRE(std::abs(a.x - b.x) < 1e-12);
                REQUIRE(std::abs(a.y - b.y) < 1e-12);
                REQUIRE(std::abs(a.d - b.d) < 1e-12);
            }
        }
    }
    constexpr std::array<uint32_t, 32> planes{
        {2999778757u, 2999778757u, 600987749u,  1490405967u, 3950056053u, 1631207001u, 1297315601u, 111208071u,
         2999778757u, 2999778757u, 1238691254u, 1437343719u, 4005950782u, 4177749902u, 1433710814u, 1946744854u,
         2999778757u, 2999778757u, 2230758709u, 1233642914u, 2685419314u, 3254469381u, 2708559910u, 3049316810u,
         2999778757u, 2999778757u, 2202257638u, 2063129909u, 1230270006u, 342880189u,  3937509474u, 1236696162u}};
    const PixelFxInfo info{PixelFxKind::IceShatter, "fixture", 96, 80, 48, 64, 1, true};
    PixelFx           source(PixelFxKind::IceShatter);
    size_t            index = 0;
    for (bool transform : {false, true})
        for (bool variant : {false, true})
            for (double reach : {0., 3., 12., 100.}) {
                std::vector<uint32_t> colors(96 * 80), lights(96 * 80);
                Paint                 p{info, colors, lights, 1};
                if (transform) {
                    p.tx = 7;
                    p.ty = -3;
                    p.fx = -1;
                    p.cs = std::cos(.2);
                    p.sn = std::sin(.2);
                }
                ice::FracturePaint o;
                if (variant) {
                    o.x     = 1.25;
                    o.y     = -2.5;
                    o.hot   = false;
                    o.alpha = .6;
                }
                ice::drawFractures(p, paths[0], reach, source.settings().palette, o);
                REQUIRE(checksum(colors) == planes[index++]);
                REQUIRE(checksum(lights) == planes[index++]);
            }
}
TEST_CASE("stylize.pixelfx.ice.shatterSourceCracksDebrisAndTransactions") {
    constexpr std::array<int, 11>      ticks{{0, 31, 32, 48, 49, 74, 75, 81, 82, 144, 336}};
    constexpr std::array<uint32_t, 22> expected{
        {2426049974u, 1929986101u, 2426049974u, 1929986101u, 4252589909u, 2935613835u, 3540062591u, 2562412460u,
         746376992u,  1735206320u, 1885431744u, 2282198015u, 4270683897u, 2073554090u, 2148935762u, 1558863221u,
         34989388u,   2168696289u, 3767358896u, 2736410510u, 3721674181u, 3721674181u}};
    PixelFx fx(PixelFxKind::IceShatter), other(PixelFxKind::IceShatter);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < ticks.size(); ++i) {
        auto seek = fx.prepareSeek(ticks[i] / 120.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(fx.advance(1.2));
    for (int i = 0; i < 144; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto old = fx.prepareSeek(1.2);
    REQUIRE(old);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto config      = PixelFx::prepareConfiguration(PixelFxKind::IceShatter, settings);
    REQUIRE(config);
    settings.palette.clear();
    REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
    REQUIRE(fx.advance(1.2));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IceShatter, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    invalid = fx.configurationValue();
    invalid.find("options")->set("seed", eve::Value(int64_t(-1)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::IceShatter, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::IceShatter, settings));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.ice.freezeSourceFloodFacetsAndTransactions") {
    constexpr std::array<int, 15>      frames{{0, 4, 5, 17, 18, 27, 33, 54, 66, 70, 74, 75, 76, 86, 120}};
    constexpr std::array<uint32_t, 30> expected{
        {983565857u,  3510902213u, 1248467528u, 3956215485u, 4268592185u, 737500066u,  432078824u,  1438035342u,
         1325924268u, 3194585578u, 3745775591u, 4113212730u, 1407300454u, 3388438737u, 1790001910u, 1568464969u,
         3754015731u, 3605966822u, 1180129108u, 206558415u,  1639258129u, 152660741u,  2233984550u, 3360954077u,
         4110472210u, 2761451381u, 4156687411u, 3307230081u, 3510902213u, 3510902213u}};
    PixelFx fx(PixelFxKind::Freeze), other(PixelFxKind::Freeze);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < frames.size(); ++i) {
        auto seek = fx.prepareSeek(frames[i] / 30.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto reset = fx.prepareSeek(0);
    REQUIRE(reset);
    REQUIRE(fx.commitSeek(std::move(reset).takeValue()));
    REQUIRE(fx.advance(1.3));
    for (int i = 0; i < 156; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto old = fx.prepareSeek(1.3);
    REQUIRE(old);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto config      = PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings);
    REQUIRE(config);
    settings.palette.clear();
    REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
    REQUIRE(fx.advance(1.3));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Freeze, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.ice.freezeCustomMaskOwnershipAndAbsence") {
    PixelFx fx(PixelFxKind::Freeze);
    auto    settings = fx.settings();
    auto&   a        = settings.actor;
    a                = {};
    a.source         = PixelFxActorSource::Custom;
    a.width          = 10;
    a.height         = 8;
    a.scale          = 1;
    a.x              = -5.5;
    a.y              = -34.25;
    a.head           = {0, -34.25};
    a.chest          = {0, -29};
    a.eyes           = {{3, 2}, {6, 2}};
    a.mask           = {0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                        1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0};
    a.pixels = {0u,          0u,          0u,          4284497952u, 4289249496u, 4289249496u, 4284497952u, 0u,
                0u,          0u,          0u,          4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u,
                4289249496u, 4289249496u, 4284497952u, 0u,          4289249496u, 4284497952u, 4289249496u, 4289249496u,
                4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u,
                4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u,
                4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u,
                4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u, 4284497952u, 4289249496u,
                4289249496u, 4284497952u, 4289249496u, 4289249496u, 0u,          4289249496u, 4289249496u, 4284497952u,
                4289249496u, 4289249496u, 4284497952u, 4289249496u, 4289249496u, 0u,          0u,          0u,
                0u,          4289249496u, 4289249496u, 4284497952u, 4289249496u, 0u,          0u,          0u};
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings);
    REQUIRE(candidate);
    std::fill(a.mask.begin(), a.mask.end(), uint8_t(0));
    std::fill(a.pixels.begin(), a.pixels.end(), 0u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    constexpr std::array<int, 6>       frames{{12, 24, 75, 78, 90, 120}};
    constexpr std::array<uint32_t, 12> expected{{427179318u, 2967103288u, 1942758061u, 2672985111u, 3981939444u,
                                                 4051866287u, 889873776u, 857195850u, 3215605342u, 1834867167u,
                                                 3510902213u, 3510902213u}};
    for (size_t i = 0; i < frames.size(); ++i) {
        auto seek = fx.prepareSeek(frames[i] / 30.);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    settings = fx.settings();
    settings.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings));
    settings = fx.settings();
    std::fill(settings.actor.mask.begin(), settings.actor.mask.end(), uint8_t(0));
    for (int y : {0, 1, 6, 7})
        for (int x = 2; x < 5; ++x) settings.actor.mask[size_t(y * 10 + x)] = 1;
    auto disconnected = PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings);
    REQUIRE(disconnected);
    REQUIRE(fx.commitConfiguration(std::move(disconnected).takeValue()));
    auto seek = fx.prepareSeek(.4);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(checksum(fx.color()) == 1992736117u);
    REQUIRE(checksum(fx.light()) == 242254143u);
    settings = fx.settings();
    std::fill(settings.actor.mask.begin(), settings.actor.mask.end(), uint8_t(0));
    auto empty = PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings);
    REQUIRE(empty);
    REQUIRE(fx.commitConfiguration(std::move(empty).takeValue()));
    REQUIRE(fx.advance(.8));
    fx.draw();
    const auto withoutBody = checksum(fx.color()), withoutBodyLight = checksum(fx.light());
    settings       = fx.settings();
    settings.actor = {};
    auto none      = PixelFx::prepareConfiguration(PixelFxKind::Freeze, settings);
    REQUIRE(none);
    REQUIRE(fx.commitConfiguration(std::move(none).takeValue()));
    REQUIRE(fx.settings().actor.mask.empty());
    REQUIRE(fx.advance(.8));
    fx.draw();
    REQUIRE(checksum(fx.color()) == withoutBody);
    REQUIRE(checksum(fx.light()) == withoutBodyLight);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
}
TEST_CASE("stylize.pixelfx.ice.frostNovaSourceRingsAndTransactions") {
    constexpr std::array<int, 14>      frames{{0, 3, 10, 12, 13, 14, 20, 30, 45, 59, 65, 75, 90, 102}};
    constexpr std::array<uint32_t, 28> expected{
        {3245989154u, 3710829981u, 2365909930u, 835614603u,  2488522284u, 3342673575u, 2139339728u,
         1354619619u, 1001195657u, 1461982698u, 998427083u,  1266190857u, 782665421u,  2105151580u,
         2150836877u, 3745935634u, 3968779890u, 2763898094u, 1655079602u, 2317196944u, 3189672296u,
         596957041u,  3458515613u, 2297359920u, 891707190u,  4017981543u, 2343312837u, 2343312837u}};
    PixelFx fx(PixelFxKind::FrostNova), other(PixelFxKind::FrostNova);
    REQUIRE(fx.isSimulated());
    REQUIRE(fx.settings().ice.reach == 104.);
    for (size_t i = 0; i < frames.size(); ++i) {
        auto candidate = fx.prepareSeek(frames[i] / 30.);
        REQUIRE(candidate);
        REQUIRE(fx.commitSeek(std::move(candidate).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto zero = fx.prepareSeek(0);
    REQUIRE(zero);
    REQUIRE(fx.commitSeek(std::move(zero).takeValue()));
    REQUIRE(fx.advance(1.3));
    for (int i = 0; i < 156; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    REQUIRE(checksum(fx.color()) == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.8);
    REQUIRE(stale);
    auto settings      = fx.settings();
    settings.seed      = 123;
    settings.ice.reach = 76;
    settings.palette   = {0xff122340u, 0xff386270u, 0xff80b9b0u, 0xffdd6250u, 0xfff12bb0u};
    auto candidate     = PixelFx::prepareConfiguration(PixelFxKind::FrostNova, settings);
    REQUIRE(candidate);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.advance(1.3));
    fx.draw();
    REQUIRE(checksum(fx.color()) != original);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette.size() == 8u);
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    const auto changed = checksum(fx.color());
    for (double reach : {15., 513.}) {
        settings           = fx.settings();
        settings.ice.reach = reach;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FrostNova, settings));
    }
    auto inactive = fx.configurationValue();
    inactive.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FrostNova, inactive));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.ice.frostBreathSourceLoopAndConfiguration") {
    constexpr std::array<int, 12>      frames{{7, 10, 13, 19, 44, 79, 84, 96, 108, 117, 125, 133}};
    constexpr std::array<uint32_t, 24> expected{
        {3384590035u, 1865943155u, 2014932085u, 329700265u,  1512132375u, 710464573u,  3555763135u, 3983490246u,
         3141351799u, 4008180613u, 4099617379u, 2577271828u, 1686134824u, 2755775537u, 3621827197u, 1764702198u,
         3240819492u, 4106296313u, 240563865u,  2169328337u, 486211762u,  643828465u,  554375465u,  1043031363u}};
    PixelFx fx(PixelFxKind::FrostBreath), other(PixelFxKind::FrostBreath);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < frames.size(); ++i) {
        fx.render(frames[i] / 120.);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(1.2);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings       = fx.settings();
    settings.ice.spread = .37;
    settings.ice.power  = 1.4;
    settings.seed       = 123;
    settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto candidate      = PixelFx::prepareConfiguration(PixelFxKind::FrostBreath, settings);
    REQUIRE(candidate);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(checksum(other.color()) == original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    for (double spread : {-.01, 1.01}) {
        settings            = fx.settings();
        settings.ice.spread = spread;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FrostBreath, settings));
    }
    for (double power : {-.01, 4.01}) {
        settings           = fx.settings();
        settings.ice.power = power;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FrostBreath, settings));
    }
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("reach", eve::Value(100.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FrostBreath, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::FrostBreath, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(1e12);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    fx.render(.375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.ice.blizzardVortexSourceLayersAndConfiguration") {
    constexpr std::array<int, 9>       frames{{0, 4, 12, 24, 48, 72, 120, 144, 240}};
    constexpr std::array<uint32_t, 18> expected{{2373643909u, 1312718172u, 1590366835u, 2321370308u, 3614470339u,
                                                 3548848541u, 3526271254u, 2810822328u, 3083591198u, 1403122856u,
                                                 3628436450u, 1020323977u, 2146517604u, 1102747422u, 3881226056u,
                                                 3059512719u, 2373643909u, 1312718172u}};
    PixelFx                            fx(PixelFxKind::BlizzardVortex), other(PixelFxKind::BlizzardVortex);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < frames.size(); ++i) {
        fx.render(frames[i] / 120.);
        bool colorMatches = false;
        if (frames[i] == 0 || frames[i] == 240) {
            // Approved exact loop repeat: only R98 ->97 at (63,71).
            colorMatches = (test_pixelfx_reference::matchesColorReference(fx.color(), expected[2 * i],
                                                                          {9151, 0xfff4c862u, 0, 0b001}));
        } else if (frames[i] == 12) {
            // Approved tick12 boundaries: G-1, B+1, R+1 and G+1 at these four pixels only.
            constexpr std::array<test_pixelfx_reference::RoundingPixel, 4> pixels{{{4035, 0xffffeca6u, 0, 0b010},
                                                                                   {12730, 0x6cfef4c9u, 0b100},
                                                                                   {13113, 0xfffffadbu, 0b001},
                                                                                   {13114, 0xfffffadcu, 0b010}}};
            colorMatches = (test_pixelfx_reference::matchesColorReference(fx.color(), expected[2 * i], pixels));
        } else if (frames[i] == 72) {
            // Approved tick72 boundary: only B255 ->254 at (57,96).
            colorMatches = (test_pixelfx_reference::matchesColorReference(fx.color(), expected[2 * i],
                                                                          {12345, 0x74fff1c0u, 0, 0b100}));
        } else if (frames[i] == 48) {
            // Pinned upstream pixel (71,64): only G=235/236 differs between V8/MSVC and GCC.
            colorMatches = (test_pixelfx_reference::matchesColorReference(fx.color(), expected[2 * i],
                                                                          {8263, 0xffffeba6u, 0b010}));
        } else if (frames[i] == 120) {
            // Additional approved frame: B+1 at (56,89), B-1 at (55,90), G+1 at (55,96).
            constexpr std::array<test_pixelfx_reference::RoundingPixel, 3> pixels{
                {{11448, 0x12feedacu, 0b100}, {11575, 0x1cffefb8u, 0, 0b100}, {12343, 0x22ffeba6u, 0b010}}};
            colorMatches = (test_pixelfx_reference::matchesColorReference(fx.color(), expected[2 * i], pixels));
        } else {
            colorMatches = (checksum(fx.color()) == expected[2 * i]);
        }
        if (!colorMatches || checksum(fx.light()) != expected[2 * i + 1])
            test_pixelfx_diagnostic::dumpSequence("ice/blizzard-vortex", fx, frames.size(), [&](size_t j) {
                return test_pixelfx_diagnostic::ExpectedFrame{frames[j] / 120., expected[2 * j], expected[2 * j + 1]};
            });
        REQUIRE(colorMatches);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    constexpr std::array<int, 12>      lightFrames{{0, 4, 12, 24, 48, 72, 96, 120, 144, 180, 216, 240}};
    constexpr std::array<uint32_t, 12> lightExpected{{1312718172u, 2321370308u, 3548848541u, 2810822328u, 1403122856u,
                                                      1020323977u, 85661786u, 1102747422u, 3059512719u, 3407878012u,
                                                      3712582659u, 1312718172u}};
    for (size_t i = 0; i < lightFrames.size(); ++i) {
        fx.render(lightFrames[i] / 120.);
        REQUIRE(checksum(fx.light()) == lightExpected[i]);
    }
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(2);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings       = fx.settings();
    settings.ice.height = 93.5;
    settings.seed       = 123;
    settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto candidate      = PixelFx::prepareConfiguration(PixelFxKind::BlizzardVortex, settings);
    REQUIRE(candidate);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(checksum(other.color()) == original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    for (double height : {3.9, 256.1}) {
        settings            = fx.settings();
        settings.ice.height = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BlizzardVortex, settings));
    }
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("power", eve::Value(2.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BlizzardVortex, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::BlizzardVortex, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(1e12);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    fx.render(.375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
}  // namespace test_pixelfx_ice_unit
