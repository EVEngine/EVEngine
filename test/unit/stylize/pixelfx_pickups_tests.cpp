// Pickup-family suites are separate from the core suite, which exceeds 5000 lines.
#include <algorithm>
#include <array>
#include <cmath>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Pickup.h"
#include "stylize/pixelfx/PickupGem.h"
#include "stylize/pixelfx/PickupLoot.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_pickups_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.pickups.coinSourceReliefAndTransactions") {
    constexpr std::array<int, 12>      frames{{0, 1, 12, 24, 48, 49, 72, 96, 120, 144, 191, 192}};
    constexpr std::array<uint32_t, 24> expected{
        {3787901646u, 215442096u,  1121157042u, 2272640582u, 127941432u,  634236239u,  893117233u,  1998864018u,
         1490162569u, 642649820u,  2476349829u, 3692216944u, 241640189u,  1579737611u, 2918563133u, 1194587200u,
         2005655895u, 2019373657u, 429228287u,  1678563362u, 2039611316u, 595875796u,  3787901646u, 215442096u}};
    PixelFx fx(PixelFxKind::CoinSpin), other(PixelFxKind::CoinSpin);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < frames.size(); ++i) {
        fx.render(frames[i] / 120.);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::CoinSpin, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("radius", eve::Value(8.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::CoinSpin, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinSpin, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::CoinSpin, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(1.6);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.scratchLayerSourceOutlineAndOwnership") {
    pixelfx::pickup::Layer<5, 4> layer, other;
    layer.begin(-2.2, -3.1);
    layer.set(-2, -3, 0xff123456u);
    layer.set(0, -2, 0xffabcdefu);
    REQUIRE(layer.get(-1.9, -2.1) == 0xff123456u);
    REQUIRE(layer.get(-4, -3) == 0u);
    layer.set(-4, -3, 0xffffffffu);
    layer.outline(0xff010203u);
    constexpr std::array<uint32_t, 20> expected{{0u,          4278256131u, 0u,          0u,          0u,
                                                 4278256131u, 4279383126u, 4278256131u, 4278256131u, 0u,
                                                 0u,          4278256131u, 4278256131u, 4289449455u, 4278256131u,
                                                 0u,          0u,          0u,          4278256131u, 0u}};
    REQUIRE(layer.colors == expected);
    const auto copy = layer;
    other.begin(20, 30);
    other.set(21, 31, 0xff222222u);
    REQUIRE(layer.colors == expected);
    layer.begin(0, 0);
    for (auto c : layer.colors) REQUIRE(c == 0u);
    REQUIRE(copy.colors == expected);
    REQUIRE(other.get(21, 31) == 0xff222222u);
}
TEST_CASE("stylize.pixelfx.pickups.gemSourceFacetsAndTransactions") {
    constexpr std::array<int, 12>      frames{{0, 1, 12, 36, 71, 72, 73, 144, 215, 216, 287, 288}};
    constexpr std::array<uint32_t, 24> expected{
        {14428915u,   3434119262u, 828735019u,  2965235594u, 3346351803u, 2379595475u, 2593133461u, 2105654751u,
         3408420188u, 4167421606u, 2762575496u, 1746550242u, 1759636111u, 4253596023u, 551019851u,  2131187788u,
         3577071377u, 4087530513u, 2344269781u, 2901826593u, 14051969u,   540735915u,  14428915u,   3434119262u}};
    PixelFx fx(PixelFxKind::GemSpin), other(PixelFxKind::GemSpin);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < frames.size(); ++i) {
        fx.render(frames[i] / 120.);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::GemSpin, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("radius", eve::Value(8.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::GemSpin, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::GemSpin, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::GemSpin, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(2.4);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.brilliantSourcePlanesAndMaterialControls") {
    using namespace pixelfx;
    using namespace pixelfx::pickup;
    const auto                         mesh = brilliant(7.5, 3.2, .5, 9.5, .9);
    constexpr std::array<GemPlane, 49> expected{
        {{{{0, 1, 0}}, 3.6500000000000004, 0},
         {{{0.6491192706966527, 0.7606866453476397, 0}}, 5.210703520631332, 1},
         {{{1, 0, 0}}, 7.5, 3},
         {{{0.7848827655334262, -0.6196442885790208, 0}}, 6.165460671361256, 4},
         {{{0.738621546113418, 0.6006951031559573, 0.30594706186116216}}, 6.3383523380715205, 2},
         {{{0.9238795325112867, 0, 0.3826834323650898}}, 7.5, 3},
         {{{0.6643621548010997, -0.6949061762772779, 0.27518781484602917}}, 5.867758481539348, 5},
         {{{0.4589966381084693, 0.7606866453476397, 0.45899663810846925}}, 5.210703520631332, 1},
         {{{0.7071067811865476, 0, 0.7071067811865475}}, 7.5, 3},
         {{{0.5549959259451367, -0.6196442885790208, 0.5549959259451366}}, 6.165460671361256, 4},
         {{{0.3059470618611622, 0.6006951031559573, 0.738621546113418}}, 6.3383523380715205, 2},
         {{{0.38268343236508984, 0, 0.9238795325112867}}, 7.5, 3},
         {{{0.27518781484602917, -0.6949061762772779, 0.6643621548010997}}, 5.867758481539348, 5},
         {{{3.9747091856175996e-17, 0.7606866453476397, 0.6491192706966527}}, 5.210703520631332, 1},
         {{{6.123233995736766e-17, 0, 1}}, 7.5, 3},
         {{{4.8060208325821644e-17, -0.6196442885790208, 0.7848827655334262}}, 6.165460671361256, 4},
         {{{-0.3059470618611621, 0.6006951031559573, 0.738621546113418}}, 6.3383523380715205, 2},
         {{{-0.3826834323650897, 0, 0.9238795325112867}}, 7.5, 3},
         {{{-0.2751878148460291, -0.6949061762772779, 0.6643621548010997}}, 5.867758481539348, 5},
         {{{-0.45899663810846925, 0.7606866453476397, 0.4589966381084693}}, 5.210703520631332, 1},
         {{{-0.7071067811865475, 0, 0.7071067811865476}}, 7.5, 3},
         {{{-0.5549959259451366, -0.6196442885790208, 0.5549959259451367}}, 6.165460671361256, 4},
         {{{-0.738621546113418, 0.6006951031559573, 0.3059470618611623}}, 6.3383523380715205, 2},
         {{{-0.9238795325112867, 0, 0.3826834323650899}}, 7.5, 3},
         {{{-0.6643621548010997, -0.6949061762772779, 0.2751878148460292}}, 5.867758481539348, 5},
         {{{-0.6491192706966527, 0.7606866453476397, 7.949418371235199e-17}}, 5.210703520631332, 1},
         {{{-1, 0, 1.2246467991473532e-16}}, 7.5, 3},
         {{{-0.7848827655334262, -0.6196442885790208, 9.612041665164329e-17}}, 6.165460671361256, 4},
         {{{-0.738621546113418, 0.6006951031559573, -0.3059470618611621}}, 6.3383523380715205, 2},
         {{{-0.9238795325112868, 0, -0.38268343236508967}}, 7.5, 3},
         {{{-0.6643621548010997, -0.6949061762772779, -0.27518781484602906}}, 5.867758481539348, 5},
         {{{-0.45899663810846936, 0.7606866453476397, -0.45899663810846925}}, 5.210703520631332, 1},
         {{{-0.7071067811865477, 0, -0.7071067811865475}}, 7.5, 3},
         {{{-0.5549959259451368, -0.6196442885790208, -0.5549959259451366}}, 6.165460671361256, 4},
         {{{-0.3059470618611626, 0.6006951031559573, -0.7386215461134178}}, 6.3383523380715205, 2},
         {{{-0.38268343236509034, 0, -0.9238795325112865}}, 7.5, 3},
         {{{-0.27518781484602955, -0.6949061762772779, -0.6643621548010995}}, 5.867758481539348, 5},
         {{{-1.19241275568528e-16, 0.7606866453476397, -0.6491192706966527}}, 5.210703520631332, 1},
         {{{-1.8369701987210297e-16, 0, -1}}, 7.5, 3},
         {{{-1.4418062497746492e-16, -0.6196442885790208, -0.7848827655334262}}, 6.165460671361256, 4},
         {{{0.30594706186116233, 0.6006951031559573, -0.7386215461134179}}, 6.3383523380715205, 2},
         {{{0.38268343236509, 0, -0.9238795325112866}}, 7.5, 3},
         {{{0.27518781484602933, -0.6949061762772779, -0.6643621548010996}}, 5.867758481539348, 5},
         {{{0.4589966381084692, 0.7606866453476397, -0.45899663810846936}}, 5.210703520631332, 1},
         {{{0.7071067811865474, 0, -0.7071067811865477}}, 7.5, 3},
         {{{0.5549959259451366, -0.6196442885790208, -0.5549959259451368}}, 6.165460671361256, 4},
         {{{0.7386215461134178, 0.6006951031559573, -0.30594706186116266}}, 6.3383523380715205, 2},
         {{{0.9238795325112865, 0, -0.3826834323650904}}, 7.5, 3},
         {{{0.6643621548010995, -0.6949061762772779, -0.2751878148460296}}, 5.867758481539348, 5}}};
    REQUIRE(mesh.planes.size() == expected.size());
    REQUIRE(mesh.r == 7.5);
    REQUIRE(std::abs(mesh.top - 3.65) < 1e-12);
    REQUIRE(std::abs(mesh.bottom - 9.95) < 1e-12);
    for (size_t i = 0; i < expected.size(); ++i) {
        for (size_t j = 0; j < 3; ++j) REQUIRE(std::abs(mesh.planes[i].n[j] - expected[i].n[j]) < 1e-12);
        REQUIRE(std::abs(mesh.planes[i].d - expected[i].d) < 1e-12);
        REQUIRE(mesh.planes[i].kind == expected[i].kind);
    }
    struct Fixture {
        double       spin;
        GemOptions   options;
        GemHighlight hi;
        uint32_t     color, light, layer;
    };
    const std::array<Fixture, 9> fixtures{
        {{0, {0.22, false, 1, true, 1}, {-2.5, -18.5, 1.1587395555581999}, 2047123106u, 2315578821u, 193206722u},
         {0.3, {0.22, false, 1, true, 1}, {-3.5, -16.5, 0.9745853773836346}, 1846220788u, 2315578821u, 3361842388u},
         {1.2, {0.22, false, 1, true, 1}, {-3.5, -16.5, 1.0408024940894889}, 283999798u, 2315578821u, 3606167830u},
         {2.7, {0.22, false, 1, true, 1}, {-3.5, -16.5, 1.0117934604316234}, 3793155544u, 2315578821u, 1841188856u},
         {4.8, {0.22, false, 1, true, 1}, {-3.5, -18.5, 1.1481057264715142}, 4122664817u, 2315578821u, 907776433u},
         {0.3, {0.4, false, 1, false, 0.65}, {-4.5, -17.5, 0.7863006004863687}, 4040994141u, 2315578821u, 4224809693u},
         {0.3, {0.22, true, 1, true, 1}, {0, -19, -1}, 4149335649u, 2315578821u, 1613879265u},
         {0.3, {0.22, false, 0.45, true, 1}, {-3.5, -16.5, 0.9745853773836346}, 1696156320u, 2315578821u, 3361842388u},
         {0.3, {0.22, false, 1, true, 1.3}, {-5.5, -18.5, 1.1683075939190704}, 3245439159u, 2315578821u, 466289143u}}};
    PixelFx           source(PixelFxKind::GemSpin);
    const PixelFxInfo info{PixelFxKind::GemSpin, "fixture", 80, 80, 40, 60, 1, true};
    for (const auto& f : fixtures) {
        std::vector<uint32_t> colors(80 * 80), lights(80 * 80);
        Paint                 p{info, colors, lights, 1};
        Layer<32, 32>         L;
        const auto            hi = drawGem(L, p, mesh, -.4, -18.7, f.spin, source.settings().palette, f.options);
        REQUIRE(std::abs(hi.x - f.hi.x) < 1e-12);
        REQUIRE(std::abs(hi.y - f.hi.y) < 1e-12);
        REQUIRE(std::abs(hi.k - f.hi.k) < 1e-12);
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
        REQUIRE(checksum(L.colors) == f.layer);
    }
}
TEST_CASE("stylize.pixelfx.pickups.crystalShardSourceFracturePulseAndTransactions") {
    constexpr std::array<double, 21>   times{{0,
                                              0.008333333333333333,
                                              0.3,
                                              0.8,
                                              1.1833333333333333,
                                              1.25,
                                              1.2583333333333333,
                                              1.2666666666666666,
                                              1.275,
                                              1.2833333333333334,
                                              1.5916666666666666,
                                              1.6,
                                              1.6083333333333334,
                                              2.45,
                                              2.5166666666666666,
                                              2.525,
                                              2.533333333333333,
                                              2.5416666666666665,
                                              2.55,
                                              3.191666666666667,
                                              3.2}};
    constexpr std::array<uint32_t, 42> expected{
        {1747663712u, 1807596055u, 2498988958u, 531528254u,  855360523u,  180757866u,  3157359003u,
         4118418389u, 4056035186u, 1702867736u, 2931337560u, 711073407u,  2967098878u, 3127208809u,
         2653058435u, 993906026u,  93395287u,   774177597u,  3760949670u, 1658392873u, 2717070023u,
         1792857192u, 806748997u,  2160933954u, 4100232814u, 3065098214u, 637988078u,  973206002u,
         166369024u,  1846517344u, 1513162160u, 3497440633u, 2949230525u, 3445406398u, 110528942u,
         4224609187u, 2223073804u, 2937044158u, 2829652642u, 1195586563u, 1747663712u, 1807596055u}};
    PixelFx fx(PixelFxKind::CrystalShard), other(PixelFxKind::CrystalShard);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::CrystalShard, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::CrystalShard, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CrystalShard, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::CrystalShard, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(3.2);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.poolSourceTransformAndCutoff") {
    struct Fixture {
        bool     transform;
        double   y, rx, ry, k;
        uint32_t color, light;
    };
    constexpr std::array<Fixture, 8> fixtures{{{false, 0.5, 20, 3.5, 0.3, 2315578821u, 2392254261u},
                                               {false, -0.25, 7.25, 2.5, 0.9, 2315578821u, 2637662313u},
                                               {false, 0.5, 0.5, 3.5, 0.3, 2315578821u, 2315578821u},
                                               {false, 0.5, 20, 3.5, 0.004, 2315578821u, 2315578821u},
                                               {true, 0.5, 20, 3.5, 0.3, 2315578821u, 2547717213u},
                                               {true, -0.25, 7.25, 2.5, 0.9, 2315578821u, 850089441u},
                                               {true, 0.5, 0.5, 3.5, 0.3, 2315578821u, 2315578821u},
                                               {true, 0.5, 20, 3.5, 0.004, 2315578821u, 2315578821u}}};
    const PixelFxInfo                info{PixelFxKind::CrystalShard, "fixture", 80, 80, 40, 60, 1, true};
    for (const auto& f : fixtures) {
        std::vector<uint32_t> colors(80 * 80), lights(80 * 80);
        pixelfx::Paint        p{info, colors, lights, 1};
        if (f.transform) p.place(7, -3, true, .2);
        pixelfx::pickup::pool(p, 0, f.y, f.rx, f.ry, 0xff1878e0u, f.k);
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.pickups.xpOrbSourceLayersAndTransactions") {
    constexpr std::array<double, 15> times{{0, 0.008333333333333333, 0.06666666666666667, 0.075, 0.44166666666666665,
                                            0.45, 0.4583333333333333, 0.5916666666666667, 0.6, 0.6083333333333333, 0.9,
                                            1.2, 1.5, 1.7916666666666667, 1.8}};
    constexpr std::array<uint32_t, 30> expected{
        {1045780601u, 1346090272u, 1520256292u, 1949549128u, 3099524653u, 2668107385u, 1732185878u, 2418911329u,
         140107451u,  4116300549u, 38984499u,   2526636321u, 3373184656u, 3749976213u, 921659198u,  2064648479u,
         841774379u,  3081375479u, 3384688447u, 305123316u,  68383172u,   2225292552u, 2484899706u, 2230771984u,
         3170266975u, 1741735545u, 1392625565u, 2446914088u, 1045780601u, 1346090272u}};
    PixelFx fx(PixelFxKind::XpOrb), other(PixelFxKind::XpOrb);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::XpOrb, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::XpOrb, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::XpOrb, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::XpOrb, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(1.8);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.lootBeamSourceLayersAndTransactions") {
    constexpr std::array<double, 16> times{{0, 0.008333333333333333, 0.19166666666666668, 0.2, 0.525,
                                            0.5333333333333333, 1.1916666666666667, 1.2, 1.3916666666666666, 1.4, 1.725,
                                            1.7333333333333334, 2.1083333333333334, 2.1166666666666667,
                                            2.3916666666666666, 2.4}};
    constexpr std::array<uint32_t, 32> expected{
        {3037485034u, 72907475u,   2692723798u, 672967404u,  1018484032u, 2485438036u, 3702117727u, 3611615514u,
         1097597979u, 375358918u,  819738944u,  1272623225u, 804661893u,  427448635u,  2399903026u, 1432756378u,
         2832124749u, 439484081u,  3308051972u, 754397521u,  4249602385u, 822183900u,  2550703524u, 1151668501u,
         576273226u,  1489309848u, 2425145206u, 1605322977u, 3425636037u, 3897912979u, 3037485034u, 72907475u}};
    PixelFx fx(PixelFxKind::LootBeam), other(PixelFxKind::LootBeam);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::LootBeam, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::LootBeam, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LootBeam, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::LootBeam, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(2.4);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.powerStarSourceGeometryAndTransactions") {
    constexpr std::array<double, 14> times{{0, 0.008333333333333333, 0.2, 0.39166666666666666, 0.4, 0.4083333333333333,
                                            0.7916666666666666, 0.8, 0.8083333333333333, 1.1916666666666667, 1.2,
                                            1.2083333333333333, 1.5916666666666666, 1.6}};
    constexpr std::array<uint32_t, 28> expected{
        {1853047644u, 2137902653u, 866109126u,  3452992917u, 4155282803u, 1431207908u, 1082200654u,
         525030479u,  500702342u,  2253038412u, 2034954862u, 867472199u,  1274034946u, 118274228u,
         3637906662u, 2459458816u, 623560429u,  461307033u,  2694409754u, 2275190147u, 479963276u,
         2472350178u, 3491203675u, 1953617150u, 2497949040u, 2995631524u, 1853047644u, 2137902653u}};
    PixelFx fx(PixelFxKind::PowerStar), other(PixelFxKind::PowerStar);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::PowerStar, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::PowerStar, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::PowerStar, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::PowerStar, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(1.6);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.keyGlintSourceGeometryAndTransactions") {
    constexpr std::array<double, 15> times{{0, 0.008333333333333333, 0.2, 0.35833333333333334, 0.36666666666666664, 0.8,
                                            0.9583333333333334, 0.9666666666666667, 0.9916666666666667, 1,
                                            1.0083333333333333, 1.2, 1.5, 1.9916666666666667, 2}};
    constexpr std::array<uint32_t, 30> expected{
        {41801191u,   547842383u,  3095607390u, 2279270802u, 3487200533u, 3038963168u, 3901440084u, 4033106399u,
         1994443136u, 1881860689u, 148400683u,  2513311783u, 4003365688u, 3397422576u, 3432608832u, 299947593u,
         1434863294u, 938518091u,  4283032088u, 2680835376u, 2005727320u, 2323509353u, 3452370906u, 1515876139u,
         4043674526u, 2598737896u, 1399673972u, 319065961u,  41801191u,   547842383u}};
    PixelFx fx(PixelFxKind::KeyGlint), other(PixelFxKind::KeyGlint);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::KeyGlint, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::KeyGlint, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::KeyGlint, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::KeyGlint, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(2.0);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.heartPickupSourceLayersAndTransactions") {
    constexpr std::array<double, 18>   times{{0, 0.008333333333333333, 0.041666666666666664, 0.05, 0.058333333333333334,
                                              0.14166666666666666, 0.15, 0.15833333333333333, 0.2833333333333333,
                                              0.2916666666666667, 0.3, 0.38333333333333336, 0.39166666666666666, 0.4,
                                              0.7916666666666666, 0.8, 1.1916666666666667, 1.2}};
    constexpr std::array<uint32_t, 36> expected{
        {2375258286u, 535929874u,  3272342414u, 1247394592u, 642107278u,  255358815u,  3662085608u, 1012431719u,
         4145574176u, 1028922725u, 621578432u,  2772051204u, 399909625u,  825147028u,  863560011u,  1813213287u,
         1571500376u, 4290667237u, 3082532726u, 1999521975u, 3765893663u, 1837627764u, 2332477761u, 2535047085u,
         2332477761u, 4265401494u, 2067887719u, 3954590769u, 1394701571u, 3746317959u, 1394701571u, 927066818u,
         1437218542u, 738738747u,  2375258286u, 535929874u}};
    PixelFx fx(PixelFxKind::HeartPickup), other(PixelFxKind::HeartPickup);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::HeartPickup, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HeartPickup, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HeartPickup, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HeartPickup, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(1.2);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.potionSourceLayersAndTransactions") {
    constexpr std::array<double, 15>   times{{0, 0.008333333333333333, 0.2, 0.5916666666666667, 0.6, 0.6083333333333333,
                                              0.8, 1.1916666666666667, 1.2, 1.2083333333333333, 1.7916666666666667, 1.8,
                                              1.8083333333333333, 2.3916666666666666, 2.4}};
    constexpr std::array<uint32_t, 30> expected{
        {1178929331u, 3710525825u, 2861428257u, 3943419929u, 3437038098u, 2457399518u, 343733665u,  1386680122u,
         3864742662u, 1692127224u, 4185140716u, 3194296875u, 2463505450u, 1374336181u, 2361104546u, 1691027821u,
         3864475265u, 830103927u,  241871442u,  841731471u,  3385325429u, 2569213581u, 1270519803u, 2925585418u,
         2858507753u, 3306846844u, 4170249809u, 3028845449u, 1178929331u, 3710525825u}};
    PixelFx fx(PixelFxKind::Potion), other(PixelFxKind::Potion);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i]);
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    fx.render(.375);
    other.render(.375);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Potion, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.375);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.render(.375);
    REQUIRE(checksum(other.color()) == original);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("height", eve::Value(40.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Potion, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Potion, settings));
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Potion, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(0);
    const auto seamColor = checksum(fx.color()), seamLight = checksum(fx.light());
    fx.render(2.4);
    REQUIRE(checksum(fx.color()) == seamColor);
    REQUIRE(checksum(fx.light()) == seamLight);
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
TEST_CASE("stylize.pixelfx.pickups.heartSdfAndPillowSourceClipLighting") {
    using namespace pixelfx;
    using namespace pixelfx::pickup;
    constexpr std::array<std::array<double, 3>, 11> points{{{{-9, -20, 0.7678327619723889}},
                                                            {{-7.2, -20, 0.2704179697998515}},
                                                            {{-4, -24, -1.2512401860611648}},
                                                            {{0, -25, -0.05068585244153673}},
                                                            {{0, -20, -8.645614035087721}},
                                                            {{0, -12, 0.2890248878008973}},
                                                            {{5, -17, 0.13606821174075426}},
                                                            {{7.2, -20, 0.2704179697998515}},
                                                            {{8, -21, 0.5093701871877883}},
                                                            {{1.3, -23.4, -0.9356545685689526}},
                                                            {{-0.5, -20.5, -4.690696936570476}}}};
    for (auto q : points) REQUIRE(std::abs(heartSdf(q[0], q[1], 0, -20, 7.2, 7.2) - q[2]) < 1e-12);
    struct Fixture {
        double                sx, sy, bevel;
        std::array<double, 3> light;
        bool                  clip, transform;
        uint32_t              color, targetLight, layer;
    };
    constexpr std::array<Fixture, 6> fixtures{
        {{1, 1, 3.6, {-0.5, -0.6, 0.62}, false, false, 2447389304u, 2315578821u, 1758194872u},
         {1.16, 0.87, 3.6, {-0.5, -0.6, 0.62}, false, false, 1167170960u, 2315578821u, 2867016016u},
         {0.94, 1.12, 3.6, {-0.5, -0.6, 0.62}, false, false, 936987054u, 2315578821u, 2563721838u},
         {1, 1, 2, {0.4, -0.3, 0.72}, false, false, 1745857476u, 2315578821u, 1722843908u},
         {1, 1, 3.6, {-0.5, -0.6, 0.62}, true, false, 4220547654u, 2315578821u, 765062150u},
         {1, 1, 3.6, {-0.5, -0.6, 0.62}, false, true, 1606598686u, 2315578821u, 1758194872u}}};
    const PixelFxInfo info{PixelFxKind::HeartPickup, "fixture", 80, 80, 40, 60, 1, true};
    PixelFx           source(PixelFxKind::HeartPickup);
    const auto        C = source.settings().palette;
    for (const auto& f : fixtures) {
        std::vector<uint32_t> colors(80 * 80), lights(80 * 80);
        Paint                 p{info, colors, lights, 1};
        if (f.transform) p.place(7, -3, true, .2);
        Layer<40, 40> L;
        L.begin(-20, -40);
        const auto sdf   = [&](double x, double y) { return heartSdf(x, y, 0, -20, 7.2 * f.sx, 7.2 * f.sy); };
        const auto color = [&](double lt, double e) {
            return lt > .92 && e > 1.2 ? WHITE
                   : lt > .8           ? C[6]
                   : lt > .6           ? C[5]
                   : lt > .38          ? C[4]
                   : lt > .12          ? C[3]
                                       : C[2];
        };
        const PillowOptions o{f.bevel, f.light};
        if (f.clip)
            pillow(L, -12, -31, 12, -9, sdf, color, o, [](double x, double y) { return x > 0 && y < -20; });
        else
            pillow(L, -12, -31, 12, -9, sdf, color, o);
        L.outline(hex(0x12060c));
        L.draw(p);
        REQUIRE(checksum(colors) == f.color);
        REQUIRE(checksum(lights) == f.targetLight);
        REQUIRE(checksum(L.colors) == f.layer);
    }
}
TEST_CASE("stylize.pixelfx.pickups.collectBurstSourceSimulationAndTransactions") {
    constexpr std::array<double, 16> times{{0, 0.008333333333333333, 0.03333333333333333, 0.06666666666666667, 0.075,
                                            0.15, 0.15833333333333333, 0.16666666666666666, 0.31666666666666665, 0.325,
                                            0.5333333333333333, 0.5416666666666666, 0.75, 1, 1.35, 1.5}};
    constexpr std::array<uint32_t, 32> expected{
        {196222919u,  1538223623u, 3467556433u, 2727148191u, 4043315634u, 2823862899u, 960033348u,  1427596258u,
         3740512414u, 2976351538u, 2050590359u, 2834005793u, 3490467168u, 1063977336u, 1102581116u, 2305931255u,
         3976709252u, 3947549369u, 543095051u,  161333056u,  1537852545u, 909709466u,  2304858205u, 4246062992u,
         1596438099u, 1100307822u, 1850071771u, 1781112229u, 173277637u,  173277637u,  173277637u,  173277637u}};
    PixelFx fx(PixelFxKind::CollectBurst), other(PixelFxKind::CollectBurst);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto zero = fx.prepareSeek(0);
    REQUIRE(zero);
    REQUIRE(fx.commitSeek(std::move(zero).takeValue()));
    REQUIRE(fx.advance(.375));
    for (int i = 0; i < 45; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    REQUIRE(checksum(fx.color()) == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::CollectBurst, settings);
    REQUIRE(candidate);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    auto inactive = fx.configurationValue();
    inactive.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::CollectBurst, inactive));
    settings         = fx.settings();
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CollectBurst, settings));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.pickups.chestSourceSimulationAndMaterials") {
    constexpr std::array<double, 22>   times{{0,
                                              0.1,
                                              0.15,
                                              0.36666666666666664,
                                              0.55,
                                              0.6916666666666667,
                                              0.7916666666666666,
                                              0.8,
                                              0.8083333333333333,
                                              0.9,
                                              1.2,
                                              1.7,
                                              2.3,
                                              2.55,
                                              2.8,
                                              3.2916666666666665,
                                              3.3,
                                              3.308333333333333,
                                              3.5,
                                              3.7,
                                              4.6,
                                              5.6}};
    constexpr std::array<uint32_t, 44> expected{
        {499097535u,  1260464025u, 499097535u,  4063579649u, 2277718505u, 4062477358u, 1362528342u, 1763872108u,
         89165450u,   679173469u,  3200535503u, 1812210536u, 1154310695u, 478214297u,  669909254u,  2026856825u,
         370818517u,  1963141394u, 3303701472u, 1715416772u, 170248357u,  2306230310u, 4050775224u, 3068255855u,
         4292895103u, 747933972u,  2272475012u, 1803518183u, 172575159u,  1299214336u, 2998988180u, 2992550691u,
         2569014815u, 4270724913u, 2858045784u, 3986810440u, 1298414012u, 2901832254u, 3489461220u, 841149644u,
         163524037u,  163524037u,  163524037u,  163524037u}};
    PixelFx fx(PixelFxKind::ChestOpen), other(PixelFxKind::ChestOpen);
    REQUIRE(fx.isSimulated());
    REQUIRE(fx.settings().pickup.coins == 12);
    REQUIRE(fx.settings().pickup.wood.size() == 7u);
    REQUIRE(fx.settings().pickup.wood[0] < 0);
    for (size_t i = 0; i < times.size(); ++i) {
        auto seek = fx.prepareSeek(times[i]);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[2 * i]);
        REQUIRE(checksum(fx.light()) == expected[2 * i + 1]);
    }
    auto zero = fx.prepareSeek(0);
    REQUIRE(zero);
    REQUIRE(fx.commitSeek(std::move(zero).takeValue()));
    REQUIRE(fx.advance(3.4));
    for (int i = 0; i < 408; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    const auto original = checksum(fx.color());
    REQUIRE(original == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto roundTrip = PixelFx::prepareConfigurationValue(PixelFxKind::ChestOpen, fx.configurationValue());
    REQUIRE(roundTrip);
    REQUIRE(fx.commitConfiguration(std::move(roundTrip).takeValue()));
    REQUIRE(fx.advance(3.4));
    fx.draw();
    REQUIRE(checksum(fx.color()) == original);
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    auto stale = fx.prepareSeek(1);
    REQUIRE(stale);
    auto settings         = fx.settings();
    settings.seed         = 123;
    settings.pickup.coins = 4;
    settings.pickup.wood  = {0xff403020LL, 0xff806040LL, 0xffc0a080LL};
    auto prepared         = PixelFx::prepareConfiguration(PixelFxKind::ChestOpen, settings);
    REQUIRE(prepared);
    settings.pickup.wood[0] = 0xff000000LL;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.advance(3.4));
    fx.draw();
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().pickup.wood.size() == 7u);
    REQUIRE(fx.settings().pickup.wood[0] == 0xff403020LL - 4294967296LL);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    for (int count : {-1, 65}) {
        settings              = fx.settings();
        settings.pickup.coins = count;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChestOpen, settings));
    }
    for (auto wood : std::vector<std::vector<int64_t>>{
             {}, {0x00ffffffLL}, {-2147483649LL}, {4294967296LL}, std::vector<int64_t>(65, -1)}) {
        settings             = fx.settings();
        settings.pickup.wood = std::move(wood);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChestOpen, settings));
    }
    auto inactive = fx.configurationValue();
    inactive.find("options")->set("height", eve::Value(42.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::ChestOpen, inactive));
    auto fractional = fx.configurationValue();
    fractional.find("options")->set("coins", eve::Value(1.5));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::ChestOpen, fractional));
    REQUIRE(!fx.prepareSeek(1e12));
    REQUIRE(checksum(fx.color()) == changed);
    for (int count : {0, 64}) {
        settings              = other.settings();
        settings.pickup.coins = count;
        auto p                = PixelFx::prepareConfiguration(PixelFxKind::ChestOpen, settings);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        REQUIRE(fx.advance(1.2));
        fx.draw();
        REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
    }
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.pickups.chestSignedMaterialSourceEquality") {
    PixelFx fx(PixelFxKind::ChestOpen), signedDefault(PixelFxKind::ChestOpen);
    auto    settings = fx.settings();
    for (auto& c : settings.pickup.wood) c = int64_t(uint32_t(c));
    auto p = PixelFx::prepareConfiguration(PixelFxKind::ChestOpen, settings);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.advance(3.4));
    fx.draw();
    REQUIRE(signedDefault.advance(3.4));
    signedDefault.draw();
    REQUIRE(checksum(fx.color()) == 3735194515u);
    REQUIRE(checksum(fx.light()) == 2361374755u);
    REQUIRE(checksum(fx.color()) != checksum(signedDefault.color()));
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::ChestOpen, fx.configurationValue());
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.advance(3.4));
    fx.draw();
    REQUIRE(checksum(fx.color()) == 3735194515u);
    REQUIRE(checksum(fx.light()) == 2361374755u);
    auto metadata = PixelFx::describeParameters(PixelFxKind::ChestOpen);
    REQUIRE(metadata);
    bool found = false;
    for (const auto& parameter : *metadata.value().find("parameters")->getIf<eve::Value::Array>()) {
        if (parameter.find("name")->asString() != "wood") continue;
        REQUIRE(parameter.find("colorNumberRepresentation"));
        REQUIRE(parameter.find("colorNumberRepresentation")->asString() == "signed-or-unsigned-32");
        found = true;
    }
    REQUIRE(found);
}
TEST_CASE("stylize.pixelfx.pickups.coinAndGemletSourceFacets") {
    using namespace pixelfx;
    struct Fixture {
        bool     gem;
        double   x, y, r, axis, angle;
        bool     white;
        double   alpha;
        bool     snap, transform;
        uint32_t color, light, layer;
    };
    const std::array<Fixture, 12> fixtures{
        {{false, -3.5, -10.5, 3, 0.3, 0, false, 1, true, false, 2706286680u, 2315578821u, 1120434520u},
         {false, 2.7, -12.3, 3, -0.4, 1.1, false, 1, true, false, 1645320223u, 2315578821u, 3061228703u},
         {false, -3.2, -8.7, 3, 0.5, 1.55, false, 1, true, false, 332763229u, 2315578821u, 2094913629u},
         {false, 1.2, -9.3, 3, 0.2, 2.7, false, 1, true, false, 2252751083u, 2315578821u, 3313868907u},
         {false, 3.5, -1.5, 3, 0, 1.0471975511965979, false, 1, false, false, 2319080111u, 2315578821u, 1709406127u},
         {false, -2.3, -4.2, 3, 0.6, 0.4, true, 0.45, true, true, 2205343590u, 2315578821u, 3726553054u},
         {true, -3.5, -10.5, 2.6, 0, 0, false, 1, true, false, 1632169130u, 2315578821u, 1203062138u},
         {true, 2.7, -12.3, 2.6, 0, 0.4, false, 1, true, false, 2622138951u, 2315578821u, 4230347239u},
         {true, -3.2, -8.7, 2.6, 0, -1.1, false, 1, true, false, 2505811707u, 2315578821u, 1366632027u},
         {true, 1.2, -9.3, 2.6, 0, 2.7, false, 1, true, false, 4195019497u, 2315578821u, 3261406217u},
         {true, 3.5, -4.5, 2.6, 0, -0.4, true, 1, true, false, 434601522u, 2315578821u, 1385798418u},
         {true, -2.3, -4.2, 2.6, 0, 0.4, false, 0.45, true, true, 4218391957u, 2315578821u, 4230347239u}}};
    const std::array<uint32_t, 8> C{{hex(0x1e1206), hex(0x4a2c08), hex(0x86520c), hex(0xc48412), hex(0xf0b424),
                                     hex(0xffe066), hex(0xfff6c4), WHITE}};
    const std::array<uint32_t, 6> G{
        {hex(0x2a0612), hex(0x6e0f2a), hex(0xb81c3c), hex(0xf0405a), hex(0xff8a96), hex(0xffd2d6)}};
    const PixelFxInfo info{PixelFxKind::ChestOpen, "fixture", 80, 80, 40, 60, 1, false};
    PixelFx           source(PixelFxKind::ChestOpen);
    for (const auto& f : fixtures) {
        std::array<uint32_t, 6400> color{}, light{};
        Paint                      p{info, color, light, 1};
        if (f.transform) p.place(7, -3, true, .2);
        pickup::Layer<16, 16> L;
        if (f.gem)
            pickup::gemlet(L, p, f.x, f.y, f.r, f.angle, G, f.white, f.alpha);
        else
            pickup::coin(L, p, f.x, f.y, f.r, f.axis, f.angle, C, f.white, f.alpha, f.snap);
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
        REQUIRE(checksum(L.colors) == f.layer);
    }
}
}  // namespace test_pixelfx_pickups_unit
