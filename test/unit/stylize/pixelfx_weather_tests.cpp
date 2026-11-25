// Weather owns canvas clipping, looping skies and particle fields; separate from the large core suite.
#include <array>
#include <bit>
#include <limits>
#include <optional>
#include <vector>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Weather.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_weather_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.weather.snowReferenceLoopConfigurationAndFade") {
    constexpr std::array<uint32_t, 18> expected{{1945214620u, 3666573068u, 1053314865u, 314382215u, 2867546951u,
                                                 317981087u, 1506524913u, 3511846537u, 3969220083u, 172024300u,
                                                 4260177672u, 913406631u, 2337880632u, 3898726631u, 3100156548u,
                                                 153235663u, 1945214620u, 3666573068u}};
    PixelFx                            fx(PixelFxKind::Snow), split(PixelFxKind::Snow);
    REQUIRE(!fx.isSimulated());
    REQUIRE(fx.settings().seed == 6u);
    for (int i = 0; i < 9; ++i) {
        fx.render(i * 1.5);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    fx.render(0);
    REQUIRE(fx.advance(.6));
    for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    const auto original = checksum(fx.color());
    auto       snapshot = fx.configurationValue();
    snapshot.find("options")->set("seed", eve::Value(int64_t(123)));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Snow, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.6);
    REQUIRE(checksum(fx.color()) != original);
    auto settings    = fx.settings();
    settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
    candidate        = PixelFx::prepareConfiguration(PixelFxKind::Snow, settings);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.6);
    REQUIRE(fx.settings().palette.size() == 8u);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    settings.palette = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Snow, settings));
    REQUIRE(checksum(fx.color()) == changed);
    auto seek = fx.prepareSeek(1000000000000.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    fx.render(.6);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.weather.transformedClipBoundsAllWriteKinds") {
    namespace px = eve::stylize::pixelfx;
    PixelFxInfo           info{PixelFxKind::Snow, "test", 384, 244, 192, 202, 12, true};
    std::vector<uint32_t> colors(384 * 244), lights(384 * 244);
    px::Paint             p{info, colors, lights, 1};
    p.place(7, -3, true, .2);
    auto q = px::weather::clip(p, PixelFx(PixelFxKind::Snow).info());
    q.rect(-300, -300, 600, 600, 0xff556677u);
    q.disc(150, -165, 40, 0xffaabbccu, .5);
    q.glow(158, -168, 40, 0xffddeeffu, .8);
    size_t colored = 0, lit = 0;
    for (int y = -202; y < 42; ++y)
        for (int x = -192; x < 192; ++x) {
            const size_t index = size_t(y + 202) * 384 + size_t(x + 192);
            const double lx = p.fromX(x + .5, y + .5), ly = p.fromY(x + .5, y + .5);
            const bool   inside = lx >= -160 && lx < 160 && ly >= -170 && ly < 10;
            if (!inside) {
                REQUIRE(colors[index] == 0u);
                REQUIRE(lights[index] == 0u);
            }
            colored += colors[index] != 0;
            lit += lights[index] != 0;
        }
    REQUIRE(colored > 10000u);
    REQUIRE(lit > 100u);
    // A clipped view is a value: the original painter keeps unrestricted writes.
    REQUIRE(!p.clipRect.has_value());
    REQUIRE(q.clipRect.has_value());
}
TEST_CASE("stylize.pixelfx.weather.fogCloudsReferenceAndTransactions") {
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{2620666868u, 4150738036u, 2603200656u, 1218743126u, 2432108749u, 60374707u, 1419279752u, 3959129914u,
           950637670u, 429416813u, 2057853525u, 3772868738u, 2397485082u, 3196195768u, 1733879646u, 2808337239u,
           2620666868u, 4150738036u}},
         {{4272456366u, 1137378480u, 2397089626u, 3817291092u, 1006935528u, 3136460661u, 928811468u, 885676633u,
           1378247027u, 1294872955u, 1405907330u, 1123467323u, 3177564175u, 180964901u, 3818307132u, 3043572026u,
           4272456366u, 1137378480u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::Fog, PixelFxKind::Clouds}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(i * 1.5);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       oldSeek  = fx.prepareSeek(.6);
        REQUIRE(oldSeek);
        auto settings    = fx.settings();
        settings.seed    = 123;
        settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
        auto candidate   = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(candidate);
        settings.seed = 99;
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().seed == 123u);
        fx.render(.6);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        auto stale = fx.commitSeek(std::move(oldSeek).takeValue());
        REQUIRE(!stale);
        REQUIRE(checksum(fx.color()) == changed);
        auto invalid    = fx.settings();
        invalid.palette = {0x00ffffffu};
        REQUIRE(!PixelFx::prepareConfiguration(kind, invalid));
        REQUIRE(checksum(fx.color()) == changed);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.weather.preparedFogMapSeekOwnership") {
    std::optional<PixelFxPreparedSeek> pending;
    {
        PixelFx owner(PixelFxKind::Fog);
        auto    prepared = owner.prepareSeek(.75);
        REQUIRE(prepared);
        pending.emplace(std::move(prepared).takeValue());
    }
    PixelFx other(PixelFxKind::Fog);
    auto    foreign = other.commitSeek(std::move(*pending));
    REQUIRE(!foreign);
    REQUIRE(foreign.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    pending.reset();
}
TEST_CASE("stylize.pixelfx.weather.float32NoiseReferenceAndTiling") {
    namespace w = eve::stylize::pixelfx::weather;
    const std::array<w::Field, 3> fields{
        {w::Field(35, w::OCTAVES), w::Field(46, w::LOW_OCTAVES), w::Field(56, w::OCTAVES)}};
    constexpr std::array<uint32_t, 3>              expected{{4177247394u, 3972928761u, 451249705u}};
    constexpr std::array<std::array<double, 3>, 3> samples{
        {{{.6463377103209496, .6453584637492895, .579277753829956}},
         {{.40087689831852913, .3999094869941473, .5182277783751488}},
         {{.38733115419745445, .3865723116323352, .4668055474758148}}}};
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f    = fields[i];
        uint32_t    hash = 2166136261u;
        for (float value : f.d) {
            const auto bits = std::bit_cast<uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8) hash = (hash ^ ((bits >> shift) & 255u)) * 16777619u;
        }
        REQUIRE(hash == expected[i]);
        REQUIRE(f.sample(-.25, 128.5) == samples[i][0]);
        REQUIRE(f.sample(256.125, -.75) == samples[i][1]);
        REQUIRE(f.sample(12.5, 34.25) == samples[i][2]);
        REQUIRE(f.sample(-.25, 128.5) == f.sample(255.75, .5));
    }
}
TEST_CASE("stylize.pixelfx.weather.auroraAndSunRaysReference") {
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{3387353485u, 1455557810u, 1913185295u, 3188017595u, 4024403125u, 1431562665u, 124663038u, 2014380762u,
           1896314777u, 265138420u, 609489400u, 1126204472u, 2130537864u, 1931868367u, 3864559494u, 817193512u,
           3387353485u, 1455557810u}},
         {{3737545568u, 1046541081u, 4132519485u, 4291366210u, 328998525u, 523748674u, 1605215777u, 3275223347u,
           666366027u, 3288915113u, 2977807530u, 675249655u, 513599711u, 1406968189u, 990597815u, 3915331872u,
           3737545568u, 1046541081u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::GodRays, PixelFxKind::Aurora}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(i * fx.info().duration / 8);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       settings = fx.settings();
        settings.seed       = 123;
        settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
        auto candidate      = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.6);
        REQUIRE(checksum(fx.color()) != original);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.weather.auroraUpperRampAtomicAndInstanceOwned") {
    PixelFx fx(PixelFxKind::Aurora), other(PixelFxKind::Aurora);
    fx.render(3.6);
    const auto initial  = checksum(fx.color());
    auto       snapshot = fx.configurationValue();
    snapshot.find("options")->set("upper",
                                  eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                     eve::Value(int64_t(0xffc0a080))}));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Aurora, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(3.6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != initial);
    REQUIRE(fx.settings().weather.upper.size() == 7u);
    other.render(3.6);
    REQUIRE(checksum(other.color()) == initial);
    fx.render(3.6);
    REQUIRE(checksum(fx.color()) == changed);
    auto invalid = snapshot;
    invalid.find("options")->set("upper", eve::Value::array({}));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Aurora, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/upper");
    invalid = snapshot;
    invalid.find("options")->set("upper", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Aurora, invalid));
    REQUIRE(checksum(fx.color()) == changed);
}
TEST_CASE("stylize.pixelfx.weather.rainReferenceDensityAndReplay") {
    constexpr std::array<uint32_t, 18> expected{{1824310066u, 2972048436u, 3565674382u, 3561530827u, 2785159876u,
                                                 2421256274u, 2103140611u, 2622584148u, 1868573064u, 1645196736u,
                                                 3413675542u, 4024671789u, 2607614420u, 2077073122u, 2279834361u,
                                                 2715302360u, 1824310066u, 2972048436u}};
    PixelFx                            fx(PixelFxKind::Rain), split(PixelFxKind::Rain);
    REQUIRE(!fx.isSimulated());
    REQUIRE(fx.settings().weather.density == 1.);
    for (int i = 0; i < 9; ++i) {
        fx.render(double(i));
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    fx.render(0);
    REQUIRE(fx.advance(.6));
    for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.6);
    REQUIRE(stale);
    auto settings            = fx.settings();
    settings.seed            = 123;
    settings.weather.density = .6;
    auto candidate           = PixelFx::prepareConfiguration(PixelFxKind::Rain, settings);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().weather.density == .6);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(checksum(fx.color()) == changed);
    for (double value : {-1., 4.1, std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid            = fx.settings();
        invalid.weather.density = value;
        auto rejected           = PixelFx::prepareConfiguration(PixelFxKind::Rain, invalid);
        REQUIRE(!rejected);
        REQUIRE(rejected.status().diagnostics().front().path() == "/options/density");
        REQUIRE(checksum(fx.color()) == changed);
    }
    for (double value : {0., 4.}) {
        auto snapshot = fx.configurationValue();
        snapshot.find("options")->set("density", eve::Value(value));
        candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Rain, snapshot);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.6);
        REQUIRE(checksum(fx.color()) != changed);
    }
    auto far = fx.prepareSeek(1000000000000.);
    REQUIRE(far);
    REQUIRE(fx.commitSeek(std::move(far).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    fx.render(.6);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.weather.crownReferenceRasterAndLifetime") {
    namespace px = eve::stylize::pixelfx;
    constexpr std::array<uint32_t, 21> hashes{
        {1967527313u, 1035440285u, 931669808u,  931669808u,  3044417837u, 1571601861u, 1571601861u,
         1967527313u, 3438910633u, 3755647164u, 4125767240u, 2141107988u, 4133328173u, 1571601861u,
         1967527313u, 3438910633u, 732127343u,  3440483972u, 616241312u,  1792010733u, 1571601861u}};
    constexpr std::array<bool, 21> visible{{true, true, true,  true, true, false, false, true, true, true, true,
                                            true, true, false, true, true, true,  true,  true, true, false}};
    PixelFxInfo                    info{PixelFxKind::Rain, "test", 64, 40, 32, 30, 8, true};
    std::vector<uint32_t>          color(64 * 40), light(64 * 40);
    size_t                         index = 0;
    for (double size : {.2, .8, 1.})
        for (double age : {0., .025, .05, .09, .18, .3, .4}) {
            std::fill(color.begin(), color.end(), 0u);
            std::fill(light.begin(), light.end(), 0u);
            px::Paint  p{info, color, light, 1};
            const auto state =
                px::weather::crown(p, 1.25, 2.5, age, size, {0xffaabbccu, 0xff334455u, 0xffffffffu}, .314159);
            REQUIRE((state == px::weather::CrownState::Visible) == visible[index]);
            REQUIRE(checksum(color) == hashes[index]);
            for (auto c : light) REQUIRE(c == 0u);
            ++index;
        }
}
TEST_CASE("stylize.pixelfx.weather.blizzardAndMeteorsReferenceReplay") {
    // Independent complete planes at nearest original30Hz frames across each loop.
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{4170258060u, 2647406816u, 753011066u, 4171098795u, 1530897346u, 3254077509u, 3513457935u, 78855176u,
           1589199090u, 1439585785u, 3234015735u, 143859181u, 1235094655u, 3150612926u, 1419952088u, 4263025545u,
           4170258060u, 2647406816u}},
         {{1306756586u, 3104458333u, 1350858472u, 3287010915u, 2408249572u, 2993393791u, 4030054269u, 204606307u,
           1571364930u, 3093798711u, 1212834447u, 1886914893u, 2912072298u, 3964807298u, 3406403092u, 1462931425u,
           1306756586u, 3104458333u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::Blizzard, PixelFxKind::ShootingStars}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(i * fx.info().duration * 30 / 8) / 30);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       settings = fx.settings();
        settings.seed       = 123;
        settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
        auto candidate      = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.6);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        auto invalid    = fx.settings();
        invalid.palette = {0x00ffffffu};
        REQUIRE(!PixelFx::prepareConfiguration(kind, invalid));
        REQUIRE(checksum(fx.color()) == changed);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.weather.starfieldRampAtomicAndIndependent") {
    PixelFx fx(PixelFxKind::ShootingStars), other(PixelFxKind::ShootingStars);
    fx.render(.6);
    const auto original = checksum(fx.color());
    auto       snapshot = fx.configurationValue();
    snapshot.find("options")->set("sky",
                                  eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                     eve::Value(int64_t(0xffc0a080))}));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ShootingStars, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().weather.sky.size() == 5u);
    other.render(.6);
    REQUIRE(checksum(other.color()) == original);
    fx.render(.6);
    REQUIRE(checksum(fx.color()) == changed);
    auto invalid = snapshot;
    invalid.find("options")->set("sky", eve::Value::array({}));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::ShootingStars, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/sky");
    invalid = snapshot;
    invalid.find("options")->set("sky", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::ShootingStars, invalid));
    REQUIRE(checksum(fx.color()) == changed);
}
TEST_CASE("stylize.pixelfx.weather.leavesAndBlossomsReferenceReplay") {
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{2912234834u, 2926004704u, 958822769u, 1975521534u, 2951857261u, 1557821098u, 2919609560u, 691593561u,
           2601777220u, 4248442461u, 2890235886u, 525275017u, 3734859192u, 3753205685u, 725586558u, 1361260389u,
           2912234834u, 2926004704u}},
         {{3068652998u, 2862153015u, 3055803242u, 3470945540u, 1481977523u, 1683715102u, 2353392709u, 1840912411u,
           2543049540u, 2463313614u, 3384558587u, 1005873619u, 2451986599u, 3030254979u, 2751611178u, 3967517157u,
           3068652998u, 2862153015u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::FallingLeaves, PixelFxKind::CherryBlossoms}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (int i = 0; i < 9; ++i) {
            fx.render(std::round(i * fx.info().duration * 30 / 8) / 30);
            REQUIRE(checksum(fx.color()) == expected[row][size_t(i) * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][size_t(i) * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       settings = fx.settings();
        settings.seed       = 123;
        settings.palette    = {0xff403020u, 0xff806040u, 0xffc0a080u};
        auto candidate      = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        fx.render(.6);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        auto invalid    = fx.settings();
        invalid.palette = {0x00ffffffu};
        REQUIRE(!PixelFx::prepareConfiguration(kind, invalid));
        REQUIRE(checksum(fx.color()) == changed);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.weather.preparedAutumnMaterialsAndRasterOwnership") {
    PixelFx fx(PixelFxKind::FallingLeaves), other(PixelFxKind::FallingLeaves);
    fx.render(.6);
    const auto original = checksum(fx.color());
    const auto ground   = checksum(fx.color().subspan(size_t(fx.info().originY) * size_t(fx.info().width)));
    auto       old      = fx.prepareSeek(.6);
    REQUIRE(old);
    auto snapshot = fx.configurationValue();
    snapshot.find("options")->set("reds",
                                  eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                     eve::Value(int64_t(0xffc0a080))}));
    snapshot.find("options")->set(
        "golds", eve::Value::array({eve::Value(int64_t(0xff405080)), eve::Value(int64_t(0xffa0b0d0))}));
    auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::FallingLeaves, snapshot);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    fx.render(.6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(checksum(fx.color().subspan(size_t(fx.info().originY) * size_t(fx.info().width))) != ground);
    REQUIRE(fx.settings().weather.reds.size() == 6u);
    REQUIRE(fx.settings().weather.golds.size() == 6u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    REQUIRE(checksum(fx.color()) == changed);
    other.render(.6);
    REQUIRE(checksum(other.color()) == original);
    fx.render(.6);
    REQUIRE(checksum(fx.color()) == changed);
    auto invalid = snapshot;
    invalid.find("options")->set("reds", eve::Value::array({}));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::FallingLeaves, invalid);
    REQUIRE(!rejected);
    REQUIRE(rejected.status().diagnostics().front().path() == "/options/reds");
    invalid = snapshot;
    invalid.find("options")->set("golds", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FallingLeaves, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    std::optional<PixelFxPreparedSeek> pending;
    {
        PixelFx owner(PixelFxKind::CherryBlossoms);
        auto    prepared = owner.prepareSeek(.6);
        REQUIRE(prepared);
        pending.emplace(std::move(prepared).takeValue());
    }
    PixelFx cherry(PixelFxKind::CherryBlossoms);
    auto    foreign = cherry.commitSeek(std::move(*pending));
    REQUIRE(!foreign);
    REQUIRE(foreign.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    pending.reset();
}
TEST_CASE("stylize.pixelfx.weather.firefliesMeadowReferenceAndOwnership") {
    constexpr std::array<uint32_t, 18> expected{{3939413787u, 234358158u, 3611872792u, 332913266u, 4231114619u,
                                                 933673119u, 3169228443u, 1877076736u, 4193235324u, 2148866908u,
                                                 2621274410u, 2895488658u, 4277877160u, 3440781735u, 857826328u,
                                                 3456378663u, 3939413787u, 234358158u}};
    PixelFx                            fx(PixelFxKind::Fireflies), other(PixelFxKind::Fireflies);
    REQUIRE(!fx.isSimulated());
    for (int i = 0; i < 9; ++i) {
        fx.render(i * 1.5);
        REQUIRE(checksum(fx.color()) == expected[size_t(i) * 2]);
        REQUIRE(checksum(fx.light()) == expected[size_t(i) * 2 + 1]);
    }
    fx.render(.6);
    other.render(.6);
    const auto original = checksum(fx.color());
    auto       stale    = fx.prepareSeek(.6);
    REQUIRE(stale);
    auto value = fx.configurationValue();
    value.find("options")->set("grass",
                               eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                  eve::Value(int64_t(0xffc0a080))}));
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Fireflies, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().weather.grass.size() == 6u);
    fx.render(.6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(checksum(other.color()) == original);
    value.find("options")->set("grass", eve::Value::array({}));
    auto invalid = PixelFx::prepareConfigurationValue(PixelFxKind::Fireflies, value);
    REQUIRE(!invalid);
    REQUIRE(invalid.status().diagnostics().front().path() == "/options/grass");
    REQUIRE(checksum(fx.color()) == changed);
    auto far = fx.prepareSeek(1000000000000.);
    REQUIRE(far);
    REQUIRE(fx.commitSeek(std::move(far).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    fx.render(.6);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.weather.stormLeaderStrokesSheetAndStrikeReference") {
    constexpr std::array<int, 16>      samples{{0, 27, 78, 79, 89, 90, 91, 99, 102, 117, 144, 192, 194, 199, 260, 300}};
    constexpr std::array<uint32_t, 32> expected{
        {2194222391u, 2236050885u, 994216665u, 3931290101u, 2864263874u, 2236050885u, 794419446u,  614833844u,
         3974504288u, 260610086u,  414890354u, 3033602293u, 4161541087u, 2347422883u, 3012104802u, 927232367u,
         2191251353u, 759410634u,  425821315u, 3899262868u, 3997456058u, 2236050885u, 3762400976u, 1440095096u,
         1210142382u, 3100990395u, 480767972u, 3765510659u, 1728885943u, 2077140688u, 2194222391u, 2236050885u}};
    PixelFx fx(PixelFxKind::Storm), split(PixelFxKind::Storm);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < samples.size(); ++i) {
        fx.render(samples[i] / 30.);
        REQUIRE(checksum(fx.color()) == expected[i * 2]);
        REQUIRE(checksum(fx.light()) == expected[i * 2 + 1]);
    }
    REQUIRE(fx.advance(3.4));
    for (int i = 0; i < 408; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    fx.render(3.3);
    const auto original = checksum(fx.color()), lighting = checksum(fx.light());
    auto       old = fx.prepareSeek(3.3);
    REQUIRE(old);
    auto value = fx.configurationValue();
    value.find("options")->set("bolt",
                               eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                                  eve::Value(int64_t(0xffc0a080))}));
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Storm, value);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().weather.bolt.size() == 6u);
    fx.render(3.3);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(checksum(fx.light()) != lighting);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    split.render(3.3);
    REQUIRE(checksum(split.color()) == original);
    fx.render(3.3);
    REQUIRE(checksum(fx.color()) == changed);
    value.find("options")->set("bolt", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
    auto invalid = PixelFx::prepareConfigurationValue(PixelFxKind::Storm, value);
    REQUIRE(!invalid);
    REQUIRE(invalid.status().diagnostics().front().path() == "/options/bolt/0");
    REQUIRE(checksum(fx.color()) == changed);
    auto far = fx.prepareSeek(1000000000000.);
    REQUIRE(far);
    REQUIRE(fx.commitSeek(std::move(far).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    fx.render(3.3);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
}  // namespace test_pixelfx_weather_unit
