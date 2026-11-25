// Water rendering is a separate cohesive suite: the existing stylize suite is near 5000 lines.
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/WaterSimulation.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_water_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels, int mode = 0) {
    uint32_t h = 2166136261u;
    for (auto c : pixels) {
        if (mode == 1 && (c >> 24) != 255u) c = 0;
        if (mode == 2) c &= 0xff000000u;
        for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
    }
    return h;
}
TEST_CASE("stylize.pixelfx.water.ripplesReference") {
    // Pinned upstream, complete color and light at t=i*D/8, including the loop boundary.
    constexpr std::array<uint32_t, 18> expected{{2154383159u, 2521580475u, 642270610u, 3247924623u, 3711601299u,
                                                 1608281619u, 495678496u, 2785404277u, 3209774660u, 2534081614u,
                                                 455737640u, 978595668u, 3706222616u, 2907592284u, 3261092113u,
                                                 162672819u, 2154383159u, 2521580475u}};
    PixelFx                            fx(PixelFxKind::Ripples);
    REQUIRE(!fx.isSimulated());
    REQUIRE(fx.settings().seed == 2u);
    for (int i = 0; i < 9; ++i) {
        fx.render(i * .25);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.water.rainReferenceCoverageAlphaCrestsAndLight") {
    // Complete opaque crest/reflection cells, complete alpha and complete light from upstream.
    // The independent dense raw comparison also checks every translucent RGB cell with the
    // explicitly diagnosed one-LSB hypot/exp mixing contract; these hashes do not replace it.
    constexpr std::array<uint32_t, 27> expected{
        {36580012u,   4025941623u, 1872726711u, 2460138207u, 551867292u,  103356411u,  2199879024u,
         2450951124u, 733847151u,  1750053403u, 1979120135u, 2679344554u, 104972252u,  3095632877u,
         3785790474u, 1814021982u, 1118578978u, 1735367477u, 403180685u,  3782191959u, 2493548930u,
         3905250989u, 2031079200u, 3749987303u, 36580012u,   4025941623u, 1872726711u}};
    PixelFx fx(PixelFxKind::RainSplash);
    REQUIRE(fx.settings().seed == 9u);
    for (int i = 0; i < 9; ++i) {
        fx.render(std::round(i * 2.4 * 120 / 8) / 120);
        REQUIRE(checksum(fx.color(), 1) == expected[size_t(i) * 3]);
        REQUIRE(checksum(fx.color(), 2) == expected[size_t(i) * 3 + 1]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 3 + 2]);
    }
}
TEST_CASE("stylize.pixelfx.water.configurationReplayCadenceAndStop") {
    for (const auto kind :
         {PixelFxKind::Ripples, PixelFxKind::RainSplash, PixelFxKind::Bubbles, PixelFxKind::Fountain,
          PixelFxKind::WaterOrb, PixelFxKind::Whirlpool, PixelFxKind::WaterJet, PixelFxKind::Waterfall}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        REQUIRE(before == std::vector<uint32_t>(split.color().begin(), split.color().end()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        auto config = fx.configurationValue();
        config.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(kind, config);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.advance(.6));
        fx.draw();
        const std::vector<uint32_t> seeded(fx.color().begin(), fx.color().end());
        bool                        seedChangesFrame = seeded != before;
        for (int i = 0; i < 9; ++i) {
            const double time = i * fx.info().duration / 8;
            fx.render(time);
            split.render(time);
            seedChangesFrame = seedChangesFrame || checksum(fx.color()) != checksum(split.color());
        }
        REQUIRE(seedChangesFrame);
        fx.render(.6);
        auto invalid = config;
        invalid.find("options")->set("palette", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kind, invalid));
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(seeded == std::vector<uint32_t>(fx.color().begin(), fx.color().end()));
        auto changed    = fx.settings();
        changed.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
        candidate       = PixelFx::prepareConfiguration(kind, changed);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().palette.size() == 8u);
        REQUIRE(fx.advance(.6));
        fx.draw();
        REQUIRE(seeded != std::vector<uint32_t>(fx.color().begin(), fx.color().end()));
        fx.render(0);
        const auto hash = checksum(fx.color());
        auto       seek = fx.prepareSeek(fx.info().duration);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(hash == checksum(fx.color()));
        REQUIRE(!fx.done());
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0);
        for (auto c : fx.light()) REQUIRE(c == 0);
    }
}
TEST_CASE("stylize.pixelfx.water.dripAndBubblesReferencePlanes") {
    // Independent pinned upstream full-plane FNV1a hashes, sampled at t=i*D/8.
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{629262032u, 737163113u, 1509817685u, 6272573u, 2005453745u, 3941518265u, 1676491668u, 1276959789u,
           774104718u, 1382440761u, 3557698979u, 638954005u, 4214986190u, 1054232473u, 789922633u, 1723870026u,
           629262032u, 737163113u}},
         {{1776907339u, 1412596806u, 874912823u, 3855228549u, 2173663034u, 2565019827u, 3618121319u, 3415682640u,
           2295014679u, 1757854874u, 3327945527u, 2441203088u, 3158594035u, 1593922594u, 1868031804u, 4073691852u,
           1776907339u, 1412596806u}}}};
    size_t row = 0;
    for (const auto kind : {PixelFxKind::Drip, PixelFxKind::Bubbles}) {
        PixelFx fx(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.water.dripStrictBooleanHeightAndAtomicity") {
    using eve::Value;
    PixelFx fx(PixelFxKind::Drip);
    REQUIRE(fx.settings().water.source);
    REQUIRE(fx.settings().water.height == 62);
    auto original = fx.configurationValue();
    REQUIRE(original.find("options")->find("source")->isBool());
    REQUIRE(original.find("options")->find("source")->asBool());
    auto metadata = PixelFx::describeParameters(PixelFxKind::Drip);
    REQUIRE(metadata);
    bool foundBoolean = false;
    for (const auto& parameter : *metadata.value().find("parameters")->getIf<Value::Array>())
        if (parameter.find("name")->asString() == "source") {
            REQUIRE(parameter.find("type")->asString() == "boolean");
            REQUIRE(parameter.find("default")->asBool());
            REQUIRE(parameter.find("minimum") == nullptr);
            REQUIRE(parameter.find("maximum") == nullptr);
            foundBoolean = true;
        }
    REQUIRE(foundBoolean);
    fx.render(.6);
    const auto originalHash = checksum(fx.color());
    auto       config       = original;
    config.find("options")->set("source", Value(false));
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Drip, config);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    fx.render(.6);
    REQUIRE(!fx.settings().water.source);
    const auto noStoneHash = checksum(fx.color());
    REQUIRE(noStoneHash != originalHash);
    // No clock, buffer or configuration publication on a wrong type or out-of-range height.
    for (const auto& wrong :
         std::array<Value, 5>{{Value(int64_t(0)), Value(int64_t(1)), Value(0.), Value("false"), Value()}}) {
        auto invalid = config;
        invalid.find("options")->set("source", wrong);
        auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Drip, invalid);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().path() == "/options/source");
        REQUIRE(!fx.settings().water.source);
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(checksum(fx.color()) == noStoneHash);
    }
    auto invalid = config;
    invalid.find("options")->set("height", Value(-1.));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Drip, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/height");
    config.find("options")->set("height", Value(45.));
    prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Drip, config);
    REQUIRE(prepared);
    // The prepared candidate owns its boolean and height independently of the source Value tree.
    config.find("options")->set("source", Value(true));
    config.find("options")->set("height", Value(100.));
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.settings().water.source);
    REQUIRE(fx.settings().water.height == 45);
    REQUIRE(fx.configurationValue().find("options")->find("source")->isBool());
    REQUIRE(fx.advance(.6));
    fx.draw();
    REQUIRE(checksum(fx.color()) != noStoneHash);
    PixelFx split(PixelFxKind::Drip);
    split.configure(fx.settings());
    for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0);
    for (auto c : fx.light()) REQUIRE(c == 0);
    // Omitted typed options use source defaults, and source seed is retained even though drip has no seeded RNG.
    auto omitted = original;
    omitted.set("options", Value::object({}));
    prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Drip, omitted);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().water.source);
    REQUIRE(fx.settings().water.height == 62);
    REQUIRE(fx.settings().seed == 1u);
    fx.render(0);
    const auto loopHash = checksum(fx.color());
    fx.render(fx.info().duration);
    REQUIRE(checksum(fx.color()) == loopHash);
}
TEST_CASE("stylize.pixelfx.water.splashReferenceLandingAndStone") {
    constexpr std::array<uint32_t, 18> expected{{3468469234u, 4154685893u, 1408106407u, 757843427u, 3734065979u,
                                                 213189898u, 4269924104u, 1231205592u, 2215646479u, 3822432021u,
                                                 1396977180u, 3594371059u, 2127720534u, 2595861621u, 3830109322u,
                                                 2684880444u, 4154685893u, 4154685893u}};
    PixelFx                            fx(PixelFxKind::WaterSplash);
    REQUIRE(fx.isSimulated());
    REQUIRE(fx.settings().seed == 3u);
    REQUIRE(fx.settings().water.stone);
    for (int i = 0; i < 9; ++i) {
        const double time = std::round(i * 2.5 * 120 / 8) / 120;
        auto         seek = fx.prepareSeek(time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    REQUIRE(fx.done());
    auto config = fx.configurationValue();
    config.find("options")->set("stone", eve::Value(false));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WaterSplash, config);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.draw();
    for (auto c : fx.color()) REQUIRE(c == 0);
    REQUIRE(!fx.settings().water.stone);
    auto invalid = config;
    invalid.find("options")->set("stone", eve::Value(int64_t(0)));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::WaterSplash, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/stone");
    REQUIRE(!fx.settings().water.stone);
    // Source stone=false hides only the incoming stone; collision/crown simulation still takes place.
    PixelFx stone(PixelFxKind::WaterSplash);
    REQUIRE(fx.advance(.6));
    REQUIRE(stone.advance(.6));
    fx.draw();
    stone.draw();
    REQUIRE(checksum(fx.color()) == checksum(stone.color()));
    auto settings = fx.settings();
    settings.seed = 123;
    candidate     = PixelFx::prepareConfiguration(PixelFxKind::WaterSplash, settings);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.advance(.6));
    fx.draw();
    REQUIRE(checksum(fx.color()) != checksum(stone.color()));
    PixelFx split(PixelFxKind::WaterSplash);
    split.configure(fx.settings());
    for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0);
    for (auto c : fx.light()) REQUIRE(c == 0);
}
TEST_CASE("stylize.pixelfx.water.owningPhysicsDelayedAndAppendedDrops") {
    // Numeric fixtures from pinned water/_lib.js; callback order and append timing are observable.
    namespace w = eve::stylize::pixelfx::water;
    w::Drops drops;
    drops.add({1, -2, .1, 10, 3, -20, 2, 1});
    drops.add({-3, 2, 2, -4, 5, 30, 1, .06, .01});
    drops.add({7, 0, .1, 0, 0, -1, 1, 1});
    std::vector<std::array<double, 3>> events;
    auto                               land = [&](const w::Drop& d) {
        events.push_back({d.x, d.z, d.vh});
        if (d.s > 1) drops.add({d.x, d.z, .5, 0, 0, 15, 1, .2, 0, true});
    };
    w::Ripples rings;
    rings.add(1, -2, .6, 5, .7, {1, 3, .95, .06, 0, .02});
    w::Mist mist;
    mist.add({1, -2, 10, -3, 2, 7, .8, 0xfff5d386u, .3, .01});
    for (int step = 1; step <= 15; ++step) {
        drops.update(1. / 120, land);
        rings.update(1. / 120);
        mist.update(1. / 120);
        if (step == 1) {
            REQUIRE(events.size() == 1u);
            REQUIRE(drops.list.size() == 3u);
            REQUIRE(drops.list.back().h == .5);
            REQUIRE(drops.list.back().life == .2);
            REQUIRE(std::abs(drops.list[1].h - .03481741898148148) < 1e-12);
        }
        if (step == 3) {
            REQUIRE(events.size() == 2u);
            REQUIRE(drops.list.size() == 2u);
            REQUIRE(std::abs(drops.list.back().h - .5789521123288002) < 1e-12);
            REQUIRE(std::abs(drops.list.back().vh - 1.319496535011575) < 1e-12);
        }
        if (step == 8) {
            REQUIRE(events.size() == 3u);
            REQUIRE(drops.list.size() == 1u);
            REQUIRE(std::abs(drops.list[0].x - -3.1979682292052183) < 1e-12);
            REQUIRE(std::abs(drops.list[0].h - 2.301170993932953) < 1e-12);
        }
    }
    REQUIRE(drops.list.empty());
    REQUIRE(events.size() == 3u);
    constexpr std::array<std::array<double, 3>, 3> expected{
        {{1.0830902777777778, -1.9750729166666667, -26.794201388888887},
         {7, 0, -14.633870826099537},
         {1.0830902777777778, -1.9750729166666667, -32.70757460879232}}};
    for (size_t i = 0; i < 3; ++i)
        for (size_t c = 0; c < 3; ++c) REQUIRE(std::abs(events[i][c] - expected[i][c]) < 1e-12);
    REQUIRE(rings.list.size() == 1u);
    REQUIRE(rings.sequence == 1u);
    REQUIRE(std::abs(rings.list[0].age - .10499999999999998) < 1e-12);
    REQUIRE(mist.list.size() == 1u);
    REQUIRE(std::abs(mist.list[0].x - 1.9431585771879083) < 1e-12);
    REQUIRE(std::abs(mist.list[0].y - -2.3238318770321222) < 1e-12);
    REQUIRE(std::abs(mist.list[0].vy - -2.980751865771675) < 1e-12);
    REQUIRE(std::abs(mist.list[0].life - .6916666666666671) < 1e-12);
}
TEST_CASE("stylize.pixelfx.water.fountainAndOrbReferencePlanes") {
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{1869992893u, 2210292331u, 3535785716u, 1546788129u, 3425917912u, 2397232367u, 700598630u, 805731520u,
           542598398u, 4043440070u, 3497030674u, 695418849u, 2074714856u, 3701876823u, 1874401513u, 1135305728u,
           1869992893u, 2210292331u}},
         {{2329155771u, 2324420500u, 1327591907u, 3413291125u, 3648592057u, 1672242733u, 1651297034u, 1579258113u,
           2352153538u, 2702047318u, 4236639064u, 2704368537u, 4046346366u, 699873526u, 1868181698u, 3461700189u,
           2329155771u, 2324420500u}}}};
    size_t row = 0;
    for (const auto kind : {PixelFxKind::Fountain, PixelFxKind::WaterOrb}) {
        PixelFx fx(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(i * fx.info().duration * 120 / 8) / 120);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.water.orbRadiusAtomicConfigurationAndBounds") {
    PixelFx fx(PixelFxKind::WaterOrb);
    REQUIRE(fx.settings().water.radius == 19);
    fx.render(.6);
    const auto before = checksum(fx.color());
    for (double radius : std::array<double, 4>{{1.999, 40.001, -1, std::numeric_limits<double>::quiet_NaN()}}) {
        auto settings         = fx.settings();
        settings.water.radius = radius;
        auto rejected         = PixelFx::prepareConfiguration(PixelFxKind::WaterOrb, settings);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().path() == "/options/radius");
        REQUIRE(fx.settings().water.radius == 19);
        REQUIRE(fx.effectTime() == .6);
        REQUIRE(checksum(fx.color()) == before);
    }
    auto config = fx.configurationValue();
    config.find("options")->set("radius", eve::Value(25.));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WaterOrb, config);
    REQUIRE(candidate);
    config.find("options")->set("radius", eve::Value(40.));
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().water.radius == 25);
    fx.render(.6);
    REQUIRE(checksum(fx.color()) != before);
    for (double radius : {2., 40.}) {
        auto settings         = fx.settings();
        settings.water.radius = radius;
        candidate             = PixelFx::prepareConfiguration(PixelFxKind::WaterOrb, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        // Boundary radii keep the release point above the water, so the analytic landing root stays finite.
        for (double time : {0., .75, 1., 1.25, 2., 2.5, 3.}) fx.render(time);
    }
    PixelFx fountain(PixelFxKind::Fountain);
    auto    invalid = fountain.configurationValue();
    invalid.find("options")->set("radius", eve::Value(25.));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Fountain, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/radius");
}
TEST_CASE("stylize.pixelfx.water.whirlpoolReferencePlanes") {
    constexpr std::array<uint32_t, 18> expected{{3584696999u, 1740008559u, 1944037669u, 2368667937u, 3892544503u,
                                                 2541124396u, 2943447795u, 652839044u, 780722434u, 1518936497u,
                                                 709141234u, 4288078121u, 317062996u, 1670610648u, 1391677364u,
                                                 208558357u, 3584696999u, 1740008559u}};
    PixelFx                            fx(PixelFxKind::Whirlpool);
    REQUIRE(!fx.isSimulated());
    REQUIRE(fx.settings().seed == 5u);
    for (int i = 0; i < 9; ++i) {
        fx.render(i * 3. / 8);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
}
TEST_CASE("stylize.pixelfx.water.jetAndWaterfallReferencePlanes") {
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{147323017u, 470361509u, 3213172048u, 494771893u, 339302856u, 3381209035u, 1221469926u, 498826138u,
           995922214u, 2339657809u, 56166647u, 53756627u, 1313943839u, 2319689523u, 4079401725u, 955029357u, 147323017u,
           470361509u}},
         {{3708048949u, 4153207369u, 1304438461u, 2218953074u, 77758207u, 3508267491u, 1118891520u, 1893832339u,
           1647933052u, 3569946520u, 1487491447u, 3365715724u, 2060654524u, 2327076111u, 3839287214u, 2942416195u,
           3708048949u, 4153207369u}}}};
    size_t row = 0;
    for (const auto kind : {PixelFxKind::WaterJet, PixelFxKind::Waterfall}) {
        PixelFx fx(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(i * fx.info().duration / 8);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.water.preparedAnalyticGeometryTransactionsAndSeek") {
    PixelFx fx(PixelFxKind::Waterfall);
    fx.render(.5);
    const auto original = checksum(fx.color());
    auto       oldSeek  = fx.prepareSeek(.5);
    REQUIRE(oldSeek);
    auto config = fx.configurationValue();
    config.find("options")->set("height", eve::Value(64.5));
    config.find("options")->set("width", eve::Value(13.25));
    config.find("options")->set("seed", eve::Value(int64_t(123)));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Waterfall, config);
    REQUIRE(candidate);
    config.find("options")->set("height", eve::Value(200.));
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().water.height == 64.5);
    REQUIRE(fx.settings().water.width == 13.25);
    fx.render(.5);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    auto stale = fx.commitSeek(std::move(oldSeek).takeValue());
    REQUIRE(!stale);
    REQUIRE(stale.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(checksum(fx.color()) == changed);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("ledge", eve::Value(int64_t(0)));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Waterfall, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/ledge");
    REQUIRE(checksum(fx.color()) == changed);
    // Cached analytics stay analytic: huge seeks share the immutable program and never replay fixed ticks.
    auto far = fx.prepareSeek(1000000000000.);
    REQUIRE(far);
    REQUIRE(fx.commitSeek(std::move(far).takeValue()));
    fx.draw();
    fx.render(0);
    const auto zero = checksum(fx.color());
    fx.render(1000000000000.);
    REQUIRE(checksum(fx.color()) == zero);
    auto noLedge = fx.configurationValue();
    noLedge.find("options")->set("ledge", eve::Value(false));
    candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Waterfall, noLedge);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.5);
    REQUIRE(checksum(fx.color()) != changed);
    // A pending seek may outlive its originating instance; its program owns all prepared maps.
    std::optional<PixelFxPreparedSeek> orphan;
    {
        PixelFx owner(PixelFxKind::Waterfall);
        auto    prepared = owner.prepareSeek(.75);
        REQUIRE(prepared);
        orphan.emplace(std::move(prepared).takeValue());
    }
    auto foreign = fx.commitSeek(std::move(*orphan));
    REQUIRE(!foreign);
    REQUIRE(foreign.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    orphan.reset();
}
TEST_CASE("stylize.pixelfx.water.jetLengthAndCascadeBoundsAreAtomic") {
    for (const auto kind : {PixelFxKind::WaterJet, PixelFxKind::Waterfall}) {
        PixelFx fx(kind);
        fx.render(.5);
        const auto  hash    = checksum(fx.color());
        auto        invalid = fx.configurationValue();
        const char* field   = kind == PixelFxKind::WaterJet ? "length" : "height";
        invalid.find("options")->set(field, eve::Value(0.));
        auto rejected = PixelFx::prepareConfigurationValue(kind, invalid);
        REQUIRE(!rejected);
        REQUIRE(checksum(fx.color()) == hash);
        REQUIRE(fx.effectTime() == .5);
        auto settings = fx.settings();
        if (kind == PixelFxKind::WaterJet)
            settings.length = 90.5;
        else {
            settings.water.height = 64.5;
            settings.water.width  = 13.5;
        }
        auto candidate = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.5);
        REQUIRE(checksum(fx.color()) != hash);
    }
}
TEST_CASE("stylize.pixelfx.water.waveCrashReferenceAndReplay") {
    // Independent pinned source: nine complete color/light planes at 0.4 second intervals.
    constexpr std::array<uint32_t, 18> expected{{2451738053u, 2451738053u, 567826399u, 3285994308u, 3602637459u,
                                                 2626039059u, 1100546505u, 1638761653u, 3190348293u, 3290415718u,
                                                 1788553820u, 2274228397u, 2581640757u, 93896685u, 1886365225u,
                                                 733675776u, 2451738053u, 2451738053u}};
    PixelFx                            fx(PixelFxKind::WaveCrash), split(PixelFxKind::WaveCrash);
    auto                               sample = [&](double time) {
        auto prepared = fx.prepareSeek(time);
        REQUIRE(prepared);
        REQUIRE(fx.commitSeek(std::move(prepared).takeValue()));
        fx.draw();
    };
    REQUIRE(fx.isSimulated());
    REQUIRE(fx.settings().seed == 6u);
    for (int i = 0; i < 9; ++i) {
        sample(i * .4);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    sample(0);
    REQUIRE(fx.advance(1.6));
    for (int i = 0; i < 192; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    const auto original = checksum(fx.color());
    auto       snapshot = fx.configurationValue();
    snapshot.find("options")->set("seed", eve::Value(int64_t(123)));
    snapshot.find("options")->set("palette",
                                  eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                     eve::Value(int64_t(0xffc0a080))}));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WaveCrash, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(1.6);
    const auto changed = checksum(fx.color());
    REQUIRE(original != changed);
    auto invalid = snapshot;
    invalid.find("options")->set("seed", eve::Value(-1.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::WaveCrash, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    auto seek = fx.prepareSeek(1.6);
    REQUIRE(seek);
    REQUIRE(fx.advance(.2));
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(checksum(fx.color()) == changed);
    sample(3.2);
    REQUIRE(fx.done());
    sample(4.2);
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    sample(1.6);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.water.geyserReferenceHeightAndReplay") {
    // Independent pinned source: nine complete color/light planes at 0.625 second intervals.
    constexpr std::array<uint32_t, 18> expected{{2923595528u, 984161517u, 2584181431u, 1156693897u, 617512751u,
                                                 4049124163u, 64238063u, 4041707314u, 3336024695u, 4223915537u,
                                                 3323222478u, 1883243397u, 3102738085u, 2126994822u, 1288333694u,
                                                 1179140309u, 375241157u, 375241157u}};
    PixelFx                            fx(PixelFxKind::Geyser), split(PixelFxKind::Geyser);
    auto                               sample = [&](double time) {
        auto prepared = fx.prepareSeek(time);
        REQUIRE(prepared);
        REQUIRE(fx.commitSeek(std::move(prepared).takeValue()));
        fx.draw();
    };
    REQUIRE(fx.isSimulated());
    REQUIRE(fx.settings().seed == 4u);
    for (int i = 0; i < 9; ++i) {
        sample(i * .625);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    sample(0);
    REQUIRE(fx.advance(1.6));
    for (int i = 0; i < 192; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    const auto original = checksum(fx.color());
    auto       snapshot = fx.configurationValue();
    snapshot.find("options")->set("seed", eve::Value(int64_t(123)));
    snapshot.find("options")->set("palette",
                                  eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                     eve::Value(int64_t(0xffc0a080))}));
    snapshot.find("options")->set("height", eve::Value(72.5));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Geyser, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(1.6);
    const auto changed = checksum(fx.color());
    REQUIRE(original != changed);
    REQUIRE(fx.settings().water.height == 72.5);
    auto invalid = snapshot;
    invalid.find("options")->set("height", eve::Value(0.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Geyser, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    auto seek = fx.prepareSeek(1.6);
    REQUIRE(seek);
    REQUIRE(fx.advance(.2));
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(checksum(fx.color()) == changed);
    sample(5.);
    REQUIRE(fx.done());
    sample(6.);
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    sample(1.6);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
}
}  // namespace test_pixelfx_water_unit
