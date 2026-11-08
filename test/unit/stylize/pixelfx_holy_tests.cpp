// Holy effect fixtures have independent ownership from the core suite, which is near5000 lines.
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <span>
#include <vector>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/HolyActor.h"
#include "stylize/pixelfx/HolyFeathers.h"
#include "stylize/pixelfx/ResurrectionWings.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_holy_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
void sampleSimulated(PixelFx& fx, double time) {
    auto seek = fx.prepareSeek(time);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
}

TEST_CASE("stylize.pixelfx.holy.blessingPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 18> times{{0, 0.008333333333333333, 0.25, 0.5, 0.7416666666666667, 0.75,
                                            0.7583333333333333, 1, 1.4916666666666667, 1.5, 1.5083333333333333, 2,
                                            2.2416666666666667, 2.25, 2.2583333333333333, 2.75, 2.9916666666666667, 3}};
    constexpr std::array<uint32_t, 36> expected{
        {1378631681u, 2221939596u, 844565855u,  1477234357u, 2660802319u, 2488103324u, 3528672140u, 2000282952u,
         464270397u,  519527506u,  3823968092u, 875119840u,  2678792472u, 383542558u,  2458676139u, 770529439u,
         3691959268u, 180908476u,  113049352u,  875184967u,  762526150u,  3925981603u, 20713213u,   3424588887u,
         3452715993u, 4069421321u, 1832301622u, 158339309u,  112222187u,  3115378389u, 1381081441u, 4028772562u,
         3740631364u, 2498276099u, 1378631681u, 2221939596u}};
    PixelFx fx(PixelFxKind::Blessing), other(PixelFxKind::Blessing);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Blessing, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Blessing, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Blessing, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Blessing, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Blessing, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(3.);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), seam.begin()));
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

TEST_CASE("stylize.pixelfx.holy.blessingOwnsMaskDependentLandings") {
    PixelFx filled(PixelFxKind::Blessing), empty(PixelFxKind::Blessing);
    auto    settings = filled.settings();
    settings.actor   = {};
    auto& a          = settings.actor;
    a.source         = PixelFxActorSource::Custom;
    a.width          = 24;
    a.height         = 20;
    a.x              = -12;
    a.y              = -70;
    a.scale          = 1;
    a.head           = {0, -70};
    a.chest          = {0, -60};
    a.mask.assign(480, 1);
    a.pixels.assign(480, 0);
    auto first = PixelFx::prepareConfiguration(PixelFxKind::Blessing, settings);
    REQUIRE(first);
    a.mask.assign(480, 0);
    auto second = PixelFx::prepareConfiguration(PixelFxKind::Blessing, settings);
    REQUIRE(second);
    REQUIRE(filled.commitConfiguration(std::move(first).takeValue()));
    REQUIRE(empty.commitConfiguration(std::move(second).takeValue()));
    REQUIRE(filled.settings().actor.mask.front() == 1);
    REQUIRE(empty.settings().actor.mask.front() == 0);
    bool different = false;
    for (double t : {.3, 1., 1.9, 2.8}) {
        filled.render(t);
        empty.render(t);
        different = different || !std::equal(filled.color().begin(), filled.color().end(), empty.color().begin());
    }
    REQUIRE(different);
    filled.render(1.9);
    const std::vector<uint32_t> original(filled.color().begin(), filled.color().end());
    auto                        document = filled.configurationValue();
    auto                        restored = PixelFx::prepareConfigurationValue(PixelFxKind::Blessing, document);
    REQUIRE(restored);
    REQUIRE(filled.commitConfiguration(std::move(restored).takeValue()));
    filled.render(1.9);
    REQUIRE(std::equal(filled.color().begin(), filled.color().end(), original.begin()));
    auto invalid = filled.settings();
    invalid.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Blessing, invalid));
    REQUIRE(filled.configurationValue() == document);
    for (auto source : {PixelFxActorSource::None, PixelFxActorSource::Hero, PixelFxActorSource::Dummy}) {
        auto s         = filled.settings();
        s.actor.source = source;
        auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Blessing, s);
        REQUIRE(candidate);
        REQUIRE(filled.commitConfiguration(std::move(candidate).takeValue()));
        filled.render(1.9);
    }
}

TEST_CASE("stylize.pixelfx.holy.blessingFractionalOneCellBounds") {
    constexpr std::array<uint32_t, 14> expected{{1600428461u, 2851543365u, 1642826567u, 107352365u, 315750058u,
                                                 3054155521u, 3609419401u, 3254510003u, 1299216813u, 3424588887u,
                                                 1435242950u, 1653462542u, 1600428461u, 2851543365u}};
    PixelFx                            fx(PixelFxKind::Blessing);
    auto                               settings = fx.settings();
    settings.actor                              = {};
    auto& a                                     = settings.actor;
    a.source                                    = PixelFxActorSource::Custom;
    a.width                                     = 1;
    a.height                                    = 1;
    a.scale                                     = 1;
    a.x                                         = -.5;
    a.y                                         = -50.5;
    a.head                                      = {0, -50.5};
    a.chest                                     = {0, -50};
    a.mask                                      = {1};
    a.pixels                                    = {0};
    auto candidate                              = PixelFx::prepareConfiguration(PixelFxKind::Blessing, settings);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    for (size_t i = 0; i < 7; ++i) {
        fx.render(double(i) / 2);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
}

TEST_CASE("stylize.pixelfx.holy.featherBurstPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 19>   times{{0, 0.008333333333333333, 0.016666666666666666, 0.025, 0.03333333333333333,
                                              0.05, 0.1, 0.15, 0.3, 0.45, 0.6, 0.8, 1, 1.4, 1.8, 2.2, 2.6, 3, 3.5}};
    constexpr std::array<uint32_t, 38> expected{
        {2182876289u, 2419011041u, 2350077726u, 960862177u,  3372699300u, 696968078u,  4256177045u, 3236610457u,
         2327900508u, 2003114464u, 1227337696u, 587148365u,  1310492138u, 1842683535u, 100187349u,  2265857829u,
         929151631u,  1349463601u, 2582918993u, 2058036955u, 1691491973u, 3919108526u, 2144989762u, 607289706u,
         1442952294u, 1783174668u, 2741602371u, 2471687567u, 2669602682u, 157717908u,  717801292u,  1200418582u,
         2944811088u, 1119739333u, 1119739333u, 1119739333u, 1119739333u, 1119739333u}};
    PixelFx fx(PixelFxKind::FeatherBurst), other(PixelFxKind::FeatherBurst);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::FeatherBurst, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 6u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::FeatherBurst, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FeatherBurst, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FeatherBurst, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::FeatherBurst, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 3.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}


TEST_CASE("stylize.pixelfx.holy.sourceFeatherRaster") {
    const std::array<uint32_t, 6> ramp{0xff60454au, 0xff94747au, 0xffc2a5aau, 0xffe6d3d6u, 0xfffaf3f4u, 0xffffffffu};
    constexpr std::array<std::array<double, 7>, 6>   specs{{{0, 12, 3, 1, 1, 0, .02},
                                                            {.7, 14, 3.4, .7, -1, 1, .2},
                                                            {-2.1, 8, 2, .3, 1, 0, .2},
                                                            {1.5, 20, .4, 1, -1, .6, .02},
                                                            {0, 1, 3, 1, 1, 1, .2},
                                                            {.1, 12, 3, .02, 1, 0, .2}}};
    constexpr std::array<std::array<uint32_t, 2>, 6> sums{{{{1280876615u, 2315578821u}},
                                                           {{1141177288u, 2315578821u}},
                                                           {{3256609301u, 2315578821u}},
                                                           {{4078199229u, 2315578821u}},
                                                           {{2315578821u, 2315578821u}},
                                                           {{2315578821u, 2315578821u}}}};
    const PixelFxInfo info{PixelFxKind::FeatherBurst, "feather-fixture", 80, 80, 40, 40, 3, false};
    for (size_t i = 0; i < specs.size(); ++i) {
        std::vector<uint32_t> color(6400), light(6400);
        Paint                 p{info, color, light, 1};
        if (i == 2) {
            p.tx = 7;
            p.ty = -3;
            p.fx = -1;
            p.cs = std::cos(.2);
            p.sn = std::sin(.2);
        }
        const auto q = specs[i];
        holy::feather(p, -5, 0, q[0], q[1], q[2], ramp, q[3], q[4], q[5], q[6]);
        uint32_t c = 2166136261u, l = c;
        for (auto byte : std::as_bytes(std::span(color))) c = (c ^ uint8_t(byte)) * 16777619u;
        for (auto byte : std::as_bytes(std::span(light))) l = (l ^ uint8_t(byte)) * 16777619u;
        REQUIRE(c == sums[i][0]);
        REQUIRE(l == sums[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.holy.sourceFeatherDynamicsAndDefaultDraws") {
    struct Expected {
        int                                 tick;
        uint32_t                            rng;
        std::vector<std::array<double, 20>> rows;
    };
    const std::array<Expected, 11> expected{{{0,
                                              4103143828u,
                                              {{{0.0,
                                                 -2.0,
                                                 30.0,
                                                 20.0,
                                                 0.6,
                                                 12.0,
                                                 3.0,
                                                 0.8,
                                                 0.025,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.0,
                                                 0.0,
                                                 0.0}},
                                               {{4.0,
                                                 -4.0,
                                                 -20.0,
                                                 12.0,
                                                 -1.2,
                                                 7.0,
                                                 2.0,
                                                 1.2,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.0,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 5.0,
                                                 0.0,
                                                 0.0,
                                                 2.5,
                                                 10.0,
                                                 2.5,
                                                 1.4,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.0,
                                                 0.0,
                                                 0.0}}}},
                                             {1,
                                              4103143828u,
                                              {{{0.0,
                                                 -2.0,
                                                 30.0,
                                                 20.0,
                                                 0.6,
                                                 12.0,
                                                 3.0,
                                                 0.8,
                                                 0.01666666666666667,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.0,
                                                 0.0,
                                                 0.0}},
                                               {{3.8380555555555556,
                                                 -3.902833333333333,
                                                 -19.433333333333334,
                                                 11.66,
                                                 -1.2467346008168534,
                                                 7.0,
                                                 2.0,
                                                 1.1916666666666667,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.008333333333333333,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 2.5012293538311496,
                                                 10.0,
                                                 2.5,
                                                 0.6,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.008333333333333333,
                                                 0.0,
                                                 1.0}}}},
                                             {3,
                                              4103143828u,
                                              {{{0.0,
                                                 -2.0,
                                                 30.0,
                                                 20.0,
                                                 0.6,
                                                 12.0,
                                                 3.0,
                                                 0.8,
                                                 3.469446951953614e-18,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.0,
                                                 0.0,
                                                 0.0}},
                                               {{3.52780193904321,
                                                 -3.716681163425926,
                                                 -18.347711759259262,
                                                 11.008627055555555,
                                                 -1.3402038024505603,
                                                 7.0,
                                                 2.0,
                                                 1.175,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.025,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 2.6035096030981553,
                                                 10.0,
                                                 2.5,
                                                 0.5833333333333334,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.025,
                                                 0.0,
                                                 1.0}}}},
                                             {4,
                                              4103143828u,
                                              {{{0.0,
                                                 -2.0,
                                                 30.0,
                                                 20.0,
                                                 0.6,
                                                 12.0,
                                                 3.0,
                                                 0.8,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.0,
                                                 0.0,
                                                 0.0}},
                                               {{3.379236439659208,
                                                 -3.627541863795525,
                                                 -17.82785992608025,
                                                 10.696715955648148,
                                                 -1.3869384032674137,
                                                 7.0,
                                                 2.0,
                                                 1.1666666666666667,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.03333333333333333,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 2.6483498573057918,
                                                 10.0,
                                                 2.5,
                                                 0.5750000000000001,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.03333333333333333,
                                                 0.0,
                                                 1.0}}}},
                                             {12,
                                              4103143828u,
                                              {{{1.761161775583976,
                                                 -0.8258921496106825,
                                                 23.837444215795347,
                                                 15.891629477196902,
                                                 1.0000246128067372,
                                                 12.0,
                                                 3.0,
                                                 0.7333333333333336,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.06666666666666667,
                                                 0.0,
                                                 0.0}},
                                               {{2.332644924916611,
                                                 -2.999586954949966,
                                                 -14.165687215831712,
                                                 8.499412329499027,
                                                 -1.7608152098022412,
                                                 7.0,
                                                 2.0,
                                                 1.1000000000000003,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.09999999999999999,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 2.8956962006562486,
                                                 10.0,
                                                 2.5,
                                                 0.5083333333333336,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.09999999999999999,
                                                 0.0,
                                                 1.0}}}},
                                             {24,
                                              4103143828u,
                                              {{{3.7589733701657595,
                                                 0.49289671246094185,
                                                 16.883688939289684,
                                                 11.255792626193125,
                                                 1.5881178119182016,
                                                 12.0,
                                                 3.0,
                                                 0.633333333333334,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.16666666666666666,
                                                 0.0,
                                                 0.0}},
                                               {{1.1308272386396694,
                                                 -2.2346548835534916,
                                                 -10.0333347148389,
                                                 6.0200008289033375,
                                                 -2.3335049125803304,
                                                 7.0,
                                                 2.0,
                                                 1.0000000000000007,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.19999999999999998,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 3.0550381772130013,
                                                 10.0,
                                                 2.5,
                                                 0.4083333333333334,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.19999999999999998,
                                                 0.0,
                                                 1.0}}}},
                                             {48,
                                              4103143828u,
                                              {{{6.50013218014563,
                                                 2.84122520085582,
                                                 8.469985117455836,
                                                 5.64665674497056,
                                                 2.852456868100065,
                                                 12.0,
                                                 3.0,
                                                 0.43333333333333424,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.366666666666667,
                                                 0.0,
                                                 0.0}},
                                               {{-0.36474716292303716,
                                                 0.44418169082712633,
                                                 -5.03339027499957,
                                                 3.0200341649997404,
                                                 -3.2589007524571523,
                                                 7.0,
                                                 2.0,
                                                 0.8000000000000014,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.4000000000000004,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 3.130868474151271,
                                                 10.0,
                                                 2.5,
                                                 0.20833333333333293,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.4000000000000004,
                                                 0.0,
                                                 1.0}}}},
                                             {72,
                                              4103143828u,
                                              {{{7.076126827802053,
                                                 4.0,
                                                 6.9263395453163055,
                                                 4.61755969687754,
                                                 3.1333683518620923,
                                                 12.0,
                                                 3.0,
                                                 0.23333333333333367,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.5666666666666671,
                                                 0.0,
                                                 1.0}},
                                               {{-0.3217979664316244,
                                                 3.799730024591401,
                                                 -2.5250844689742835,
                                                 1.5150506813845699,
                                                 -3.2578781464755977,
                                                 7.0,
                                                 2.0,
                                                 0.6000000000000021,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.6000000000000003,
                                                 1.0,
                                                 0.0}},
                                               {{-3.0,
                                                 4.0,
                                                 0.0,
                                                 0.0,
                                                 3.140263917868154,
                                                 10.0,
                                                 2.5,
                                                 0.008333333333332969,
                                                 0.0,
                                                 1.0,
                                                 2.2332022815417467,
                                                 3.929514790372923,
                                                 8.948754444951192,
                                                 16.107526203617454,
                                                 0.14752245973795652,
                                                 0.12666089609265324,
                                                 1.4,
                                                 0.6000000000000003,
                                                 0.0,
                                                 1.0}}}},
                                             {96,
                                              4103143828u,
                                              {{{7.076126827802053,
                                                 4.0,
                                                 6.9263395453163055,
                                                 4.61755969687754,
                                                 3.140573655021764,
                                                 12.0,
                                                 3.0,
                                                 0.033333333333333715,
                                                 -0.00833333333333333,
                                                 1.0,
                                                 1.1218223078305816,
                                                 3.8934417196549473,
                                                 9.0,
                                                 28.0,
                                                 6.000369192101061,
                                                 0.25457638672087335,
                                                 0.8,
                                                 0.7666666666666664,
                                                 0.0,
                                                 1.0}},
                                               {{-0.17323993384186692,
                                                 4.0,
                                                 -2.250842610719713,
                                                 1.3505055664318273,
                                                 -3.1544546497309778,
                                                 7.0,
                                                 2.0,
                                                 0.40000000000000213,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.7999999999999996,
                                                 1.0,
                                                 1.0}}}},
                                             {120,
                                              4103143828u,
                                              {{{-0.17323993384186692,
                                                 4.0,
                                                 -2.250842610719713,
                                                 1.3505055664318273,
                                                 -3.1431862667722603,
                                                 7.0,
                                                 2.0,
                                                 0.20000000000000168,
                                                 0.0,
                                                 1.0,
                                                 4.927853776889748,
                                                 3.7727307316381484,
                                                 9.072107032639906,
                                                 20.0,
                                                 -5.608152098022401,
                                                 -0.3424930305453017,
                                                 1.2,
                                                 0.9999999999999989,
                                                 1.0,
                                                 1.0}}}},
                                             {180, 4103143828u, {}}}};
    Rng                            r(123);
    holy::Feathers                 f(4.5);
    f.add(r, {0, -2, 30, 20, .6, 12, 3, .8, .025, false, 28., 9.});
    f.add(r, {4, -4, -20, 12, -1.2, 7, 2, 1.2, 0, true, 20., {}});
    f.add(r, {-3, 5, 0, 0, 2.5, 10, 2.5, 1.4, 0, false, {}, {}});
    int tick = 0;
    for (const auto& snapshot : expected) {
        while (tick < snapshot.tick) {
            f.update(1. / 120);
            ++tick;
        }
        REQUIRE(r.state == snapshot.rng);
        REQUIRE(f.list.size() == snapshot.rows.size());
        for (size_t i = 0; i < f.list.size(); ++i) {
            const auto&                  q = f.list[i];
            const std::array<double, 20> actual{q.x,
                                                q.y,
                                                q.vx,
                                                q.vy,
                                                q.angle,
                                                q.length,
                                                q.width,
                                                q.life,
                                                q.delay,
                                                q.side,
                                                q.phase,
                                                q.omega,
                                                q.amplitude,
                                                q.sink,
                                                q.angularVelocity,
                                                q.tilt,
                                                q.maximum,
                                                q.age,
                                                q.back ? 1. : 0.,
                                                q.landed ? 1. : 0.};
            for (size_t k = 0; k < actual.size(); ++k) REQUIRE(std::abs(actual[k] - snapshot.rows[i][k]) < 1e-11);
        }
    }
}
TEST_CASE("stylize.pixelfx.holy.featherBurstGlowAtomicBounds") {
    PixelFx fx(PixelFxKind::FeatherBurst);
    REQUIRE(fx.settings().holy.glow == 0xffc0f0ffu);
    sampleSimulated(fx, .3);
    const auto document = fx.configurationValue();
    for (uint32_t color : {0u, 0x00ffffffu, 0xfeffffffu}) {
        auto s      = fx.settings();
        s.holy.glow = color;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FeatherBurst, s));
        REQUIRE(fx.configurationValue() == document);
    }
    auto s         = fx.settings();
    s.holy.glow    = 0xffffaa55u;
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::FeatherBurst, s);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().holy.glow == 0xffffaa55u);
}

TEST_CASE("stylize.pixelfx.holy.divineShieldPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 18> times{{0, 0.008333333333333333, 0.15, 0.3, 0.5916666666666667, 0.6,
                                            0.6083333333333333, 1.1916666666666667, 1.2, 1.2083333333333333,
                                            1.3166666666666667, 1.325, 1.5, 1.7916666666666667, 1.8, 1.8083333333333333,
                                            2.3916666666666666, 2.4}};
    constexpr std::array<uint32_t, 36> expected{
        {1184971521u, 3609453267u, 1236654416u, 3312602033u, 3106318935u, 548470322u,  3903837343u, 2489795470u,
         2065162593u, 4172590875u, 2747427103u, 3540793328u, 557447098u,  2435549138u, 1693686743u, 1415621928u,
         3389692892u, 2959359850u, 502545708u,  635640047u,  1196980996u, 1945214206u, 1601881971u, 345974434u,
         4116288041u, 3540785612u, 3525821623u, 2664878391u, 1171314114u, 4195806048u, 3387205603u, 603263541u,
         3864532384u, 3681360246u, 1184971521u, 3609453267u}};
    PixelFx fx(PixelFxKind::DivineShield), other(PixelFxKind::DivineShield);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::DivineShield, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::DivineShield, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DivineShield, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::DivineShield, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::DivineShield, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(2.4);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), seam.begin()));
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


TEST_CASE("stylize.pixelfx.holy.sourceNormalizedHypotBits") {
    struct Sample {
        double   x, y;
        uint64_t bits;
        bool     nan;
    };
    const std::array<Sample, 179> samples{
        {{0, 0, 0x0ull, false},
         {-0.0, -0.0, 0x0ull, false},
         {3, 4, 0x4014000000000000ull, false},
         {-7.5, -24.5, 0x40399f4c2a7dbb33ull, false},
         {1e+308, 1e+308, 0x7fe92c80954c51f5ull, false},
         {std::numeric_limits<double>::denorm_min(), std::numeric_limits<double>::denorm_min(), 0x1ull, false},
         {1e-300, 1e+300, 0x7e37e43c8800759cull, false},
         {std::numeric_limits<double>::max(), 1, 0x7fefffffffffffffull, false},
         {INFINITY, NAN, 0x7ff0000000000000ull, false},
         {NAN, 1, 0x7ff8000000000000ull, true},
         {-29.5, -29.5, 0x4044dc12067d4b21ull, false},
         {-24.5, -29.5, 0x40432c6dbd42b196ull, false},
         {-19.5, -29.5, 0x4041ae636e8fb45aull, false},
         {-14.5, -29.5, 0x40406f7b9c943ce7ull, false},
         {-9.5, -29.5, 0x403efdef6a447beaull, false},
         {-4.5, -29.5, 0x403dd75bf04c98d0ull, false},
         {0.5, -29.5, 0x403d8115accb2104ull, false},
         {5.5, -29.5, 0x403e02220eb87c9aull, false},
         {10.5, -29.5, 0x403f501c9d5485b2ull, false},
         {15.5, -29.5, 0x4040a97e40846cafull, false},
         {20.5, -29.5, 0x4041f6363a4074afull, false},
         {25.5, -29.5, 0x40437f2dee73ac04ull, false},
         {30.5, -29.5, 0x4045375593016446ull, false},
         {-29.5, -24.5, 0x40432c6dbd42b196ull, false},
         {-24.5, -24.5, 0x404152f9467969c9ull, false},
         {-19.5, -24.5, 0x403f501c9d5485b3ull, false},
         {-14.5, -24.5, 0x403c7822d84b57c4ull, false},
         {-9.5, -24.5, 0x403a470180d16a3eull, false},
         {-4.5, -24.5, 0x4038e8eb1ad2811aull, false},
         {0.5, -24.5, 0x4038814e552142a1ull, false},
         {5.5, -24.5, 0x40391c192b70c1a4ull, false},
         {10.5, -24.5, 0x403aa7bb926a0be2ull, false},
         {15.5, -24.5, 0x403cfdcaf353049full, false},
         {20.5, -24.5, 0x403ff1fceea843f9ull, false},
         {25.5, -24.5, 0x4041ae636e8fb45aull, false},
         {30.5, -24.5, 0x40438f90c30efae6ull, false},
         {-29.5, -19.5, 0x4041ae636e8fb45aull, false},
         {-24.5, -19.5, 0x403f501c9d5485b3ull, false},
         {-19.5, -19.5, 0x403b93c10ceb10e2ull, false},
         {-14.5, -19.5, 0x40384cda48e32f6cull, false},
         {-9.5, -19.5, 0x4035b0e628f84ed5ull, false},
         {-4.5, -19.5, 0x40340332f1b4762aull, false},
         {0.5, -19.5, 0x403381a40895d77dull, false},
         {5.5, -19.5, 0x403442c3c30b16d1ull, false},
         {10.5, -19.5, 0x403625b12a8664c6ull, false},
         {15.5, -19.5, 0x4038e8eb1ad28119ull, false},
         {20.5, -19.5, 0x403c4b092bff80c5ull, false},
         {25.5, -19.5, 0x40400cfabc462944ull, false},
         {30.5, -19.5, 0x404219b4c167cf7cull, false},
         {-29.5, -14.5, 0x40406f7b9c943ce7ull, false},
         {-24.5, -14.5, 0x403c7822d84b57c4ull, false},
         {-19.5, -14.5, 0x40384cda48e32f6cull, false},
         {-14.5, -14.5, 0x4034818f8ce34e32ull, false},
         {-9.5, -14.5, 0x403155be5aa796bcull, false},
         {-4.5, -14.5, 0x402e5d4cc90c59c2ull, false},
         {0.5, -14.5, 0x402d0469986885fdull, false},
         {5.5, -14.5, 0x402f0420c1e6308dull, false},
         {10.5, -14.5, 0x4031e70b24cc1a2dull, false},
         {15.5, -14.5, 0x40353998a295e63eull, false},
         {20.5, -14.5, 0x40391c192b70c1a6ull, false},
         {25.5, -14.5, 0x403d559364979037ull, false},
         {30.5, -14.5, 0x4040e2b99df438f8ull, false},
         {-29.5, -9.5, 0x403efdef6a447beaull, false},
         {-24.5, -9.5, 0x403a470180d16a3eull, false},
         {-19.5, -9.5, 0x4035b0e628f84ed5ull, false},
         {-14.5, -9.5, 0x403155be5aa796bcull, false},
         {-9.5, -9.5, 0x402adebc19b71703ull, false},
         {-4.5, -9.5, 0x402506177f5491bbull, false},
         {0.5, -9.5, 0x402306bb705ae7c3ull, false},
         {5.5, -9.5, 0x4025f45a01d483b4ull, false},
         {10.5, -9.5, 0x402c51d19a043913ull, false},
         {15.5, -9.5, 0x40322dfe21393874ull, false},
         {20.5, -9.5, 0x403698209334a4f6ull, false},
         {25.5, -9.5, 0x403b364e20ee48f5ull, false},
         {30.5, -9.5, 0x403ff1fceea843f9ull, false},
         {-29.5, -4.5, 0x403dd75bf04c98d0ull, false},
         {-24.5, -4.5, 0x4038e8eb1ad2811aull, false},
         {-19.5, -4.5, 0x40340332f1b4762aull, false},
         {-14.5, -4.5, 0x402e5d4cc90c59c2ull, false},
         {-9.5, -4.5, 0x402506177f5491bbull, false},
         {-4.5, -4.5, 0x401974b2334f2347ull, false},
         {0.5, -4.5, 0x40121c5b70d9f824ull, false},
         {5.5, -4.5, 0x401c6ce322982a40ull, false},
         {10.5, -4.5, 0x4026d8e9eb365354ull, false},
         {15.5, -4.5, 0x403023d7da22d2d0ull, false},
         {20.5, -4.5, 0x4034fcf39697f0d2ull, false},
         {25.5, -4.5, 0x4039e4de269be7f8ull, false},
         {30.5, -4.5, 0x403ed486ad7f1f6cull, false},
         {-29.5, 0.5, 0x403d8115accb2104ull, false},
         {-24.5, 0.5, 0x4038814e552142a1ull, false},
         {-19.5, 0.5, 0x403381a40895d77dull, false},
         {-14.5, 0.5, 0x402d0469986885fdull, false},
         {-9.5, 0.5, 0x402306bb705ae7c3ull, false},
         {-4.5, 0.5, 0x40121c5b70d9f824ull, false},
         {0.5, 0.5, 0x3fe6a09e667f3bcdull, false},
         {5.5, 0.5, 0x401617398f2aaa48ull, false},
         {10.5, 0.5, 0x402506177f5491baull, false},
         {15.5, 0.5, 0x402f0420c1e6308cull, false},
         {20.5, 0.5, 0x4034818f8ce34e31ull, false},
         {25.5, 0.5, 0x40398141395a05a3ull, false},
         {30.5, 0.5, 0x403e810c9276822bull, false},
         {-29.5, 5.5, 0x403e02220eb87c9aull, false},
         {-24.5, 5.5, 0x40391c192b70c1a4ull, false},
         {-19.5, 5.5, 0x403442c3c30b16d1ull, false},
         {-14.5, 5.5, 0x402f0420c1e6308dull, false},
         {-9.5, 5.5, 0x4025f45a01d483b4ull, false},
         {-4.5, 5.5, 0x401c6ce322982a40ull, false},
         {0.5, 5.5, 0x401617398f2aaa48ull, false},
         {5.5, 5.5, 0x401f1cd9cceef23aull, false},
         {10.5, 5.5, 0x4027b4dfc079258dull, false},
         {15.5, 5.5, 0x4030726700ceb219ull, false},
         {20.5, 5.5, 0x40353998a295e63eull, false},
         {25.5, 5.5, 0x403a161dfa425855ull, false},
         {30.5, 5.5, 0x403efdef6a447becull, false},
         {-29.5, 10.5, 0x403f501c9d5485b2ull, false},
         {-24.5, 10.5, 0x403aa7bb926a0be2ull, false},
         {-19.5, 10.5, 0x403625b12a8664c6ull, false},
         {-14.5, 10.5, 0x4031e70b24cc1a2dull, false},
         {-9.5, 10.5, 0x402c51d19a043913ull, false},
         {-4.5, 10.5, 0x4026d8e9eb365354ull, false},
         {0.5, 10.5, 0x402506177f5491baull, false},
         {5.5, 10.5, 0x4027b4dfc079258dull, false},
         {10.5, 10.5, 0x402db2cfe686fe7dull, false},
         {15.5, 10.5, 0x4032b8bdbdd8a195ull, false},
         {20.5, 10.5, 0x4037085787dc462bull, false},
         {25.5, 10.5, 0x403b93c10ceb10e2ull, false},
         {30.5, 10.5, 0x404020de3d7d8f3dull, false},
         {-29.5, 15.5, 0x4040a97e40846cafull, false},
         {-24.5, 15.5, 0x403cfdcaf353049full, false},
         {-19.5, 15.5, 0x4038e8eb1ad28119ull, false},
         {-14.5, 15.5, 0x40353998a295e63eull, false},
         {-9.5, 15.5, 0x40322dfe21393874ull, false},
         {-4.5, 15.5, 0x403023d7da22d2d0ull, false},
         {0.5, 15.5, 0x402f0420c1e6308cull, false},
         {5.5, 15.5, 0x4030726700ceb219ull, false},
         {10.5, 15.5, 0x4032b8bdbdd8a195ull, false},
         {15.5, 15.5, 0x4035eb99734b41efull, false},
         {20.5, 15.5, 0x4039b33ff33cc995ull, false},
         {25.5, 15.5, 0x403dd75bf04c98d1ull, false},
         {30.5, 15.5, 0x40411b358505a916ull, false},
         {-29.5, 20.5, 0x4041f6363a4074afull, false},
         {-24.5, 20.5, 0x403ff1fceea843f9ull, false},
         {-19.5, 20.5, 0x403c4b092bff80c5ull, false},
         {-14.5, 20.5, 0x40391c192b70c1a6ull, false},
         {-9.5, 20.5, 0x403698209334a4f6ull, false},
         {-4.5, 20.5, 0x4034fcf39697f0d2ull, false},
         {0.5, 20.5, 0x4034818f8ce34e31ull, false},
         {5.5, 20.5, 0x40353998a295e63eull, false},
         {10.5, 20.5, 0x4037085787dc462bull, false},
         {15.5, 20.5, 0x4039b33ff33cc995ull, false},
         {20.5, 20.5, 0x403cfdcaf353049full, false},
         {25.5, 20.5, 0x40405bf7afcb07b1ull, false},
         {30.5, 20.5, 0x40425fe422bf55e8ull, false},
         {-29.5, 25.5, 0x40437f2dee73ac04ull, false},
         {-24.5, 25.5, 0x4041ae636e8fb45aull, false},
         {-19.5, 25.5, 0x40400cfabc462944ull, false},
         {-14.5, 25.5, 0x403d559364979037ull, false},
         {-9.5, 25.5, 0x403b364e20ee48f5ull, false},
         {-4.5, 25.5, 0x4039e4de269be7f8ull, false},
         {0.5, 25.5, 0x40398141395a05a3ull, false},
         {5.5, 25.5, 0x403a161dfa425855ull, false},
         {10.5, 25.5, 0x403b93c10ceb10e2ull, false},
         {15.5, 25.5, 0x403dd75bf04c98d1ull, false},
         {20.5, 25.5, 0x40405bf7afcb07b1ull, false},
         {25.5, 25.5, 0x404207fe39ad63a7ull, false},
         {30.5, 25.5, 0x4043e0b450873627ull, false},
         {-29.5, 30.5, 0x4045375593016446ull, false},
         {-24.5, 30.5, 0x40438f90c30efae6ull, false},
         {-19.5, 30.5, 0x404219b4c167cf7cull, false},
         {-14.5, 30.5, 0x4040e2b99df438f8ull, false},
         {-9.5, 30.5, 0x403ff1fceea843f9ull, false},
         {-4.5, 30.5, 0x403ed486ad7f1f6cull, false},
         {0.5, 30.5, 0x403e810c9276822bull, false},
         {5.5, 30.5, 0x403efdef6a447becull, false},
         {10.5, 30.5, 0x404020de3d7d8f3dull, false},
         {15.5, 30.5, 0x40411b358505a916ull, false},
         {20.5, 30.5, 0x40425fe422bf55e8ull, false},
         {25.5, 30.5, 0x4043e0b450873627ull, false},
         {30.5, 30.5, 0x40459116f9b144ffull, false}}};
    for (auto sample : samples) {
        const double v = jsHypot2(sample.x, sample.y);
        if (sample.nan)
            REQUIRE(std::isnan(v));
        else
            REQUIRE(std::bit_cast<uint64_t>(v) == sample.bits);
    }
}
TEST_CASE("stylize.pixelfx.holy.sourceActorOutlineAndFractionalQueries") {
    struct Expected {
        double   x, y, top, bottom;
        size_t   edgeCount, outCount;
        uint32_t edge, out, query;
    };
    const std::array<Expected, 7> expected{{{-6, -19, -19, 0, 56, 57, 1083393920u, 3052571198u, 2710720880u},
                                            {-12, -38, -38, 0, 91, 95, 2028554077u, 2803192992u, 2713618387u},
                                            {-12, -70, -70, -50, 84, 88, 1321613381u, 335362629u, 3512693475u},
                                            {-12, -70, -70, -50, 0, 0, 2166136261u, 2166136261u, 3194043999u},
                                            {-3.5, -65.5, -65.5, -57.5, 24, 28, 3725699365u, 916878757u, 3194043999u},
                                            {-2.5, -65.5, -65.5, -57.5, 22, 26, 509676592u, 2806501385u, 322483412u},
                                            {-0.5, -50.5, -50.5, -49.5, 1, 4, 2180607561u, 3304991028u, 3194043999u}}};
    const auto                    hash = [](const std::vector<Point>& points) {
        uint32_t h = 2166136261u;
        for (auto p : points)
            for (double value : {p.x, p.y}) {
                const auto bits = std::bit_cast<uint64_t>(value);
                for (int shift = 0; shift < 64; shift += 8) h = (h ^ uint8_t(bits >> shift)) * 16777619u;
            }
        return h;
    };
    for (size_t i = 0; i < expected.size(); ++i) {
        PixelFx fx(PixelFxKind::DivineShield);
        auto    a = fx.settings().actor;
        if (i == 0) {
            auto settings         = fx.settings();
            settings.actor.source = PixelFxActorSource::Hero;
            auto candidate        = PixelFx::prepareConfiguration(PixelFxKind::DivineShield, settings);
            REQUIRE(candidate);
            REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
            a = fx.settings().actor;
        } else if (i >= 2) {
            a        = {};
            a.source = PixelFxActorSource::Custom;
            a.width  = i < 4 ? 24 : i == 4 ? 6 : i == 5 ? 5 : 1;
            a.height = i < 4 ? 20 : i < 6 ? 8 : 1;
            a.x      = expected[i].x;
            a.y      = expected[i].y;
            a.mask.assign(size_t(a.width * a.height), i == 3 ? 0 : 1);
            a.pixels.assign(a.mask.size(), 0);
        }
        const auto shape = holy::prepareShape(a);
        const auto e     = expected[i];
        REQUIRE(shape.x == e.x);
        REQUIRE(shape.y == e.y);
        REQUIRE(shape.top == e.top);
        REQUIRE(shape.bottom == e.bottom);
        REQUIRE(shape.edge.size() == e.edgeCount);
        REQUIRE(shape.out.size() == e.outCount);
        REQUIRE(hash(shape.edge) == e.edge);
        REQUIRE(hash(shape.out) == e.out);
        uint32_t query = 2166136261u;
        for (double y : {a.y - 1, a.y, a.y + .5, a.y + .9, a.y + a.height - 1, a.y + a.height, 0.})
            for (double x : {a.x - 1, a.x, a.x + .5, a.x + .9, a.x + a.width - 1, a.x + a.width, 0.})
                query = (query ^ uint32_t(holy::containsActorCell(a, x, y))) * 16777619u;
        REQUIRE(query == e.query);
    }
}
TEST_CASE("stylize.pixelfx.holy.divineShieldRadiusAtomicBounds") {
    PixelFx fx(PixelFxKind::DivineShield);
    REQUIRE(fx.settings().holy.radius == 29.);
    fx.render(.5);
    const auto document = fx.configurationValue();
    for (double radius :
         {0., .99, 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto settings        = fx.settings();
        settings.holy.radius = radius;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DivineShield, settings));
        REQUIRE(fx.configurationValue() == document);
    }
    for (double radius : {1., 21., 22., 256.}) {
        auto settings        = fx.settings();
        settings.holy.radius = radius;
        auto candidate       = PixelFx::prepareConfiguration(PixelFxKind::DivineShield, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.5);
        REQUIRE(fx.settings().holy.radius == radius);
    }
}

TEST_CASE("stylize.pixelfx.holy.healPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 18> times{{0, 0.008333333333333333, 0.075, 0.15, 0.15833333333333333,
                                            0.16666666666666666, 0.3, 0.5, 0.7333333333333333, 0.7416666666666667, 0.75,
                                            0.775, 0.7833333333333333, 1, 1.2, 1.8916666666666666, 1.9, 2.5}};
    constexpr std::array<uint32_t, 36> expected{
        {4240760047u, 202442181u,  4196429085u, 202442181u,  3417426761u, 2386336940u, 3686044583u, 4007427754u,
         1865660812u, 3133106306u, 1684295175u, 3274344433u, 3085916768u, 2770875177u, 3453227939u, 1573356514u,
         1377272246u, 3574110327u, 583586599u,  1747684106u, 2491026433u, 1277098121u, 3464655676u, 1767995403u,
         1535989295u, 1669080848u, 2486977970u, 4114482192u, 1805224486u, 2285401325u, 202442181u,  202442181u,
         202442181u,  202442181u,  202442181u,  202442181u}};
    PixelFx fx(PixelFxKind::Heal), other(PixelFxKind::Heal);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Heal, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Heal, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Heal, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Heal, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Heal, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 2.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}


TEST_CASE("stylize.pixelfx.holy.sourceActorRecolorTraversal") {
    constexpr std::array<std::array<uint32_t, 2>, 4> sums{{{{4210965934u, 1995431365u}},
                                                           {{731281326u, 1995431365u}},
                                                           {{616938953u, 1995431365u}},
                                                           {{616938953u, 1995431365u}}}};
    PixelFxActor                                     actor;
    actor.source = PixelFxActorSource::Custom;
    actor.width  = 3;
    actor.height = 2;
    actor.x      = -1.5;
    actor.y      = -2.5;
    actor.mask.assign(6, 0);
    actor.pixels = {0xff335577u, 0, 0xffabcdefu, 0x123456u, 0xffffffffu, 0xff000000u};
    const PixelFxInfo info{PixelFxKind::Heal, "recolor-fixture", 32, 32, 16, 16, 1.9, false};
    for (size_t i = 0; i < 4; ++i) {
        std::vector<uint32_t> color(1024), light(1024);
        Paint                 p{info, color, light, 1};
        p.circle(0, 0, 12, hex(0x20202a));
        if (i >= 2) {
            p.tx = 7;
            p.ty = -3;
            p.fx = -1;
            p.cs = std::cos(.2);
            p.sn = std::sin(.2);
        }
        const auto recolor = [](uint32_t c, double x, double y) -> uint32_t {
            return x < 0 && y < -2 ? 0 : mix(c, hex(0xffeeaa), clamp((x + y + 6) / 12));
        };
        if (i % 2)
            holy::recolor(p, actor, recolor, [](double x, double y) { return .5 + .04 * x - .03 * y; });
        else
            holy::recolor(p, actor, recolor, .65);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(std::span(color))) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(std::span(light))) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i][0]);
        REQUIRE(l == sums[i][1]);
    }
}
TEST_CASE("stylize.pixelfx.holy.healRequiresActorBeforePublication") {
    PixelFx fx(PixelFxKind::Heal);
    sampleSimulated(fx, .5);
    const auto                  document = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.actor.source                = PixelFxActorSource::None;
    auto rejected                        = PixelFx::prepareConfiguration(PixelFxKind::Heal, settings);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/actor/source");
    REQUIRE(fx.configurationValue() == document);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    auto bad = document;
    bad.find("options")->set("actor", eve::Value::object({{"schema", eve::Value("eve.pixelfx.actor")},
                                                          {"version", eve::Value(int64_t(1))},
                                                          {"source", eve::Value("none")}}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Heal, bad));
    for (auto kind : {PixelFxKind::Heal, PixelFxKind::DivineShield}) {
        auto metadata = PixelFx::describeParameters(kind);
        REQUIRE(metadata);
        bool found = false;
        for (const auto& p : *metadata.value().find("parameters")->getIf<eve::Value::Array>()) {
            if (p.find("name")->asString() != "actor") continue;
            found               = true;
            const auto* sources = p.find("constraints")->find("sources")->getIf<eve::Value::Array>();
            REQUIRE(sources);
            const bool none = std::any_of(sources->begin(), sources->end(),
                                          [](const eve::Value& v) { return v.asString() == "none"; });
            REQUIRE(none == (kind == PixelFxKind::DivineShield));
        }
        REQUIRE(found);
    }
    for (auto source : {PixelFxActorSource::Hero, PixelFxActorSource::Dummy}) {
        auto s         = fx.settings();
        s.actor.source = source;
        auto candidate = PixelFx::prepareConfiguration(PixelFxKind::Heal, s);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        sampleSimulated(fx, .5);
    }
}

TEST_CASE("stylize.pixelfx.holy.healAoePhasesMaterialsAndTransactions") {
    constexpr std::array<double, 19> times{{0, 0.008333333333333333, 0.25, 0.2916666666666667, 0.3, 0.30833333333333335,
                                            0.4, 0.5, 0.7, 0.9, 1, 1.2, 1.3, 1.55, 1.9, 2.3, 2.6, 2.9, 3.5}};
    constexpr std::array<uint32_t, 38> expected{
        {3536654097u, 3543769157u, 588805211u,  642989286u,  1955198262u, 146318755u,  2012995612u, 1325508338u,
         3398446096u, 1956150056u, 2330981751u, 2325585789u, 2656050059u, 3305844032u, 3495396785u, 342038879u,
         2914153968u, 3551670228u, 2874474713u, 3667263947u, 1197800823u, 2099980412u, 2618947507u, 3427063718u,
         992316069u,  2076242941u, 3158283142u, 3172223174u, 1956122362u, 1093150639u, 358234805u,  1827276918u,
         1999314373u, 1999314373u, 1999314373u, 1999314373u, 1999314373u, 1999314373u}};
    PixelFx fx(PixelFxKind::HealAoe), other(PixelFxKind::HealAoe);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::HealAoe, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::HealAoe, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HealAoe, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HealAoe, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HealAoe, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 3.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.holy.healAoeReachPublication") {
    PixelFx fx(PixelFxKind::HealAoe);
    sampleSimulated(fx, .75);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> pixels(fx.color().begin(), fx.color().end());
    for (double reach :
         {0., -.01, 256.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto settings       = fx.settings();
        settings.holy.reach = reach;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HealAoe, settings));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), pixels.begin()));
    }
    for (double reach : {1., 32.5, 104., 256.}) {
        auto settings       = fx.settings();
        settings.holy.reach = reach;
        auto prepared       = PixelFx::prepareConfiguration(PixelFxKind::HealAoe, settings);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        sampleSimulated(fx, .75);
        REQUIRE(fx.settings().holy.reach == reach);
        auto value = fx.configurationValue();
        REQUIRE(value.find("options")->find("reach")->asDouble() == reach);
        auto decoded = PixelFx::prepareConfigurationValue(PixelFxKind::HealAoe, value);
        REQUIRE(decoded);
        REQUIRE(fx.commitConfiguration(std::move(decoded).takeValue()));
    }
}

TEST_CASE("stylize.pixelfx.holy.holyPillarPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 24>   times{{0,
                                              0.008333333333333333,
                                              0.2916666666666667,
                                              0.3,
                                              0.30833333333333335,
                                              0.65,
                                              0.6583333333333333,
                                              0.6666666666666666,
                                              0.75,
                                              1,
                                              1.5,
                                              2.0416666666666665,
                                              2.05,
                                              2.058333333333333,
                                              2.441666666666667,
                                              2.45,
                                              2.4583333333333335,
                                              2.7416666666666667,
                                              2.75,
                                              2.7583333333333333,
                                              3,
                                              3.4916666666666667,
                                              3.5,
                                              4}};
    constexpr std::array<uint32_t, 48> expected{
        {2223111621u, 2223111621u, 3852686269u, 189820266u,  2571170693u, 3482253870u, 2894069816u, 874744536u,
         2196097348u, 1765524905u, 4104269301u, 392085001u,  383203222u,  1801375984u, 733462317u,  2735822308u,
         3723699270u, 1428355712u, 1912739779u, 844874554u,  887552519u,  1803201224u, 2962352262u, 3257065554u,
         168387281u,  2831725593u, 485356460u,  3438754265u, 24630162u,   2168144893u, 1172583921u, 4072582499u,
         1389080036u, 729666121u,  3711053198u, 564671373u,  1200875870u, 378472048u,  2471056862u, 1989062189u,
         2201814186u, 1027424220u, 2223111621u, 2223111621u, 2223111621u, 2223111621u, 2223111621u, 2223111621u}};
    PixelFx fx(PixelFxKind::HolyPillar), other(PixelFxKind::HolyPillar);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::HolyPillar, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::HolyPillar, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HolyPillar, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HolyPillar, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HolyPillar, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 4.);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.holy.holyPillarShaftPublication") {
    PixelFx fx(PixelFxKind::HolyPillar);
    sampleSimulated(fx, .75);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (int n : {-1, 0, 2, 65}) {
        auto settings        = fx.settings();
        settings.holy.shafts = n;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HolyPillar, settings));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    auto fraction = before;
    fraction.find("options")->set("shafts", eve::Value(3.5));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HolyPillar, fraction));
    for (int n : {3, 8, 64}) {
        auto settings        = fx.settings();
        settings.holy.shafts = n;
        auto prepared        = PixelFx::prepareConfiguration(PixelFxKind::HolyPillar, settings);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        sampleSimulated(fx, .75);
        REQUIRE(fx.settings().holy.shafts == n);
        auto value = fx.configurationValue();
        auto rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HolyPillar, value);
        REQUIRE(rt);
        REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
        REQUIRE(fx.configurationValue() == value);
    }
}

TEST_CASE("stylize.pixelfx.holy.smitePhasesMaterialsAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.4,
                                              0.4166666666666667,
                                              0.425,
                                              0.43333333333333335,
                                              0.525,
                                              0.5333333333333333,
                                              0.5416666666666666,
                                              0.55,
                                              0.6,
                                              0.75,
                                              1,
                                              1.25,
                                              1.5,
                                              1.75,
                                              2,
                                              2.5,
                                              2.7916666666666665,
                                              2.8,
                                              3.5}};
    constexpr std::array<uint32_t, 42> expected{
        {519947651u,  4016160197u, 1214926779u, 3995409533u, 1715404355u, 2719500780u, 443333456u,
         1119076540u, 772664114u,  476290049u,  28352904u,   4260407987u, 2027191984u, 2224687898u,
         3122904934u, 995603453u,  2631802069u, 917890054u,  4152008628u, 3074312230u, 2762236255u,
         3196315423u, 1585935736u, 3510100677u, 1973117909u, 2032639037u, 600823508u,  544289133u,
         2005408913u, 2871583943u, 375699825u,  789673051u,  2882552752u, 2847157711u, 3134817709u,
         423297693u,  4016160197u, 4016160197u, 4016160197u, 4016160197u, 4016160197u, 4016160197u}};
    PixelFx fx(PixelFxKind::Smite), other(PixelFxKind::Smite);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Smite, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Smite, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Smite, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Smite, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Smite, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 3.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.holy.smiteStonePublication") {
    PixelFx fx(PixelFxKind::Smite);
    sampleSimulated(fx, 1.);
    const auto                  document = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        invalid = fx.settings();
    invalid.holy.stone                  = 0x00ffffffu;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Smite, invalid));
    REQUIRE(fx.configurationValue() == document);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    auto settings       = fx.settings();
    settings.holy.stone = 0xff2233eeu;
    auto candidate      = PixelFx::prepareConfiguration(PixelFxKind::Smite, settings);
    REQUIRE(candidate);
    settings.holy.stone = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sampleSimulated(fx, 1.);
    REQUIRE(fx.settings().holy.stone == 0xff2233eeu);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    auto value = fx.configurationValue();
    auto rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Smite, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto bad = value;
    bad.find("options")->set("stone", eve::Value(4294967296.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Smite, bad));
    REQUIRE(fx.configurationValue() == value);
}


TEST_CASE("stylize.pixelfx.holy.resurrectionWingSourceRaster") {
    constexpr std::array<uint32_t, 4> seeds{5u, 0u, 123u, 4294967295u},
        states{601530475u, 601530471u, 601530593u, 601530469u};
    constexpr std::array<double, 25>    times{0,    1.1,  1.1083333333333334,
                                              1.2,  1.24, 1.5,
                                              1.55, 1.95, 2.1,
                                              2.25, 2.28, 2.3,
                                              2.37, 2.4,  2.5,
                                              2.8,  2.95, 2.975,
                                              3,    3.05, 3.1,
                                              3.15, 3.2,  3.3,
                                              4};
    constexpr std::array<uint32_t, 400> sums{
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  2016975313u, 304276315u,  71654689u,
        3320399227u, 3723543197u, 3541336494u, 3625860185u, 106562622u,  3625860185u, 4182908291u, 4148516681u,
        216884770u,  3064194825u, 4182040094u, 979935069u,  4018335005u, 648340785u,  3570484072u, 3180106413u,
        4027716649u, 4004404785u, 3118041946u, 335435397u,  3700791912u, 863968885u,  1042756807u, 3355709853u,
        4044080194u, 3625860185u, 3092588653u, 3625860185u, 1723614192u, 3625860185u, 2715042731u, 761964217u,
        3829788456u, 1280105469u, 1932774190u, 2475836374u, 511603958u,  3427811008u, 414752197u,  414752197u,
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  1228986950u,
        734877044u,  2833876371u, 2291544881u, 39139736u,   1848336363u, 4220909833u, 2881723514u, 4220909833u,
        3438090332u, 2620209069u, 1380282038u, 3413061644u, 737999494u,  254280880u,  10086589u,   1308591123u,
        586662241u,  3342193975u, 2024271822u, 1762856311u, 3194540398u, 3884174769u, 1961911787u, 1979612264u,
        1323260028u, 2587124885u, 128491541u,  4220909833u, 1399928203u, 4220909833u, 2146324332u, 4220909833u,
        1635419274u, 3986479719u, 1118820468u, 50720558u,   803828902u,  2515284063u, 2001227424u, 3220129230u,
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,
        414752197u,  2016975313u, 304276315u,  71654689u,   3320399227u, 3723543197u, 3541336494u, 3625860185u,
        106562622u,  3625860185u, 4182908291u, 4148516681u, 216884770u,  3064194825u, 4182040094u, 979935069u,
        4018335005u, 648340785u,  3570484072u, 3180106413u, 4027716649u, 4004404785u, 3118041946u, 335435397u,
        3700791912u, 863968885u,  1042756807u, 3355709853u, 4044080194u, 3625860185u, 3801730676u, 3625860185u,
        1723614192u, 3625860185u, 580908404u,  761964217u,  2934866122u, 1134169261u, 3016400119u, 97790057u,
        1241194000u, 263372029u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,
        414752197u,  414752197u,  414752197u,  1228986950u, 734877044u,  2833876371u, 2291544881u, 39139736u,
        1848336363u, 4220909833u, 2881723514u, 4220909833u, 3438090332u, 2620209069u, 1380282038u, 3413061644u,
        737999494u,  254280880u,  10086589u,   1308591123u, 586662241u,  3342193975u, 2024271822u, 1762856311u,
        3194540398u, 3884174769u, 1961911787u, 1979612264u, 1323260028u, 2587124885u, 128491541u,  4220909833u,
        3383307881u, 4220909833u, 2146324332u, 4220909833u, 440850673u,  3986479719u, 1835719282u, 33096100u,
        4011579312u, 1065794979u, 3611766309u, 2428151202u, 414752197u,  414752197u,  414752197u,  414752197u,
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  2016975313u, 304276315u,  71654689u,
        3320399227u, 3723543197u, 3541336494u, 3625860185u, 106562622u,  3625860185u, 4182908291u, 4148516681u,
        216884770u,  3064194825u, 4182040094u, 979935069u,  4018335005u, 648340785u,  3570484072u, 3180106413u,
        4027716649u, 4004404785u, 3118041946u, 335435397u,  3700791912u, 863968885u,  1042756807u, 3355709853u,
        4044080194u, 3625860185u, 2507984476u, 3625860185u, 3709449880u, 3625860185u, 3160452995u, 1692096642u,
        2265475472u, 3809916300u, 1855224478u, 2732006674u, 2990621745u, 2822025038u, 414752197u,  414752197u,
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  1228986950u,
        734877044u,  2833876371u, 2291544881u, 39139736u,   1848336363u, 4220909833u, 2881723514u, 4220909833u,
        3438090332u, 2620209069u, 1380282038u, 3413061644u, 737999494u,  254280880u,  10086589u,   1308591123u,
        586662241u,  3342193975u, 2024271822u, 1762856311u, 3194540398u, 3884174769u, 1961911787u, 1979612264u,
        1323260028u, 2587124885u, 128491541u,  4220909833u, 3240328957u, 4220909833u, 2523370102u, 4220909833u,
        959068986u,  1760079764u, 1360335551u, 1223411268u, 1335264096u, 828395495u,  4019169706u, 4097664098u,
        414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,
        414752197u,  2016975313u, 304276315u,  71654689u,   3320399227u, 3723543197u, 3541336494u, 3625860185u,
        106562622u,  3625860185u, 4182908291u, 4148516681u, 216884770u,  3064194825u, 4182040094u, 979935069u,
        4018335005u, 648340785u,  3570484072u, 3180106413u, 4027716649u, 4004404785u, 3118041946u, 335435397u,
        3700791912u, 863968885u,  1042756807u, 3355709853u, 4044080194u, 3625860185u, 2706550423u, 3625860185u,
        3709449880u, 3625860185u, 2303542729u, 1692096642u, 2973083168u, 501230608u,  445760352u,  770945317u,
        2089966441u, 263372029u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,  414752197u,
        414752197u,  414752197u,  414752197u,  1228986950u, 734877044u,  2833876371u, 2291544881u, 39139736u,
        1848336363u, 4220909833u, 2881723514u, 4220909833u, 3438090332u, 2620209069u, 1380282038u, 3413061644u,
        737999494u,  254280880u,  10086589u,   1308591123u, 586662241u,  3342193975u, 2024271822u, 1762856311u,
        3194540398u, 3884174769u, 1961911787u, 1979612264u, 1323260028u, 2587124885u, 128491541u,  4220909833u,
        1737016037u, 4220909833u, 2523370102u, 4220909833u, 580337781u,  1760079764u, 2525832352u, 1680719634u,
        2065824641u, 4024615198u, 2729539334u, 2428151202u, 414752197u,  414752197u,  414752197u,  414752197u};
    const std::array<uint32_t, 8> C{hex(0x3b1d2a), hex(0x7a3a2c), hex(0xc4652e), hex(0xf0a040),
                                    hex(0xffd25e), hex(0xfff0a0), hex(0xfffbe0), WHITE};
    const PixelFxInfo             info{PixelFxKind::Heal, "resurrection-wing-fixture", 256, 192, 128, 160, 4.8, false};
    size_t                        n = 0;
    for (size_t seed = 0; seed < seeds.size(); ++seed) {
        Rng rng(seeds[seed]);
        // Independent source closure consumes six five-draw rays and34six-draw motes before quill preparation.
        for (int i = 0; i < 6 * 5 + 34 * 6; ++i) rng.next();
        holy::ResurrectionWings wings(rng);
        REQUIRE(rng.state == states[seed]);
        for (bool transformed : {false, true})
            for (double t : times) {
                std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size());
                Paint                 p{info, color, light, 1};
                if (transformed) p.place(7, -3, true, .2);
                wings.draw(p, t, -1, C);
                wings.draw(p, t, 1, C);
                uint32_t c = 2166136261u, l = c;
                for (auto b : std::as_bytes(std::span(color))) c = (c ^ uint8_t(b)) * 16777619u;
                for (auto b : std::as_bytes(std::span(light))) l = (l ^ uint8_t(b)) * 16777619u;
                REQUIRE(c == sums[n++]);
                REQUIRE(l == sums[n++]);
            }
    }
    REQUIRE(n == sums.size());
}
TEST_CASE("stylize.pixelfx.holy.featherExplicitSidePreservesSourceDraws") {
    Rng            rng(123), control(123);
    holy::Feathers f(4.5), defaults(4.5);
    for (double side : {1., -1., 0.}) {
        holy::FeatherLaunch launch{0, -2, 30, 20, .6, 12, 3, .8};
        defaults.add(control, launch);
        launch.side = side;
        f.add(rng, launch);
        REQUIRE(f.list.back().side == side);
        REQUIRE(rng.state == control.state);
        REQUIRE(f.list.back().phase == defaults.list.back().phase);
        REQUIRE(f.list.back().angularVelocity == defaults.list.back().angularVelocity);
    }
    REQUIRE(rng.state == 4103143828u);
    for (int i = 0; i < 48; ++i) {
        f.update(1. / 120);
        defaults.update(1. / 120);
    }
    REQUIRE(f.list.size() == defaults.list.size());
    for (size_t i = 0; i < f.list.size(); ++i) {
        REQUIRE(f.list[i].x == defaults.list[i].x);
        REQUIRE(f.list[i].y == defaults.list[i].y);
        REQUIRE(f.list[i].angle == defaults.list[i].angle);
    }
}

TEST_CASE("stylize.pixelfx.holy.resurrectionPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.3333333333333333,
                                              0.3416666666666667,
                                              0.5,
                                              0.75,
                                              1.1,
                                              1.15,
                                              1.2416666666666667,
                                              1.5,
                                              1.95,
                                              2.25,
                                              2.375,
                                              2.8,
                                              2.95,
                                              3.5,
                                              3.85,
                                              4.3,
                                              4.791666666666667,
                                              4.8,
                                              5.5}};
    constexpr std::array<uint32_t, 42> expected{
        {2597944479u, 1071521221u, 2597944479u, 1071521221u, 1063074194u, 3288686170u, 3600344516u,
         210320254u,  3133787417u, 1490643540u, 753137145u,  3673588416u, 671780427u,  2151034671u,
         92034484u,   958368336u,  870540990u,  2747197586u, 392255261u,  3362683352u, 4071615491u,
         2418347423u, 3979910095u, 1210435402u, 2027221717u, 2878676173u, 1363728852u, 884314746u,
         78256849u,   3334039558u, 3715386723u, 3340527317u, 3394804822u, 3645644227u, 878707317u,
         325590530u,  1071521221u, 1071521221u, 1071521221u, 1071521221u, 1071521221u, 1071521221u}};
    PixelFx fx(PixelFxKind::Resurrection), other(PixelFxKind::Resurrection);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sampleSimulated(fx, times[i]);
        uint32_t c = 2166136261u, l = 2166136261u;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == expected[i * 2]);
        REQUIRE(l == expected[i * 2 + 1]);
    }
    sampleSimulated(fx, .375);
    sampleSimulated(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Resurrection, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sampleSimulated(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Resurrection, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Resurrection, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Resurrection, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Resurrection, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sampleSimulated(fx, 5.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sampleSimulated(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.holy.resurrectionAshActorPublication") {
    PixelFx fx(PixelFxKind::Resurrection);
    sampleSimulated(fx, .375);
    const auto                  document = fx.configurationValue();
    const std::vector<uint32_t> pixels(fx.color().begin(), fx.color().end());
    auto                        absent = fx.settings();
    absent.actor.source                = PixelFxActorSource::None;
    auto rejected                      = PixelFx::prepareConfiguration(PixelFxKind::Resurrection, absent);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/actor/source");
    REQUIRE(fx.configurationValue() == document);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), pixels.begin()));
    auto settings     = fx.settings();
    settings.holy.ash = {0xff403020u, 0xffc0a080u};
    auto candidate    = PixelFx::prepareConfiguration(PixelFxKind::Resurrection, settings);
    REQUIRE(candidate);
    settings.holy.ash[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sampleSimulated(fx, .375);
    REQUIRE(fx.settings().holy.ash.size() == 6u);
    REQUIRE(fx.settings().holy.ash[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), pixels.begin()));
    const auto value     = fx.configurationValue();
    auto       roundtrip = PixelFx::prepareConfigurationValue(PixelFxKind::Resurrection, value);
    REQUIRE(roundtrip);
    REQUIRE(fx.commitConfiguration(std::move(roundtrip).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        auto invalid     = fx.settings();
        invalid.holy.ash = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Resurrection, invalid));
    }
}

}  // namespace test_pixelfx_holy_unit
