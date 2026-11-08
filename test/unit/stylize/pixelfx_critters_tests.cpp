// Critter-family fixtures are separate from the core suite, which exceeds5000 lines.
#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include "stylize/pixelfx/Critter.h"
#include "stylize/pixelfx/CritterBubble.h"
#include "stylize/pixelfx/CritterFire.h"
#include "stylize/pixelfx/CritterMotion.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_critters_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
template <class T, size_t N>
uint32_t checksum(const std::array<T, N>& values) {
    uint32_t h = 2166136261u;
    for (auto b : std::as_bytes(std::span(values))) h = (h ^ uint8_t(b)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.critters.gridSourceFloatAlphaTagsAndStamp") {
    constexpr std::array<std::array<uint32_t, 5>, 6> expected{
        {{{298888206u, 3024827170u, 556837509u, 403886590u, 4108235205u}},
         {{298888206u, 4283952830u, 556837509u, 965075860u, 4108235205u}},
         {{4034999624u, 1431102194u, 1534488069u, 2122340613u, 4108235205u}},
         {{298888206u, 3238993462u, 556837509u, 2073899060u, 4108235205u}},
         {{1679031883u, 2658080408u, 4176527270u, 3375743050u, 4108235205u}},
         {{298888206u, 3938312837u, 556837509u, 2410621604u, 4108235205u}}}};
    const PixelFxInfo              info{PixelFxKind::CoinSpin, "grid-fixture", 40, 40, 20, 20, 1, false};
    constexpr std::array<Point, 4> pts{{{-4, 0}, {-1, -4}, {4.3, 1.2}, {1, 4}}};
    for (int v = 0; v < 6; ++v) {
        critter::Grid<12, 12> g(-5, -5);
        g.ellipse(
            -.3, .7, 3.2, 2.7, [](double nx, double ny) { return nx + ny < 0 ? hex(0x336699) : hex(0xcc8844); },
            v == 0 ? 1 : .42);
        g.shade(-3.7, -4.2, 2.8, -2.1, [](double x, double y, auto& out) {
            out.a   = .73123456;
            out.tag = 7;
            return x + y < -2 ? hex(0x8855aa) : 0u;
        });
        g.line(-4.5, 4.3, 3.2, -3.1, [](double u) { return u < .5 ? hex(0xeeeecc) : hex(0x224455); }, .65);
        g.poly(pts, [](double x, double y) { return x + y < 0 ? hex(0x447755) : hex(0xaaddee); }, v == 1 ? .999 : 1);
        g.set(-6, -6, WHITE);
        g.set(-1, -1, 0);
        g.erase(0, 0);
        g.outline(
            [&](double, double, int nx, int ny) {
                return v == 2 && ny < 0 ? 0u : nx + ny < 0 ? hex(0x112233) : hex(0x778899);
            },
            .8);
        if (v == 4) {
            g.clear();
            g.set(-1, -2, hex(0xaaff44), .35, 3);
        }
        if (v == 5) g.alpha.fill(.42f);
        std::array<uint32_t, 1600> color{}, light{};
        Paint                      p{info, color, light, 1};
        p.circle(0, 0, 9, hex(0x202030));
        if (v == 3) p.place(3, -2, true, .2);
        p.fade = .7;
        g.draw(p, .65, -.5, .5);
        REQUIRE(checksum(g.colors) == expected[size_t(v)][0]);
        REQUIRE(checksum(g.alpha) == expected[size_t(v)][1]);
        REQUIRE(checksum(g.tags) == expected[size_t(v)][2]);
        REQUIRE(checksum(color) == expected[size_t(v)][3]);
        REQUIRE(checksum(light) == expected[size_t(v)][4]);
        REQUIRE(g.get(-6, -6) == 0u);
        REQUIRE(g.cellTag(100, 100) == 0u);
    }
}
TEST_CASE("stylize.pixelfx.critters.seedPhaseSourceNumbers") {
    constexpr std::array<std::array<double, 2>, 5> values{{{{0, 1.5974000000000002}},
                                                           {{1, 0.375}},
                                                           {{2, 2.3526}},
                                                           {{123, 1.6421999999999892}},
                                                           {{4294967295, 2.589399438476563}}}};
    for (auto pair : values) REQUIRE(critter::shift(.375, 3.2, uint32_t(pair[0])) == pair[1]);
}
TEST_CASE("stylize.pixelfx.critters.butterflyPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 19>   times{{0, 0.008333333333333333, 0.075, 0.08333333333333333, 0.15,
                                              0.15833333333333333, 0.4666666666666667, 0.475, 0.9416666666666667, 0.95,
                                              0.9583333333333334, 1.4916666666666667, 1.5, 1.5083333333333333,
                                              2.5416666666666665, 2.55, 2.558333333333333, 3.191666666666667, 3.2}};
    constexpr std::array<uint32_t, 38> expected{
        {1730724331u, 1211837263u, 2248772351u, 2110532098u, 3938922870u, 4050957489u, 3728954382u, 1740243774u,
         3358138621u, 2149711891u, 262235497u,  2136758165u, 271785533u,  4146953308u, 4090130697u, 3278178407u,
         3934197345u, 3804685233u, 12204581u,   1550352448u, 996251989u,  132584643u,  279979099u,  3462137337u,
         2243872551u, 3738722360u, 3954271791u, 2985511188u, 1354553863u, 3518417543u, 2218899227u, 2905151671u,
         195788935u,  1355982189u, 3509534815u, 1111784712u, 1730724331u, 1211837263u}};
    PixelFx fx(PixelFxKind::Butterfly), other(PixelFxKind::Butterfly);
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
    auto settings          = fx.settings();
    settings.seed          = 123;
    settings.critter.spots = {0xff403020u, 0xffc0a080u};
    auto p                 = PixelFx::prepareConfiguration(PixelFxKind::Butterfly, settings);
    REQUIRE(p);
    settings.critter.spots[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.spots.size() == 4u);
    REQUIRE(fx.settings().critter.spots[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Butterfly, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings               = fx.settings();
        settings.critter.spots = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Butterfly, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Butterfly, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Butterfly, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(3.2);
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
TEST_CASE("stylize.pixelfx.critters.birdPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.06666666666666667,
                                              0.075,
                                              0.08333333333333333,
                                              0.14166666666666666,
                                              0.15,
                                              0.15833333333333333,
                                              0.36666666666666664,
                                              0.375,
                                              0.38333333333333336,
                                              1.4916666666666667,
                                              1.5,
                                              1.5083333333333333,
                                              1.6333333333333333,
                                              1.6416666666666666,
                                              2.2416666666666667,
                                              2.25,
                                              2.2583333333333333,
                                              2.3916666666666666,
                                              2.4}};
    constexpr std::array<uint32_t, 42> expected{
        {492039381u,  2911933561u, 834770447u,  2911933561u, 2105865586u, 685270513u,  4182090535u,
         685270513u,  1365228386u, 3679899005u, 3351923131u, 2959607493u, 3865698653u, 2959607493u,
         2786834573u, 3679899005u, 4179770024u, 231362729u,  2993125623u, 2911933561u, 2504305329u,
         2911933561u, 2139780826u, 231362729u,  3426441734u, 231362729u,  731674811u,  889756277u,
         1306441753u, 782925929u,  484617264u,  1522533313u, 19361365u,   615997149u,  19361365u,
         3808396013u, 19361365u,   1522533313u, 990585300u,  889756277u,  492039381u,  2911933561u}};
    PixelFx fx(PixelFxKind::Bird), other(PixelFxKind::Bird);
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
    auto settings           = fx.settings();
    settings.seed           = 123;
    settings.critter.breast = {0xff403020u, 0xffc0a080u};
    auto p                  = PixelFx::prepareConfiguration(PixelFxKind::Bird, settings);
    REQUIRE(p);
    settings.critter.breast[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.breast.size() == 4u);
    REQUIRE(fx.settings().critter.breast[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Bird, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings                = fx.settings();
        settings.critter.breast = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Bird, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Bird, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Bird, other.settings());
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
TEST_CASE("stylize.pixelfx.critters.batPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 28> times{
        {0,   0.008333333333333333, 0.09166666666666666, 0.1, 0.10833333333333334, 0.19166666666666668,
         0.2, 0.20833333333333334,  0.2916666666666667,  0.3, 0.30833333333333335, 0.39166666666666666,
         0.4, 0.4083333333333333,   0.7916666666666666,  0.8, 0.8083333333333333,  1.1916666666666667,
         1.2, 1.2083333333333333,   1.5916666666666666,  1.6, 1.6083333333333334,  1.9916666666666667,
         2,   2.0083333333333333,   2.3916666666666666,  2.4}};
    constexpr std::array<uint32_t, 56> expected{
        {824841877u,  1347507607u, 824841877u,  2141531121u, 3563635477u, 355822344u,  1776782593u, 3693473421u,
         2508363345u, 3064607994u, 791374993u,  866584964u,  3180531348u, 4179880536u, 3180531348u, 684761235u,
         3170012628u, 3382960569u, 4060685023u, 557934673u,  609126622u,  1999287111u, 2490203888u, 1367059438u,
         2490203888u, 4260177570u, 3068803093u, 3755400576u, 4261339373u, 309569023u,  2250696773u, 2163521507u,
         3718755253u, 1717895846u, 3915554973u, 1903623615u, 3915554973u, 2187605319u, 86348485u,   229706531u,
         1450664320u, 3197163845u, 4164293253u, 3549249376u, 4164293253u, 3217700091u, 3249784157u, 2177498204u,
         717178853u,  3162447491u, 843317205u,  1286955930u, 1435059757u, 2251947846u, 824841877u,  1347507607u}};
    PixelFx fx(PixelFxKind::Bat), other(PixelFxKind::Bat);
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
    auto settings         = fx.settings();
    settings.seed         = 123;
    settings.critter.eyes = {0xff403020u, 0xffc0a080u};
    auto p                = PixelFx::prepareConfiguration(PixelFxKind::Bat, settings);
    REQUIRE(p);
    settings.critter.eyes[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.eyes.size() == 4u);
    REQUIRE(fx.settings().critter.eyes[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Bat, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings              = fx.settings();
        settings.critter.eyes = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Bat, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Bat, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Bat, other.settings());
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
TEST_CASE("stylize.pixelfx.critters.eyeballPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 29>   times{{0,
                                              0.008333333333333333,
                                              0.5416666666666666,
                                              0.55,
                                              0.5583333333333333,
                                              0.6166666666666667,
                                              0.625,
                                              1.1416666666666666,
                                              1.15,
                                              1.1583333333333334,
                                              1.7333333333333334,
                                              1.7416666666666667,
                                              1.75,
                                              1.825,
                                              1.8333333333333333,
                                              1.9083333333333334,
                                              1.9166666666666667,
                                              1.925,
                                              1.9416666666666667,
                                              1.95,
                                              1.9583333333333333,
                                              2.4916666666666667,
                                              2.5,
                                              2.5083333333333333,
                                              2.9916666666666667,
                                              3,
                                              3.0083333333333333,
                                              3.191666666666667,
                                              3.2}};
    constexpr std::array<uint32_t, 58> expected{
        {876293384u,  202262848u,  2201736678u, 1905387239u, 3312366668u, 183849844u,  3646324628u, 1659308737u,
         800502107u,  3944736665u, 3019307275u, 550879712u,  3697298286u, 2519137683u, 2755919562u, 1795447329u,
         597719923u,  4251478879u, 67591651u,   1363221195u, 20320618u,   3803490040u, 3264869724u, 214557554u,
         876611647u,  1562558107u, 1722511945u, 2100195773u, 859410411u,  1176401249u, 4231164910u, 2855122947u,
         2116478172u, 2640399496u, 234117946u,  591214191u,  2576763248u, 683885351u,  765000835u,  2177953900u,
         3920623188u, 4079872057u, 2946516046u, 1950186329u, 1826355812u, 1209581771u, 1000163182u, 4094447905u,
         1743561117u, 795102715u,  3653411449u, 2880151052u, 1078440869u, 234265085u,  3981415147u, 2102064247u,
         876293384u,  202262848u}};
    PixelFx fx(PixelFxKind::Eyeball), other(PixelFxKind::Eyeball);
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
    auto settings         = fx.settings();
    settings.seed         = 123;
    settings.critter.iris = {0xff403020u, 0xffc0a080u};
    auto p                = PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings);
    REQUIRE(p);
    settings.critter.iris[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.iris.size() == 5u);
    REQUIRE(fx.settings().critter.iris[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Eyeball, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings              = fx.settings();
        settings.critter.iris = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Eyeball, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Eyeball, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(3.2);
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
TEST_CASE("stylize.pixelfx.critters.eyeballFleshHoverBoundsAndOwnership") {
    PixelFx fx(PixelFxKind::Eyeball);
    auto    settings       = fx.settings();
    settings.critter.flesh = {0xff403020u, 0xffc0a080u};
    settings.critter.hover = 38.5;
    auto prepared          = PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings);
    REQUIRE(prepared);
    settings.critter.flesh[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.flesh.size() == 5u);
    REQUIRE(fx.settings().critter.flesh[0] == 0xff403020u);
    const auto                  config = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (double height :
         {-1., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        settings               = fx.settings();
        settings.critter.hover = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings));
    }
    settings               = fx.settings();
    settings.critter.flesh = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    for (double height : {0., 256.}) {
        settings               = fx.settings();
        settings.critter.hover = height;
        auto p                 = PixelFx::prepareConfiguration(PixelFxKind::Eyeball, settings);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        fx.render(.375);
        REQUIRE(fx.settings().critter.hover == height);
    }
}
TEST_CASE("stylize.pixelfx.critters.fishSchoolPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 25>   times{{0,
                                              0.008333333333333333,
                                              0.24166666666666667,
                                              0.25,
                                              0.25833333333333336,
                                              0.5916666666666667,
                                              0.6,
                                              0.6083333333333333,
                                              0.825,
                                              0.8333333333333334,
                                              0.8416666666666667,
                                              2.4916666666666667,
                                              2.5,
                                              2.5083333333333333,
                                              4.025,
                                              4.033333333333333,
                                              4.041666666666667,
                                              4.275,
                                              4.283333333333333,
                                              4.291666666666667,
                                              4.625,
                                              4.633333333333334,
                                              4.641666666666667,
                                              4.791666666666667,
                                              4.8}};
    constexpr std::array<uint32_t, 50> expected{
        {668612624u,  1950504005u, 2847145611u, 3596484225u, 3102325728u, 1297346817u, 3926538698u, 854453880u,
         3021407637u, 2286074679u, 729495716u,  3251154621u, 142471916u,  3558889024u, 1954324338u, 2790199186u,
         4180623976u, 1161963670u, 2737516501u, 3908373734u, 1341706181u, 1936059056u, 3575207400u, 1234993664u,
         917537605u,  3406263155u, 2394934199u, 4055494219u, 1444863252u, 204588182u,  2478855670u, 1381856707u,
         2708478777u, 4247765257u, 248849686u,  2883948416u, 1512285083u, 1407163599u, 1321521928u, 2393698800u,
         545690348u,  2317457477u, 931198877u,  3509287208u, 1220366240u, 2012392064u, 1738220474u, 1175218784u,
         668612624u,  1950504005u}};
    PixelFx fx(PixelFxKind::FishSchool), other(PixelFxKind::FishSchool);
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
    auto settings           = fx.settings();
    settings.seed           = 123;
    settings.critter.stripe = {0xff403020u, 0xffc0a080u};
    auto p                  = PixelFx::prepareConfiguration(PixelFxKind::FishSchool, settings);
    REQUIRE(p);
    settings.critter.stripe[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.stripe.size() == 3u);
    REQUIRE(fx.settings().critter.stripe[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::FishSchool, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings                = fx.settings();
        settings.critter.stripe = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FishSchool, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FishSchool, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::FishSchool, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(4.8);
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
TEST_CASE("stylize.pixelfx.critters.sourceFlameLayersCoolingAndBubbles") {
    struct Fixture {
        int      kind;
        double   time;
        int      layer;
        double   tag, k, r, wobble;
        bool     transform;
        uint32_t color, light;
    };
    const std::array<Fixture, 17> fixtures{{{0, 0.375, -1, 91.5, 0, 0, 0, false, 1507541118u, 205962693u},
                                            {0, 0.375, 0, 91.5, 0, 0, 0, false, 3521469449u, 205962693u},
                                            {0, 0.375, 1, 91.5, 0, 0, 0, true, 3243739600u, 205962693u},
                                            {0, 0.375, 2, 91.5, 0, 0, 0, false, 3617653221u, 205962693u},
                                            {0, 0.375, 3, 91.5, 0, 0, 0, false, 2305330946u, 205962693u},
                                            {0, 1.975, -1, 47244640271, 0, 0, 0, true, 2725398100u, 205962693u},
                                            {1, 0, -1, 0, 1, 0, 0, false, 4164705797u, 3503739319u},
                                            {1, 0, -1, 0, 0.8, 0, 0, false, 1090780845u, 268062745u},
                                            {1, 0, -1, 0, 0.5, 0, 0, true, 4170819316u, 1348584432u},
                                            {1, 0, -1, 0, 0.2, 0, 0, false, 1983411965u, 205962693u},
                                            {1, 0, -1, 0, 0.01, 0, 0, false, 2205189041u, 205962693u},
                                            {2, 0, -1, 0, 0, 0.6, 0, false, 2556089221u, 205962693u},
                                            {2, 0, -1, 0, 0, 0.75, 0, false, 2556089221u, 205962693u},
                                            {2, 0, -1, 0, 0, 1.2, 0, false, 419139898u, 1559423916u},
                                            {2, 0, -1, 0, 0, 1.6, 0, false, 311248565u, 2372533028u},
                                            {2, 0, -1, 0, 0, 2.3, 0, true, 1344292206u, 3023966359u},
                                            {2, 0, -1, 0, 0, 3.1, 0.2, false, 802621493u, 1330979576u}}};
    const std::array<uint32_t, 9> heat{{WHITE, hex(0xfff3a6), hex(0xffe26b), hex(0xffc23d), hex(0xff8a2a),
                                        hex(0xf2561d), hex(0xc9281f), hex(0x8a1f17), hex(0x4a1712)}};
    const std::array<uint32_t, 4> colors{{hex(0xf4fff8), hex(0xbfe8e6), hex(0x62bccc), hex(0x2f8aa8)}};
    const PixelFxInfo             info{PixelFxKind::FishSchool, "source-kit", 96, 112, 48, 96, 2.4, true};
    for (auto f : fixtures) {
        std::array<uint32_t, 96 * 112> color{}, light{};
        Paint                          p{info, color, light, 1};
        p.circle(0, -20, 18, hex(0x181a28));
        if (f.transform) p.place(7, -3, true, .2);
        p.fade = .73;
        if (f.kind == 0) critter::flameTongue(p, -2, -12, 21.7, 4.2, -3.8, f.time, 2.4, f.tag, heat, f.layer);
        if (f.kind == 1) critter::coolingFirePuff(p, 3.2, -23.7, f.k, 4.2, 19.37, heat);
        if (f.kind == 2) critter::bubbleLook(p, -3.4, -14.7, f.r, colors, .65, f.wobble);
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.critters.sourceAnalyticFloorBounces") {
    struct Sample {
        double age, x, y, vx, vy;
        int    hops;
        bool   rest;
    };
    const std::array<Sample, 9> samples{
        {{0, -3, -32, 27, -80, 0, false},
         {0.1, -0.2999999999999998, -37.85, 27, -37, 0, false},
         {0.3, 5.1, -36.650000000000006, 27, 49, 0, false},
         {0.5, 10.5, -18.25, 27, 135, 0, false},
         {0.7, 15.184237513912397, -5.623063779751715, 18.9, -38.863879752458125, 1, false},
         {1, 20.679930531876355, -1.2999348434595823, 13.229999999999999, -19.086122315042385, 2, false},
         {1.3, 22.26519605801611, -0.75, 0, 0, 2, true},
         {1.8, 22.26519605801611, -0.75, 0, 0, 2, true},
         {3, 22.26519605801611, -0.75, 0, 0, 2, true}}};
    for (auto f : samples) {
        const auto b = critter::groundBounce(-3, -32, 27, -80, f.age, 430, -.5, .42);
        REQUIRE(std::abs(b.x - f.x) < 1e-12);
        REQUIRE(std::abs(b.y - f.y) < 1e-12);
        REQUIRE(std::abs(b.vx - f.vx) < 1e-12);
        REQUIRE(std::abs(b.vy - f.vy) < 1e-12);
        REQUIRE(b.hops == f.hops);
        REQUIRE(b.rest == f.rest);
    }
}
TEST_CASE("stylize.pixelfx.critters.flyingSkullPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 25>   times{{0,
                                              0.008333333333333333,
                                              0.14166666666666666,
                                              0.15,
                                              0.15833333333333333,
                                              0.7416666666666667,
                                              0.75,
                                              0.7583333333333333,
                                              1.2916666666666667,
                                              1.3,
                                              1.3083333333333333,
                                              1.9416666666666667,
                                              1.95,
                                              1.9583333333333333,
                                              1.975,
                                              1.9833333333333334,
                                              1.9916666666666667,
                                              2.091666666666667,
                                              2.1,
                                              2.1083333333333334,
                                              2.2916666666666665,
                                              2.3,
                                              2.308333333333333,
                                              2.3916666666666666,
                                              2.4}};
    constexpr std::array<uint32_t, 50> expected{
        {1913177329u, 1354295126u, 1152630625u, 2581320764u, 149544206u,  918595172u,  234319826u,  525094912u,
         1086861776u, 4218502202u, 1380617608u, 2877766463u, 296522964u,  787880469u,  3824505716u, 782003250u,
         3846236279u, 57502100u,   4288212187u, 113721001u,  266569171u,  815056822u,  403668698u,  3925485146u,
         4057022710u, 734141015u,  3117626817u, 556234982u,  3460617620u, 467278972u,  3302567828u, 1117717498u,
         1732540944u, 513384369u,  1438584495u, 151703281u,  3364520759u, 1246911883u, 4184876450u, 1067727645u,
         935378074u,  1325257737u, 395956235u,  544573334u,  1948255557u, 1378686925u, 826956987u,  2450010438u,
         1913177329u, 1354295126u}};
    PixelFx fx(PixelFxKind::FlyingSkull), other(PixelFxKind::FlyingSkull);
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
    auto settings         = fx.settings();
    settings.seed         = 123;
    settings.critter.bone = {0xff403020u, 0xffc0a080u};
    auto p                = PixelFx::prepareConfiguration(PixelFxKind::FlyingSkull, settings);
    REQUIRE(p);
    settings.critter.bone[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.bone.size() == 7u);
    REQUIRE(fx.settings().critter.bone[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::FlyingSkull, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings              = fx.settings();
        settings.critter.bone = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FlyingSkull, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FlyingSkull, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::FlyingSkull, other.settings());
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
TEST_CASE("stylize.pixelfx.critters.flyingSkullFloorAndHoverTransactions") {
    PixelFx fx(PixelFxKind::FlyingSkull);
    REQUIRE(fx.settings().critter.floor);
    REQUIRE(fx.settings().critter.hover == 40.);
    fx.render(.375);
    const auto original = fx.configurationValue();
    auto       invalid  = original;
    invalid.find("options")->set("floor", eve::Value(int64_t(0)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FlyingSkull, invalid));
    for (double height :
         {-1., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto settings          = fx.settings();
        settings.critter.hover = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::FlyingSkull, settings));
    }
    REQUIRE(fx.configurationValue() == original);
    for (bool floor : {true, false})
        for (double height : {0., 256.}) {
            auto settings          = fx.settings();
            settings.critter.floor = floor;
            settings.critter.hover = height;
            auto p                 = PixelFx::prepareConfiguration(PixelFxKind::FlyingSkull, settings);
            REQUIRE(p);
            REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
            fx.render(.375);
            REQUIRE(fx.settings().critter.floor == floor);
            REQUIRE(fx.settings().critter.hover == height);
        }
    auto metadata = PixelFx::describeParameters(PixelFxKind::FlyingSkull);
    REQUIRE(metadata);
    bool found = false;
    for (const auto& p : *metadata.value().find("parameters")->getIf<eve::Value::Array>()) {
        if (p.find("name")->asString() != "floor") continue;
        REQUIRE(p.find("type")->asString() == "boolean");
        found = true;
    }
    REQUIRE(found);
}
TEST_CASE("stylize.pixelfx.critters.frogPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 28>   times{{0,
                                              0.008333333333333333,
                                              0.05,
                                              0.45,
                                              0.5,
                                              0.85,
                                              0.875,
                                              0.8833333333333333,
                                              0.9416666666666667,
                                              0.95,
                                              0.9583333333333334,
                                              1.1916666666666667,
                                              1.2,
                                              1.2083333333333333,
                                              1.3916666666666666,
                                              1.4,
                                              1.4083333333333334,
                                              1.6166666666666667,
                                              1.625,
                                              1.6333333333333333,
                                              1.7416666666666667,
                                              1.75,
                                              1.7583333333333333,
                                              1.9416666666666667,
                                              1.95,
                                              1.9583333333333333,
                                              2.591666666666667,
                                              2.6}};
    constexpr std::array<uint32_t, 56> expected{
        {1986492480u, 4207117677u, 1567569745u, 852685778u,  1496778568u, 1941564359u, 1257851739u, 3076437021u,
         989274427u,  1283507726u, 123602031u,  270354293u,  3145778507u, 852685778u,  2798568149u, 2620376929u,
         3656088581u, 2200927408u, 3606716005u, 3643084611u, 176514156u,  1307720488u, 3867350215u, 2609262945u,
         1619755063u, 4020656922u, 3584434634u, 1111923420u, 2092168963u, 3106173556u, 2184115461u, 128273622u,
         657907951u,  4191020485u, 341223406u,  4191020485u, 3590719272u, 4191020485u, 3651986995u, 4191020485u,
         3344390437u, 4191020485u, 3050496892u, 4191020485u, 2961605801u, 4191020485u, 1772563338u, 4191020485u,
         2181118664u, 4191020485u, 3200133832u, 605920166u,  1929495604u, 2731365890u, 1986492480u, 4207117677u}};
    PixelFx fx(PixelFxKind::Frog), other(PixelFxKind::Frog);
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
    auto settings          = fx.settings();
    settings.seed          = 123;
    settings.critter.belly = {0xff403020u, 0xffc0a080u};
    auto p                 = PixelFx::prepareConfiguration(PixelFxKind::Frog, settings);
    REQUIRE(p);
    settings.critter.belly[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.belly.size() == 3u);
    REQUIRE(fx.settings().critter.belly[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Frog, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings               = fx.settings();
        settings.critter.belly = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Frog, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Frog, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Frog, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(2.6);
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
TEST_CASE("stylize.pixelfx.critters.ghostPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 15> times{
        {0, 0.008333333333333333, 0.2, 0.4, 0.6, 0.8, 1, 1.2, 1.4, 1.6, 1.8, 2, 2.2, 2.3916666666666666, 2.4}};
    constexpr std::array<uint32_t, 30> expected{
        {850320434u,  2920011370u, 536648507u,  44277844u,   2833926395u, 1790112324u, 863787574u,  1969951159u,
         543509722u,  1663163704u, 3701572864u, 2820724203u, 750948355u,  1251570046u, 1583435552u, 3165054418u,
         4266251517u, 3791639147u, 1724651536u, 3448404659u, 2923712229u, 3991779028u, 2734314255u, 3552197511u,
         1554924747u, 3882928402u, 2011935795u, 2921098003u, 850320434u,  2920011370u}};
    PixelFx fx(PixelFxKind::Ghost), other(PixelFxKind::Ghost);
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
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Ghost, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Ghost, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Ghost, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Ghost, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Ghost, other.settings());
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
TEST_CASE("stylize.pixelfx.critters.sourceTubeDiscOrderAndTransparency") {
    struct Fixture {
        int                     index;
        double                  alpha, radius;
        bool                    transform;
        std::array<uint32_t, 5> sums;
    };
    const std::array<Fixture, 6> fixtures{
        {{0, 1, -1, false, {{186940183u, 3307793864u, 763125506u, 783213779u, 2315578821u}}},
         {1, 0.65, -1, false, {{1832589484u, 3655353171u, 3630104364u, 2845041063u, 2315578821u}}},
         {2, 0.42, -1, true, {{3526844198u, 3458587477u, 3906936215u, 2598325240u, 2315578821u}}},
         {3, 1, 0.7, false, {{574572492u, 3958427544u, 1898773106u, 2701448267u, 2315578821u}}},
         {4, 0.999, 0.75, false, {{1936864282u, 369878350u, 4061357865u, 1819017776u, 2315578821u}}},
         {5, 0.8, 2.4, true, {{555594751u, 440532056u, 3050545186u, 1304508050u, 2315578821u}}}}};
    const std::array<Point, 4> extended{{{-6, -5}, {-10, 0}, {-18, 10}, {-23, 12}}},
        folded{{{-4, -1.5}, {-9, -1}, {-3, -.5}, {4, -.5}}};
    const std::array<Point, 3> zero{{{0, 0}, {0, 0}, {3.2, -4.7}}};
    const std::array<Point, 2> line{{{-5, 2}, {4, -3}}};
    const PixelFxInfo          info{PixelFxKind::Frog, "tube-fixture", 80, 80, 40, 60, 1, true};
    for (auto f : fixtures) {
        critter::Grid<52, 44>        g(-26, -20);
        const std::span<const Point> points = f.index == 0   ? std::span<const Point>(extended)
                                              : f.index == 1 ? std::span<const Point>(folded)
                                              : f.index == 2 ? std::span<const Point>(zero)
                                                             : std::span<const Point>(line);
        const auto                   color  = [](double u, double v) {
            return v > .55 ? hex(0x256024) : u > .66 ? hex(0x3a8a2e) : hex(0x62b440);
        };
        if (f.radius < 0)
            g.tube(points, [](double u) { return 2 + (.7 - 2) * u; }, color, f.alpha);
        else
            g.tube(points, f.radius, color, f.alpha);
        g.outline(hex(0x0a160c));
        std::array<uint32_t, 6400> colorPixels{}, light{};
        Paint                      p{info, colorPixels, light, 1};
        p.circle(0, 0, 12, hex(0x20202a));
        if (f.transform) p.place(7, -3, true, .2);
        p.fade = .73;
        g.draw(p, .85, -.5, .5);
        REQUIRE(checksum(g.colors) == f.sums[0]);
        REQUIRE(checksum(g.alpha) == f.sums[1]);
        REQUIRE(checksum(g.tags) == f.sums[2]);
        REQUIRE(checksum(colorPixels) == f.sums[3]);
        REQUIRE(checksum(light) == f.sums[4]);
    }
}
TEST_CASE("stylize.pixelfx.critters.sourceTossAndSpringSamples") {
    struct Sample {
        double age, x, y, vx, vy;
        bool   landed;
        double landTime, landX;
    };
    const std::array<Sample, 8> flights{
        {{0, -2, -1, 33, -55, false, 0.16576111154095502, 3.4701166808515156},
         {0.01, -1.67, -1.5150000000000001, 33, -48, false, 0.16576111154095502, 3.4701166808515156},
         {0.1, 1.3000000000000003, -3, 33, 15, false, 0.16576111154095502, 3.4701166808515156},
         {0.2, 3.4701166808515156, -0.5, 0, 0, true, 0.16576111154095502, 3.4701166808515156},
         {0.3, 3.4701166808515156, -0.5, 0, 0, true, 0.16576111154095502, 3.4701166808515156},
         {0.4, 3.4701166808515156, -0.5, 0, 0, true, 0.16576111154095502, 3.4701166808515156},
         {0.5, 3.4701166808515156, -0.5, 0, 0, true, 0.16576111154095502, 3.4701166808515156},
         {1, 3.4701166808515156, -0.5, 0, 0, true, 0.16576111154095502, 3.4701166808515156}}};
    for (auto f : flights) {
        const auto q = critter::tossSample(-2, -1, 33, -55, f.age, 700, -.5);
        REQUIRE(std::abs(q.x - f.x) < 1e-12);
        REQUIRE(std::abs(q.y - f.y) < 1e-12);
        REQUIRE(std::abs(q.vx - f.vx) < 1e-12);
        REQUIRE(std::abs(q.vy - f.vy) < 1e-12);
        REQUIRE(q.landed == f.landed);
        REQUIRE(std::abs(q.landTime - f.landTime) < 1e-12);
        REQUIRE(std::abs(q.landX - f.landX) < 1e-12);
    }
    const std::array<std::array<double, 2>, 7> springs{{{{-0.1, 0}},
                                                        {{0, 1}},
                                                        {{0.01, 0.9264333826052012}},
                                                        {{0.1, 0.032779525540908225}},
                                                        {{0.2, -0.2703827984446385}},
                                                        {{0.7, -0.0044992967963801275}},
                                                        {{2.3, -3.1905046408270263e-07}}}};
    for (auto q : springs) REQUIRE(std::abs(critter::dampedSpring(q[0], 2.4, 6.5) - q[1]) < 1e-12);
}
TEST_CASE("stylize.pixelfx.critters.ghostHoverAtomicBounds") {
    PixelFx fx(PixelFxKind::Ghost);
    REQUIRE(fx.settings().critter.hover == 14.);
    fx.render(.375);
    const auto config = fx.configurationValue();
    for (double height :
         {-1., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto s          = fx.settings();
        s.critter.hover = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Ghost, s));
    }
    REQUIRE(fx.configurationValue() == config);
    for (double height : {0., 256.}) {
        auto s          = fx.settings();
        s.critter.hover = height;
        auto p          = PixelFx::prepareConfiguration(PixelFxKind::Ghost, s);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        fx.render(.375);
        REQUIRE(fx.settings().critter.hover == height);
    }
}

TEST_CASE("stylize.pixelfx.critters.wispPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 13> times{{0, 0.008333333333333333, 0.15, 0.8, 1.6, 2.191666666666667, 2.2,
                                            2.2083333333333335, 2.275, 2.283333333333333, 2.4, 3.191666666666667, 3.2}};
    constexpr std::array<uint32_t, 26> expected{
        {3384786989u, 3256608864u, 4212510849u, 3849049853u, 2334990939u, 2626285461u, 1126021109u,
         2703135905u, 2384177753u, 2322781117u, 271323811u,  1722097829u, 2530224821u, 379721264u,
         333916887u,  2145741790u, 3265152093u, 2713730514u, 3299395319u, 205928099u,  410353057u,
         818084028u,  3509026249u, 2585992441u, 3384786989u, 3256608864u}};
    PixelFx fx(PixelFxKind::Wisp), other(PixelFxKind::Wisp);
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
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Wisp, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Wisp, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Wisp, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Wisp, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Wisp, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(3.2);
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

TEST_CASE("stylize.pixelfx.critters.wispHoverAtomicBounds") {
    PixelFx fx(PixelFxKind::Wisp);
    REQUIRE(fx.settings().critter.hover == 36.);
    fx.render(.375);
    const auto config = fx.configurationValue();
    for (double height :
         {-1., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto s          = fx.settings();
        s.critter.hover = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Wisp, s));
    }
    REQUIRE(fx.configurationValue() == config);
    for (double height : {0., 256.}) {
        auto s          = fx.settings();
        s.critter.hover = height;
        auto p          = PixelFx::prepareConfiguration(PixelFxKind::Wisp, s);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        fx.render(.375);
        REQUIRE(fx.settings().critter.hover == height);
    }
}


TEST_CASE("stylize.pixelfx.critters.snakePhasesMaterialsAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.2916666666666667,
                                              0.3,
                                              0.30833333333333335,
                                              0.4666666666666667,
                                              0.6333333333333333,
                                              0.7916666666666666,
                                              0.8,
                                              0.8083333333333333,
                                              1.2,
                                              1.4916666666666667,
                                              1.5,
                                              1.5083333333333333,
                                              1.6666666666666667,
                                              1.8333333333333333,
                                              1.9916666666666667,
                                              2,
                                              2.0083333333333333,
                                              2.3916666666666666,
                                              2.4}};
    constexpr std::array<uint32_t, 42> expected{
        {2186936680u, 910047637u,  2400309990u, 2970598449u, 504291820u,  67634938u,   1463728289u,
         67634938u,   3307195356u, 67634938u,   3552304169u, 3558434900u, 2304011386u, 4133814807u,
         1805170516u, 4008088823u, 828878251u,  1751279161u, 4187420309u, 3484982109u, 1298388014u,
         910047637u,  2953471364u, 67634938u,   585525570u,  67634938u,   2026579816u, 67634938u,
         3108203823u, 3558434900u, 314400269u,  4133814807u, 3525494596u, 4008088823u, 1462961103u,
         1751279161u, 3470387172u, 3484982109u, 3366312871u, 2614950081u, 2186936680u, 910047637u}};
    PixelFx fx(PixelFxKind::Snake), other(PixelFxKind::Snake);
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
    auto settings          = fx.settings();
    settings.seed          = 123;
    settings.critter.belly = {0xff403020u, 0xffc0a080u};
    auto p                 = PixelFx::prepareConfiguration(PixelFxKind::Snake, settings);
    REQUIRE(p);
    settings.critter.belly[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.belly.size() == 3u);
    REQUIRE(fx.settings().critter.belly[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Snake, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings               = fx.settings();
        settings.critter.belly = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Snake, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Snake, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Snake, other.settings());
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


TEST_CASE("stylize.pixelfx.critters.sourceDepthSnapshots") {
    constexpr std::array<std::array<uint32_t, 4>, 4> expected{{{{1504780614u, 230699065u, 1683041235u, 782582607u}},
                                                               {{3226755450u, 4080628729u, 3578482227u, 1150607896u}},
                                                               {{1440034071u, 564225583u, 2251483775u, 2251483775u}},
                                                               {{491175871u, 491175871u, 491175871u, 491175871u}}}};
    for (int shape = 0; shape < 4; ++shape) {
        critter::Grid<17, 15> g(-8, -7);
        REQUIRE(g.cellDepth(0, 0) == 0);
        for (int y = -7; y < 8; ++y)
            for (int x = -8; x < 9; ++x) {
                const bool filled =
                    shape == 0   ? true
                    : shape == 1 ? x * x / 49. + y * y / 36. <= 1
                    : shape == 2 ? (std::abs(x) <= 6 && std::abs(y) <= 5 && !(std::abs(x) < 2 && std::abs(y) < 2))
                                 : (x + y) % 3 != 0;
                if (filled) g.set(x, y, hex(0x7a3cb8));
            }
        int variant = 0;
        for (uint8_t maximum : {uint8_t(1), uint8_t(2), uint8_t(4), uint8_t(8)}) {
            g.computeDepths(maximum);
            uint32_t checksum = 2166136261u;
            for (auto d : g.depthValues) checksum = (checksum ^ d) * 16777619u;
            REQUIRE(checksum == expected[size_t(shape)][size_t(variant++)]);
            REQUIRE(g.cellDepth(-9, 0) == 0);
            REQUIRE(g.cellDepth(9, 0) == 0);
            REQUIRE(g.cellDepth(0, -8) == 0);
            REQUIRE(g.cellDepth(0, 8) == 0);
            for (int y = -7; y < 8; ++y)
                for (int x = -8; x < 9; ++x) {
                    REQUIRE(g.cellDepth(x + .9, y + .9) == g.depthValues[size_t(g.index(x, y))]);
                    REQUIRE(g.cellDepth(x, y) <= maximum);
                    REQUIRE((g.get(x, y) == 0) == (g.cellDepth(x, y) == 0));
                }
        }
        const auto snapshot = g.depthValues;
        g.clear();
        REQUIRE(g.depthValues == snapshot);
        g.computeDepths();
        for (auto d : g.depthValues) REQUIRE(d == 0);
    }
}


TEST_CASE("stylize.pixelfx.critters.jellyfishPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 25>   times{{0,
                                              0.008333333333333333,
                                              0.24166666666666667,
                                              0.25,
                                              0.25833333333333336,
                                              0.3416666666666667,
                                              0.35,
                                              0.35833333333333334,
                                              0.6583333333333333,
                                              0.6666666666666666,
                                              0.675,
                                              1.1416666666666666,
                                              1.15,
                                              1.1583333333333334,
                                              1.5916666666666666,
                                              1.6,
                                              1.6083333333333334,
                                              1.8416666666666666,
                                              1.85,
                                              1.8583333333333334,
                                              1.9416666666666667,
                                              1.95,
                                              1.9583333333333333,
                                              3.191666666666667,
                                              3.2}};
    constexpr std::array<uint32_t, 50> expected{
        {1083314401u, 3298014176u, 659454976u,  4092727612u, 151853085u,  892715892u,  3744003188u, 2197579748u,
         2643976864u, 36805632u,   3875333133u, 815544842u,  1138973763u, 2850870339u, 1883006591u, 1808504491u,
         4093724039u, 3795137327u, 3852337227u, 1897382417u, 2713845145u, 1890436664u, 181002413u,  3941964299u,
         1383591931u, 2412598700u, 2675600558u, 3381375707u, 4004943927u, 1510263260u, 1744732639u, 2824497060u,
         4102809089u, 1010440529u, 418748970u,  1156441346u, 2486939234u, 1904821310u, 3394551572u, 1017992310u,
         4091311049u, 552814250u,  1299640470u, 2587932695u, 1738102954u, 1377018377u, 638915368u,  1239006390u,
         1083314401u, 3298014176u}};
    PixelFx fx(PixelFxKind::Jellyfish), other(PixelFxKind::Jellyfish);
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
    auto settings         = fx.settings();
    settings.seed         = 123;
    settings.critter.glow = {0xff403020u, 0xffc0a080u};
    auto p                = PixelFx::prepareConfiguration(PixelFxKind::Jellyfish, settings);
    REQUIRE(p);
    settings.critter.glow[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().critter.glow.size() == 4u);
    REQUIRE(fx.settings().critter.glow[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Jellyfish, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings              = fx.settings();
        settings.critter.glow = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Jellyfish, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Jellyfish, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Jellyfish, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(3.2);
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


TEST_CASE("stylize.pixelfx.critters.slimePhasesMaterialsAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.49166666666666664,
                                              0.5,
                                              0.5083333333333333,
                                              0.8333333333333334,
                                              0.8416666666666667,
                                              0.85,
                                              0.8916666666666667,
                                              0.9,
                                              0.9083333333333333,
                                              0.9583333333333334,
                                              0.9666666666666667,
                                              0.975,
                                              1.1666666666666667,
                                              1.4333333333333333,
                                              1.4416666666666667,
                                              1.45,
                                              1.5,
                                              1.7916666666666667,
                                              1.8}};
    constexpr std::array<uint32_t, 42> expected{
        {167977917u,  2433793573u, 4172284526u, 4009715091u, 1638178242u, 2592968157u, 1275025028u,
         2150755971u, 1872337468u, 2697218093u, 33165714u,   1122068785u, 496417995u,  1512666744u,
         3672872920u, 2081513240u, 3742420469u, 621055931u,  3237043689u, 1470496208u, 4005277602u,
         2567443395u, 3390396672u, 2809393390u, 3934027323u, 2757434978u, 1747587807u, 1352969128u,
         3220926576u, 3373543131u, 372873456u,  1898611060u, 385865776u,  895428738u,  4011686097u,
         613836320u,  740051635u,  2941389245u, 1195403810u, 2786912617u, 167977917u,  2433793573u}};
    PixelFx fx(PixelFxKind::Slime), other(PixelFxKind::Slime);
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
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Slime, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    fx.render(.375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Slime, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    fx.render(.375);
    REQUIRE(fx.configurationValue() == config);
    for (auto ramp : std::vector<std::vector<uint32_t>>{{}, {0x00ffffffu}, std::vector<uint32_t>(65, 0xffffffffu)}) {
        settings         = fx.settings();
        settings.palette = std::move(ramp);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Slime, settings));
    }
    auto invalid = config;
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Slime, invalid));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Slime, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    fx.render(0);
    const std::vector<uint32_t> seam(fx.color().begin(), fx.color().end());
    fx.render(1.8);
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

TEST_CASE("stylize.pixelfx.critters.slimeHopAtomicBounds") {
    PixelFx fx(PixelFxKind::Slime);
    REQUIRE(fx.settings().critter.hop == 24.);
    fx.render(.375);
    const auto config = fx.configurationValue();
    for (double height :
         {0., -.1, .009, 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto s        = fx.settings();
        s.critter.hop = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Slime, s));
    }
    REQUIRE(fx.configurationValue() == config);
    for (double height : {.01, 256.}) {
        auto s        = fx.settings();
        s.critter.hop = height;
        auto p        = PixelFx::prepareConfiguration(PixelFxKind::Slime, s);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        fx.render(.375);
        REQUIRE(fx.settings().critter.hop == height);
    }
}

TEST_CASE("stylize.pixelfx.critters.sourcePiecewiseCurve") {
    constexpr std::array<Point, 5>  nodes{{{0, .36}, {.4, .02}, {.55, -.05}, {.85, .08}, {1, .16}}};
    constexpr std::array<Point, 18> samples{{{-0.1, 0.36},
                                             {0, 0.36},
                                             {0.01, 0.35937312499999996},
                                             {0.2, 0.19},
                                             {0.399999999, 0.020000000000000018},
                                             {0.4, 0.020000000000000018},
                                             {0.400000001, 0.01999999999999999},
                                             {0.5, -0.03185185185185184},
                                             {0.549999999, -0.04999999999999999},
                                             {0.55, -0.05},
                                             {0.550000001, -0.049999999999999996},
                                             {0.7, 0.014999999999999958},
                                             {0.849999999, 0.08000000000000003},
                                             {0.85, 0.08},
                                             {0.850000001, 0.08000000000000002},
                                             {0.95, 0.13925925925925925},
                                             {1, 0.16},
                                             {1.1, 0.16}}};
    for (auto sample : samples) REQUIRE(std::abs(critter::sampleCurve(sample.x, nodes) - sample.y) < 1e-15);
    constexpr std::array<Point, 1> single{{{2, 7}}};
    REQUIRE(critter::sampleCurve(-100, single) == 7);
    REQUIRE(critter::sampleCurve(100, single) == 7);
}

}  // namespace test_pixelfx_critters_unit
