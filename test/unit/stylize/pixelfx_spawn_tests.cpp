
// Spawn/death kernels own their suite separately from completed Nature behavior.
#include <algorithm>
#include <array>
#include <limits>
#include "PixelFxFrameDiagnostic.h"
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/SpawnBody.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_spawn_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.spawn.coinDropPinnedPoofSpinBounceRestWinkAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2295793333u, 91401837u},
                                {0.008333333333333333, 3148833592u, 680849708u},
                                {0.03333333333333333, 3035044296u, 3920697191u},
                                {0.041666666666666664, 3245639102u, 1451240610u},
                                {0.06666666666666667, 2757657518u, 2463263250u},
                                {0.075, 4078257369u, 1611029878u},
                                {0.1, 707580432u, 1263103912u},
                                {0.2, 1394459264u, 3723963242u},
                                {0.4, 2606164856u, 3373707103u},
                                {0.6, 3945631246u, 3605579314u},
                                {0.8, 565887186u, 3891191756u},
                                {1, 1152739100u, 3448304127u},
                                {1.2, 3462539471u, 1286297433u},
                                {1.4, 174841058u, 3860601981u},
                                {1.6, 4128136322u, 3261081265u},
                                {2, 3237288607u, 3229450257u},
                                {2.15, 4170098250u, 1596803153u},
                                {2.2, 4293872850u, 1393086812u},
                                {2.3, 2049858983u, 1069070637u},
                                {2.4, 3882176585u, 2911709221u},
                                {2.5, 1019213773u, 3798673224u},
                                {2.6, 3443403243u, 3953886306u},
                                {2.7, 1146566041u, 2517952042u},
                                {2.8, 3312341333u, 2334817912u},
                                {3, 1102400965u, 1033408348u},
                                {4, 1102400965u, 1102400965u},
                                {5, 1102400965u, 1102400965u}};
    PixelFx         fx(PixelFxKind::CoinDrop);
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
TEST_CASE("stylize.pixelfx.spawn.coinDropCountSmokeOwnershipBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::CoinDrop);
    auto    s = fx.settings();
    REQUIRE(s.spawn.coins == 7);
    REQUIRE(s.spawn.smoke.size() == 4);
    s.spawn.smoke = {0xff112233u};
    s.spawn.coins = 11;
    auto p        = PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, s);
    REQUIRE(p);
    s.spawn.smoke[0] = 0xffabcdefu;
    s.spawn.coins    = 2;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.coins == 11);
    for (auto c : owned.spawn.smoke) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::CoinDrop, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.smoke.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    auto bad        = owned;
    bad.spawn.smoke = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, bad));
    bad.spawn.smoke.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, bad));
    for (int count : {-1, 65}) {
        bad             = owned;
        bad.spawn.coins = count;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (int count : {0, 1, 64}) {
        auto valid        = owned;
        valid.spawn.coins = count;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, valid));
    }
}
TEST_CASE("stylize.pixelfx.spawn.coinDropZeroCoinsSourcePoofOnly") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 3774184949u, 2288986789u},
                                {0.008333333333333333, 3896988572u, 2899391455u},
                                {0.06666666666666667, 2237033934u, 2918681239u},
                                {0.075, 2286147726u, 3871785074u},
                                {0.2, 288720330u, 2465252563u},
                                {0.5, 1477496177u, 4284923779u},
                                {1, 1102400965u, 1102400965u},
                                {3, 1102400965u, 1102400965u},
                                {5, 1102400965u, 1102400965u}};
    PixelFx         fx(PixelFxKind::CoinDrop);
    auto            s = fx.settings();
    s.seed            = 0;
    s.spawn.coins     = 0;
    auto p            = PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.spawn.coinDropMaximumCountSourceContinuationPastDuration") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{3, 3285410335u, 1894892835u},  {4, 3695354829u, 757242503u},
                                {6, 888008937u, 1726970379u},   {6.5, 62578938u, 502060487u},
                                {6.6, 4128484781u, 651478932u}, {7.2, 1102400965u, 1102400965u},
                                {8, 1102400965u, 1102400965u},  {9, 1102400965u, 1102400965u}};
    PixelFx         fx(PixelFxKind::CoinDrop);
    auto            s = fx.settings();
    s.seed            = 4294967295u;
    s.spawn.coins     = 64;
    auto p            = PixelFx::prepareConfiguration(PixelFxKind::CoinDrop, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.spawn.coinDropLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::CoinDrop);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.coinDropPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::CoinDrop), reference(PixelFxKind::CoinDrop);
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

TEST_CASE("stylize.pixelfx.spawn.coinDropStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::CoinDrop);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.bodyPinnedColorsHolesRimAndOwnership") {
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
    std::array<uint32_t, 8> palette{4279246896u, 4280431428u, 4282668390u, 4284905352u,
                                    4287142314u, 4289379276u, 4291616238u, 4294967295u};
    const auto              body = pixelfx::spawn::bodyOf(a, palette);
    const auto              rim  = pixelfx::spawn::rimOf(body);
    a.mask.assign(20, 0);
    a.pixels.assign(20, 0);
    palette.fill(0);
    constexpr pixelfx::spawn::BodyPixel expected[] = {
        {-2.25, -4.5, 0, 0, 4289379276u, true}, {-1.25, -4.5, 1, 0, 4279383126u, true},
        {-0.25, -4.5, 2, 0, 2148606515u, true}, {-2.25, -3.5, 0, 1, 4279383126u, true},
        {-0.25, -3.5, 2, 1, 1122867u, true},    {0.75, -3.5, 3, 1, 4287142314u, true},
        {-2.25, -2.5, 0, 2, 2148606515u, true}, {-1.25, -2.5, 1, 2, 1122867u, true},
        {-0.25, -2.5, 2, 2, 4287142314u, true}, {1.75, -2.5, 4, 2, 2148606515u, true},
        {-0.25, -1.5, 2, 3, 4279383126u, true}, {0.75, -1.5, 3, 3, 2148606515u, true},
        {1.75, -1.5, 4, 3, 1122867u, true}};
    constexpr pixelfx::spawn::RimPixel outside[] = {
        {-2.25, -5.5, 0, -1}, {-1.25, -5.5, 1, -1}, {-0.25, -5.5, 2, -1}, {-3.25, -4.5, -1, 0}, {0.75, -4.5, 3, 0},
        {-3.25, -3.5, -1, 1}, {-1.25, -3.5, 1, 1},  {1.75, -3.5, 4, 1},   {-3.25, -2.5, -1, 2}, {0.75, -2.5, 3, 2},
        {2.75, -2.5, 5, 2},   {-2.25, -1.5, 0, 3},  {-1.25, -1.5, 1, 3},  {2.75, -1.5, 5, 3},   {-0.25, -0.5, 2, 4},
        {0.75, -0.5, 3, 4},   {1.75, -0.5, 4, 4}};
    constexpr int indices[] = {0, 1, 2, -1, -1, 3, -1, 4, 5, -1, 6, 7, 8, -1, 9, -1, -1, 10, 11, 12};
    REQUIRE(body.list.size() == std::size(expected));
    REQUIRE(rim.size() == std::size(outside));
    for (size_t i = 0; i < body.list.size(); ++i) {
        const auto p = body.list[i], q = expected[i];
        REQUIRE(p.x == q.x);
        REQUIRE(p.y == q.y);
        REQUIRE(p.mx == q.mx);
        REQUIRE(p.my == q.my);
        REQUIRE(p.color == q.color);
        REQUIRE(p.edge == q.edge);
        REQUIRE(body.cellIndex(p.mx, p.my) == i);
    }
    for (size_t i = 0; i < rim.size(); ++i) {
        const auto p = rim[i], q = outside[i];
        REQUIRE(p.x == q.x);
        REQUIRE(p.y == q.y);
        REQUIRE(p.mx == q.mx);
        REQUIRE(p.my == q.my);
    }
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 5; ++x) {
            const int index = indices[y * 5 + x];
            REQUIRE(body.isOccupied(x, y) == (index >= 0));
            if (index < 0)
                REQUIRE(!body.cellIndex(x, y));
            else
                REQUIRE(body.cellIndex(x, y) == size_t(index));
        }
    for (const auto p : std::array<std::array<int, 2>, 4>{{{{-1, 0}}, {{5, 0}}, {{0, -1}}, {{0, 4}}}}) {
        REQUIRE(!body.isOccupied(p[0], p[1]));
        REQUIRE(!body.cellIndex(p[0], p[1]));
    }
}
TEST_CASE("stylize.pixelfx.spawn.bodyEmptyOneFallbackAndDenseBound") {
    const std::array<uint32_t, 8> palette{4279246896u, 4280431428u, 4282668390u, 4284905352u,
                                          4287142314u, 4289379276u, 4291616238u, 4294967295u};
    PixelFxActor                  a;
    a.source  = PixelFxActorSource::Custom;
    a.width   = 1;
    a.height  = 1;
    a.x       = 0;
    a.y       = -1;
    a.mask    = {1};
    a.pixels  = {0};
    auto body = pixelfx::spawn::bodyOf(a, palette);
    REQUIRE(body.list.size() == 1);
    REQUIRE(body.list[0].color == palette[5]);
    REQUIRE(body.list[0].edge);
    REQUIRE(pixelfx::spawn::rimOf(body).size() == 4);
    a.width  = 8;
    a.height = 3;
    a.mask.assign(24, 0);
    a.pixels.assign(24, 0);
    body = pixelfx::spawn::bodyOf(a, palette);
    REQUIRE(body.list.empty());
    REQUIRE(pixelfx::spawn::rimOf(body).empty());
    REQUIRE(!body.isOccupied(0, 0));
    a.width  = 256;
    a.height = 256;
    a.x      = 4096;
    a.y      = -4096;
    a.mask.assign(65536, 1);
    a.pixels.assign(65536, 0xff123456u);
    body = pixelfx::spawn::bodyOf(a, palette);
    REQUIRE(body.list.size() == 65536);
    REQUIRE(body.cells.size() == 65536);
    REQUIRE(pixelfx::spawn::rimOf(body).size() == 1024);
    REQUIRE(body.cellIndex(255, 255) == 65535);
    REQUIRE(body.list.back().x == 4351);
    REQUIRE(body.list.back().y == -3841);
    REQUIRE(!body.isOccupied(256, 255));
}


TEST_CASE("stylize.pixelfx.spawn.disintegratePinnedChargeWhitePeelWindAshAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1005068693u, 4154685893u},
                                {0.008333333333333333, 1005068693u, 2982740477u},
                                {0.2, 3277622379u, 3563653811u},
                                {0.30833333333333335, 1377317261u, 889037105u},
                                {0.31666666666666665, 3928262449u, 357177952u},
                                {0.325, 3191402557u, 4210545755u},
                                {0.36666666666666664, 2994500676u, 1007710110u},
                                {0.4166666666666667, 1270314252u, 2445975459u},
                                {0.425, 443002597u, 590199620u},
                                {0.43333333333333335, 3429439087u, 2388139705u},
                                {0.48333333333333334, 3086853653u, 3588134381u},
                                {0.49166666666666664, 1777410238u, 318504174u},
                                {0.5, 842347850u, 2243727436u},
                                {0.6, 4235118776u, 460016882u},
                                {0.8, 2566867822u, 898210369u},
                                {1, 2478615530u, 1824456929u},
                                {1.2, 3287078221u, 2779829323u},
                                {1.5, 393811601u, 1980964231u},
                                {1.6833333333333333, 1134735119u, 2288557507u},
                                {1.6916666666666667, 961834334u, 4204003064u},
                                {1.7, 2490994384u, 2453026180u},
                                {2, 4162455226u, 1729835256u},
                                {2.5, 348826804u, 946214220u},
                                {3.4, 4154685893u, 4154685893u},
                                {4, 4154685893u, 4154685893u},
                                {5, 4154685893u, 4154685893u}};
    PixelFx         fx(PixelFxKind::Disintegrate);
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
TEST_CASE("stylize.pixelfx.spawn.disintegrateAccentWindActorBoundsAndVisibility") {
    PixelFx fx(PixelFxKind::Disintegrate);
    auto    s = fx.settings();
    REQUIRE(s.actor.source == PixelFxActorSource::Dummy);
    REQUIRE(s.spawn.accent.size() == 3);
    REQUIRE(s.spawn.wind == 1);
    s.spawn.accent = {0xff112233u};
    s.spawn.wind   = -1.5;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, s);
    REQUIRE(p);
    s.spawn.accent[0] = 0xffabcdefu;
    s.spawn.wind      = 4;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.wind == -1.5);
    for (auto c : owned.spawn.accent) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Disintegrate, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.accent.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    auto bad         = owned;
    bad.spawn.accent = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, bad));
    bad.spawn.accent.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, bad));
    for (double wind :
         {-4.01, 4.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bad            = owned;
        bad.spawn.wind = wind;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double wind : {-4., 0., 4.}) {
        auto valid       = owned;
        valid.spawn.wind = wind;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, valid));
    }
    bad              = owned;
    bad.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Disintegrate, bad));
    REQUIRE(fx.configurationValue() == value);
    for (double t : {0., .31, .32, .42, .49, 1.5, 4.}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() ==
                (t < .42 - .1 ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden));
    }
}
TEST_CASE("stylize.pixelfx.spawn.disintegratePinnedHeroCustomFallbackEmptyAndInputOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3198113349u, 4154685893u},
                                    {0.008333333333333333, 3198113349u, 2982740477u},
                                    {0.30833333333333335, 599679537u, 889037105u},
                                    {0.31666666666666665, 2482073949u, 357177952u},
                                    {0.325, 3095095256u, 4210545755u},
                                    {0.4166666666666667, 120835799u, 2445975459u},
                                    {0.425, 3553832781u, 590199620u},
                                    {0.48333333333333334, 4018416873u, 3588134381u},
                                    {0.49166666666666664, 1717227872u, 2057374129u},
                                    {0.8, 806575645u, 1793893043u},
                                    {1.5, 4030644346u, 4240866770u},
                                    {2.5, 2833401197u, 279426581u},
                                    {3.4, 4154685893u, 4154685893u},
                                    {5, 4154685893u, 4154685893u}};
        auto            actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::Disintegrate);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(5)));
        options->set("wind", eve::Value(double(1)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Disintegrate, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 3520076517u, 4154685893u},
                                    {0.008333333333333333, 3520076517u, 2982740477u},
                                    {0.30833333333333335, 635868571u, 477862888u},
                                    {0.31666666666666665, 2284080195u, 1164135733u},
                                    {0.325, 628147500u, 1540001483u},
                                    {0.4166666666666667, 3419816560u, 2237620812u},
                                    {0.425, 2235157847u, 2155301800u},
                                    {0.48333333333333334, 1549645827u, 1475375599u},
                                    {0.49166666666666664, 2626665708u, 2649471307u},
                                    {0.8, 3117261658u, 4127064147u},
                                    {1.5, 1817497725u, 2059894607u},
                                    {2.5, 4154685893u, 4154685893u},
                                    {3.4, 4154685893u, 4154685893u},
                                    {5, 4154685893u, 4154685893u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::Disintegrate);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(123)));
        options->set("wind", eve::Value(double(-2)));
        options->set("accent", eve::Value::array({eve::Value(int64_t(4294967295u)), eve::Value(int64_t(4294901760u))}));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Disintegrate, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 2298996561u, 4154685893u},
                                    {0.008333333333333333, 2298996561u, 2982740477u},
                                    {0.30833333333333335, 2579111077u, 889037105u},
                                    {0.31666666666666665, 1120759317u, 357177952u},
                                    {0.325, 18157197u, 4210545755u},
                                    {0.4166666666666667, 4050472191u, 2445975459u},
                                    {0.425, 4135375895u, 590199620u},
                                    {0.48333333333333334, 2976246861u, 3588134381u},
                                    {0.49166666666666664, 2928666582u, 763771108u},
                                    {0.8, 644457767u, 390995415u},
                                    {1.5, 3464273163u, 1335125221u},
                                    {2.5, 4154685893u, 4154685893u},
                                    {3.4, 4154685893u, 4154685893u},
                                    {5, 4154685893u, 4154685893u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::Disintegrate);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(5)));
        options->set("wind", eve::Value(double(4)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Disintegrate, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 4154685893u, 4154685893u},
                                    {0.008333333333333333, 4154685893u, 2982740477u},
                                    {0.30833333333333335, 3707107293u, 889037105u},
                                    {0.31666666666666665, 1296686241u, 357177952u},
                                    {0.325, 328981301u, 4210545755u},
                                    {0.4166666666666667, 4108168563u, 2445975459u},
                                    {0.425, 1127506671u, 590199620u},
                                    {0.48333333333333334, 909297321u, 3588134381u},
                                    {0.49166666666666664, 2464476153u, 3399831769u},
                                    {0.8, 296546533u, 2256749577u},
                                    {1.5, 4154685893u, 3752278477u},
                                    {2.5, 4154685893u, 4154685893u},
                                    {3.4, 4154685893u, 4154685893u},
                                    {5, 4154685893u, 4154685893u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::Disintegrate);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(5)));
        options->set("wind", eve::Value(double(1)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Disintegrate, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
}
TEST_CASE("stylize.pixelfx.spawn.disintegrateLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Disintegrate);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.disintegratePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Disintegrate), reference(PixelFxKind::Disintegrate);
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

TEST_CASE("stylize.pixelfx.spawn.disintegrateStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::Disintegrate);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.dissolveBurnPinnedIgnitionSeamCharTonguesSmokeAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1126186586u, 205962693u},
                                {0.008333333333333333, 2943240633u, 4130987047u},
                                {0.13333333333333333, 2041600806u, 3348058135u},
                                {0.14166666666666666, 1068895676u, 3799830257u},
                                {0.15, 379595007u, 4256445576u},
                                {0.15833333333333333, 2753496651u, 4262547538u},
                                {0.2, 104220363u, 1582996504u},
                                {0.3, 3968592146u, 4079539293u},
                                {0.4, 1545137302u, 4123944017u},
                                {0.5, 2686042688u, 3105470335u},
                                {0.6, 1915191583u, 4130350460u},
                                {0.8, 1513209711u, 1787898293u},
                                {1, 2165866038u, 1279525877u},
                                {1.2, 2962410060u, 883985727u},
                                {1.5, 1860374025u, 1571028033u},
                                {1.7, 2074738024u, 2693510257u},
                                {1.9, 2906641500u, 1008677831u},
                                {2, 2072420293u, 981740959u},
                                {2.05, 2177407348u, 1198202184u},
                                {2.2, 2037590201u, 3511326619u},
                                {2.5, 1274047109u, 2403973053u},
                                {3, 2716412500u, 1705097981u},
                                {3.591666666666667, 205962693u, 205962693u},
                                {3.6, 205962693u, 205962693u},
                                {4, 205962693u, 205962693u},
                                {5, 205962693u, 205962693u}};
    PixelFx         fx(PixelFxKind::DissolveBurn);
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
TEST_CASE("stylize.pixelfx.spawn.dissolveBurnAshActorBoundsAndVisibility") {
    PixelFx fx(PixelFxKind::DissolveBurn);
    auto    s = fx.settings();
    REQUIRE(s.actor.source == PixelFxActorSource::Dummy);
    REQUIRE(s.spawn.ash.size() == 3);
    s.spawn.ash = {0xff112233u};
    auto p      = PixelFx::prepareConfiguration(PixelFxKind::DissolveBurn, s);
    REQUIRE(p);
    s.spawn.ash[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.spawn.ash) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::DissolveBurn, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.ash.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DissolveBurn, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    auto bad      = owned;
    bad.spawn.ash = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DissolveBurn, bad));
    bad.spawn.ash.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::DissolveBurn, bad));
    bad              = owned;
    bad.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::DissolveBurn, bad));
    REQUIRE(fx.configurationValue() == value);
    for (double t : {0., .31, .32, .42, .49, 1.5, 4.}) {
        auto seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
    }
}
TEST_CASE("stylize.pixelfx.spawn.dissolveBurnPinnedHeroCustomFallbackEmptyAndInputOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 2687728880u, 205962693u},
                                    {0.008333333333333333, 3129209683u, 2668599015u},
                                    {0.14166666666666666, 2279453155u, 3799830257u},
                                    {0.15, 2517985630u, 2161049278u},
                                    {0.15833333333333333, 134838840u, 2716050885u},
                                    {0.3, 91654984u, 1923647741u},
                                    {0.5, 1714325278u, 7398056u},
                                    {0.8, 22448266u, 3111670981u},
                                    {1, 3978172637u, 3342253803u},
                                    {1.5, 1956304870u, 3028915115u},
                                    {2, 1537745022u, 3809268875u},
                                    {2.5, 146586304u, 3947313684u},
                                    {3.6, 205962693u, 205962693u},
                                    {5, 205962693u, 205962693u}};
        auto            actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::DissolveBurn);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(16)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DissolveBurn, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 1823652002u, 205962693u},
                                    {0.008333333333333333, 815298951u, 205962693u},
                                    {0.14166666666666666, 4198342359u, 3799830257u},
                                    {0.15, 957013783u, 2805425969u},
                                    {0.15833333333333333, 2130636244u, 2921798849u},
                                    {0.3, 1858705830u, 2665770582u},
                                    {0.5, 2426072305u, 1030465036u},
                                    {0.8, 1126631199u, 3467907423u},
                                    {1, 1274655767u, 2630822949u},
                                    {1.5, 1754759762u, 199107263u},
                                    {2, 3486495384u, 4261047627u},
                                    {2.5, 1472283828u, 3703529227u},
                                    {3.6, 205962693u, 205962693u},
                                    {5, 205962693u, 205962693u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::DissolveBurn);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(123)));
        options->set("ash", eve::Value::array({eve::Value(int64_t(4294967295u)), eve::Value(int64_t(4294901760u))}));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DissolveBurn, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 1969529986u, 205962693u},
                                    {0.008333333333333333, 4253591149u, 205962693u},
                                    {0.14166666666666666, 4086354557u, 3799830257u},
                                    {0.15, 774477597u, 2348694254u},
                                    {0.15833333333333333, 1688360021u, 2236689298u},
                                    {0.3, 1373938239u, 3720011926u},
                                    {0.5, 2454160311u, 4007305177u},
                                    {0.8, 4168346345u, 62086551u},
                                    {1, 928396680u, 1230842857u},
                                    {1.5, 4096015431u, 1777025199u},
                                    {2, 1067470624u, 719721661u},
                                    {2.5, 1622381216u, 2813970908u},
                                    {3.6, 205962693u, 205962693u},
                                    {5, 205962693u, 205962693u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::DissolveBurn);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(16)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DissolveBurn, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
    {
        constexpr Frame frames[] = {{0, 205962693u, 205962693u},
                                    {0.008333333333333333, 205962693u, 205962693u},
                                    {0.14166666666666666, 205962693u, 3799830257u},
                                    {0.15, 1140845470u, 1926108053u},
                                    {0.15833333333333333, 1305608766u, 2687345729u},
                                    {0.3, 341615644u, 3726078160u},
                                    {0.5, 626782307u, 1591926495u},
                                    {0.8, 908126797u, 2821485058u},
                                    {1, 3296298632u, 2826425384u},
                                    {1.5, 4134379198u, 1437154243u},
                                    {2, 3788036001u, 2443998917u},
                                    {2.5, 2593065095u, 4228651751u},
                                    {3.6, 205962693u, 205962693u},
                                    {5, 205962693u, 205962693u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::DissolveBurn);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(16)));
        auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DissolveBurn, value);
        REQUIRE(prepared);
        options->set("actor", eve::Value());
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
}
TEST_CASE("stylize.pixelfx.spawn.dissolveBurnLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::DissolveBurn);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.dissolveBurnPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::DissolveBurn), reference(PixelFxKind::DissolveBurn);
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

TEST_CASE("stylize.pixelfx.spawn.dissolveBurnStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::DissolveBurn);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.eggHatchPinnedWobblesCracksPopCupCrumbleAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 530752705u, 2066034117u},
                                {0.008333333333333333, 530752705u, 2066034117u},
                                {0.2916666666666667, 530752705u, 2066034117u},
                                {0.3, 530752705u, 2066034117u},
                                {0.3416666666666667, 4205722321u, 2066034117u},
                                {0.35, 3925438887u, 1243126806u},
                                {0.35833333333333334, 3571019321u, 1492173201u},
                                {0.8416666666666667, 2368616319u, 3998700498u},
                                {0.85, 2081293222u, 2468101866u},
                                {0.8916666666666667, 785569864u, 3986977758u},
                                {0.9, 1342073651u, 3508417126u},
                                {0.9083333333333333, 2273166843u, 2305468065u},
                                {1.3916666666666666, 3642286299u, 993019218u},
                                {1.4, 3874531725u, 2088630262u},
                                {1.4416666666666667, 672167874u, 433395582u},
                                {1.45, 1088250041u, 1904202425u},
                                {1.4583333333333333, 3442958092u, 2272441735u},
                                {1.8416666666666666, 2775355709u, 2008682291u},
                                {1.975, 2367271653u, 902603153u},
                                {1.9833333333333334, 2497096676u, 1783430435u},
                                {1.9916666666666667, 3307401875u, 443708956u},
                                {2.1, 3742587235u, 2905826495u},
                                {2.3, 4239913600u, 1126231322u},
                                {2.5416666666666665, 2422399120u, 4181674359u},
                                {2.55, 631107438u, 4242819997u},
                                {2.558333333333333, 3527215917u, 4144549282u},
                                {2.75, 556147655u, 878847211u},
                                {3, 3861204569u, 1650521909u},
                                {3.091666666666667, 2066034117u, 1806337901u},
                                {3.1, 2066034117u, 1293202909u},
                                {3.5, 2066034117u, 2066034117u},
                                {4, 2066034117u, 2066034117u},
                                {5, 2066034117u, 2066034117u}};
    PixelFx         fx(PixelFxKind::EggHatch);
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
TEST_CASE("stylize.pixelfx.spawn.eggHatchPinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1093755610u, 2066034117u},
                                    {0.008333333333333333, 1093755610u, 2066034117u},
                                    {0.35, 1425450102u, 2962781055u},
                                    {0.35833333333333334, 1376101383u, 131217429u},
                                    {0.9083333333333333, 1960095235u, 1370332145u},
                                    {1.4583333333333333, 3926695904u, 2296686699u},
                                    {1.975, 2222847970u, 4227619111u},
                                    {1.9833333333333334, 1893896713u, 3894209087u},
                                    {1.9916666666666667, 3599699144u, 2995839961u},
                                    {2.3, 3841692157u, 3928309570u},
                                    {2.55, 3290912126u, 696824232u},
                                    {2.558333333333333, 2683795353u, 4281715420u},
                                    {3.1, 2066034117u, 3109458517u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.speckle   = 4281558681u;
        s.spawn.glow      = {4294967295u, 4294901760u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 530752705u, 2066034117u},
                                    {0.008333333333333333, 530752705u, 2066034117u},
                                    {0.35, 1937900498u, 4266992703u},
                                    {0.35833333333333334, 2045467473u, 2096498860u},
                                    {0.9083333333333333, 1196847492u, 3075285416u},
                                    {1.4583333333333333, 2719487213u, 1444523437u},
                                    {1.975, 1973246680u, 4254355958u},
                                    {1.9833333333333334, 1196863687u, 706831006u},
                                    {1.9916666666666667, 1903948038u, 684850174u},
                                    {2.3, 976131882u, 715101805u},
                                    {2.55, 930279957u, 1311140215u},
                                    {2.558333333333333, 2354681015u, 3653429824u},
                                    {3.1, 2066034117u, 773568773u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.seed            = 0u;
        s.spawn.glow      = {4280951850u, 4282925642u, 4285556334u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 530752705u, 2066034117u},
                                    {0.008333333333333333, 530752705u, 2066034117u},
                                    {0.35, 26213826u, 697914742u},
                                    {0.35833333333333334, 1354959636u, 1592092570u},
                                    {0.9083333333333333, 3874573028u, 380175272u},
                                    {1.4583333333333333, 3529739384u, 1185848452u},
                                    {1.975, 2000743436u, 3204576565u},
                                    {1.9833333333333334, 3009215400u, 1913312769u},
                                    {1.9916666666666667, 2285413356u, 3211470895u},
                                    {2.3, 3860282769u, 2484189226u},
                                    {2.55, 3655675942u, 2569986127u},
                                    {2.558333333333333, 4120267529u, 575168031u},
                                    {3.1, 2066034117u, 773568773u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        s.spawn.glow      = {4278190080u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3689244713u, 2066034117u},
                                    {0.008333333333333333, 3689244713u, 2066034117u},
                                    {0.35, 2103763327u, 3358731137u},
                                    {0.35833333333333334, 35834299u, 2044932945u},
                                    {0.9083333333333333, 1274399924u, 848093334u},
                                    {1.4583333333333333, 1683912245u, 4205575523u},
                                    {1.975, 1930055770u, 3478524512u},
                                    {1.9833333333333334, 627970746u, 368123370u},
                                    {1.9916666666666667, 572226483u, 1112929594u},
                                    {2.3, 2542765668u, 3407481367u},
                                    {2.55, 3480450997u, 2719519054u},
                                    {2.558333333333333, 2442134043u, 412360080u},
                                    {3.1, 2066034117u, 762237533u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        s.spawn.glow      = {4294901760u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2827718980u, 2066034117u},
                                    {0.008333333333333333, 2827718980u, 2066034117u},
                                    {0.35, 972175082u, 2730454678u},
                                    {0.35833333333333334, 509743993u, 3557610044u},
                                    {0.9083333333333333, 2002372808u, 3020904279u},
                                    {1.4583333333333333, 1475498876u, 3427843028u},
                                    {1.975, 3467491369u, 3436659739u},
                                    {1.9833333333333334, 2657141603u, 3044483495u},
                                    {1.9916666666666667, 1720627214u, 3378630933u},
                                    {2.3, 4000759137u, 3335610427u},
                                    {2.55, 1658486954u, 2403292957u},
                                    {2.558333333333333, 3260110697u, 1928183999u},
                                    {3.1, 2066034117u, 773568773u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        s.spawn.glow = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.eggHatchGlowSpeckleOwnershipBoundsAndRoundtrip") {
    PixelFx fx(PixelFxKind::EggHatch);
    auto    s = fx.settings();
    REQUIRE(s.spawn.glow.size() == 3);
    REQUIRE(s.seed == 31);
    s.spawn.glow    = {0xff112233u};
    s.spawn.speckle = 0xff123456u;
    auto p          = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
    REQUIRE(p);
    s.spawn.glow[0] = 0xffabcdefu;
    s.spawn.speckle = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.spawn.glow) REQUIRE(c == 0xff112233u);
    REQUIRE(owned.spawn.speckle == 0xff123456u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::EggHatch, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.glow.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::EggHatch, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    auto bad       = owned;
    bad.spawn.glow = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::EggHatch, bad));
    bad.spawn.glow.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::EggHatch, bad));
    for (int64_t c : {int64_t(-2147483648LL), int64_t(-1), int64_t(0), int64_t(0x80112233u), int64_t(0xff000000u),
                      int64_t(0xffffffffu)}) {
        auto valid          = owned;
        valid.spawn.speckle = c;
        auto prepared       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, valid);
        REQUIRE(prepared);
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        REQUIRE(fx.settings().spawn.speckle == c);
    }
    auto restore = PixelFx::prepareConfigurationValue(PixelFxKind::EggHatch, value);
    REQUIRE(restore);
    REQUIRE(fx.commitConfiguration(std::move(restore).takeValue()));
    for (int64_t c : {int64_t(-2147483649LL), int64_t(4294967296LL)}) {
        auto  v       = value;
        auto* options = v.find("options");
        REQUIRE(options);
        options->set("speckle", eve::Value(c));
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::EggHatch, v));
        REQUIRE(fx.configurationValue() == value);
    }
}
TEST_CASE("stylize.pixelfx.spawn.eggHatchIndependentShellGeometryAcrossInstances") {
    PixelFx a(PixelFxKind::EggHatch), reference(PixelFxKind::EggHatch);
    for (double t : {0., .35, .9, 1.45, 1.98, 2.3, 2.55, 3.1, 4.}) {
        auto sa = a.prepareSeek(t), sr = reference.prepareSeek(t);
        REQUIRE(sa);
        REQUIRE(sr);
        REQUIRE(a.commitSeek(std::move(sa).takeValue()));
        REQUIRE(reference.commitSeek(std::move(sr).takeValue()));
        reference.draw();
        const std::vector<uint32_t> expected(reference.color().begin(), reference.color().end()),
            light(reference.light().begin(), reference.light().end());
        PixelFx other(PixelFxKind::EggHatch);
        auto    s       = other.settings();
        s.palette       = {0xff331122u};
        s.spawn.glow    = {0xffaaee11u};
        s.spawn.speckle = 0xff6633bbu;
        s.seed          = 123;
        auto config     = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(other.commitConfiguration(std::move(config).takeValue()));
        auto seek = other.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(other.commitSeek(std::move(seek).takeValue()));
        other.draw();
        a.draw();
        reference.draw();
        REQUIRE(std::equal(a.color().begin(), a.color().end(), expected.begin()));
        REQUIRE(std::equal(a.light().begin(), a.light().end(), light.begin()));
        REQUIRE(std::equal(reference.color().begin(), reference.color().end(), expected.begin()));
        REQUIRE(std::equal(reference.light().begin(), reference.light().end(), light.begin()));
    }
}
TEST_CASE("stylize.pixelfx.spawn.eggHatchLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::EggHatch);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.eggHatchPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::EggHatch), reference(PixelFxKind::EggHatch);
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

TEST_CASE("stylize.pixelfx.spawn.eggHatchStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::EggHatch);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.eggHatchSpeckleSourceNumberIdentity") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 684429832u, 2066034117u},
                                    {0.35, 3667476873u, 1243126806u},
                                    {0.35833333333333334, 1326815302u, 1492173201u},
                                    {0.9083333333333333, 1896097206u, 2305468065u},
                                    {1.4583333333333333, 3330739085u, 2272441735u},
                                    {1.975, 2250530312u, 902603153u},
                                    {1.9833333333333334, 72555891u, 1783430435u},
                                    {1.9916666666666667, 2760805583u, 443708956u},
                                    {2.3, 2528184276u, 1126231322u},
                                    {2.55, 3565209731u, 4242819997u},
                                    {2.558333333333333, 58404815u, 4144549282u},
                                    {3.1, 2066034117u, 1293202909u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.spawn.speckle   = 0LL;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3425412654u, 2066034117u},
                                    {0.35, 1441819442u, 1243126806u},
                                    {0.35833333333333334, 3466220240u, 1492173201u},
                                    {0.9083333333333333, 2028857854u, 2305468065u},
                                    {1.4583333333333333, 1297766576u, 2272441735u},
                                    {1.975, 1972019951u, 902603153u},
                                    {1.9833333333333334, 1954144071u, 1783430435u},
                                    {1.9916666666666667, 145778483u, 443708956u},
                                    {2.3, 2752419256u, 1126231322u},
                                    {2.55, 1026244853u, 4242819997u},
                                    {2.558333333333333, 2488770505u, 4144549282u},
                                    {3.1, 2066034117u, 1293202909u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.spawn.speckle   = 2148606515LL;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 14984087u, 2066034117u},
                                    {0.35, 3651023673u, 1243126806u},
                                    {0.35833333333333334, 2206306486u, 1492173201u},
                                    {0.9083333333333333, 425629670u, 2305468065u},
                                    {1.4583333333333333, 2744631329u, 2272441735u},
                                    {1.975, 3431003037u, 902603153u},
                                    {1.9833333333333334, 3184457511u, 1783430435u},
                                    {1.9916666666666667, 3124850827u, 443708956u},
                                    {2.3, 136712704u, 1126231322u},
                                    {2.55, 610474128u, 4242819997u},
                                    {2.558333333333333, 575264328u, 4144549282u},
                                    {3.1, 2066034117u, 1293202909u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.spawn.speckle   = 4294967295LL;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 14984087u, 2066034117u},
                                    {0.35, 9317485u, 1243126806u},
                                    {0.35833333333333334, 625253154u, 1492173201u},
                                    {0.9083333333333333, 242743559u, 2305468065u},
                                    {1.4583333333333333, 2154811333u, 2272441735u},
                                    {1.975, 2916504843u, 902603153u},
                                    {1.9833333333333334, 3184457511u, 1783430435u},
                                    {1.9916666666666667, 3124850827u, 443708956u},
                                    {2.3, 136712704u, 1126231322u},
                                    {2.55, 610474128u, 4242819997u},
                                    {2.558333333333333, 575264328u, 4144549282u},
                                    {3.1, 2066034117u, 1293202909u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.spawn.speckle   = -1LL;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1035643268u, 2066034117u},
                                    {0.35, 3977036239u, 1243126806u},
                                    {0.35833333333333334, 3629943043u, 1492173201u},
                                    {0.9083333333333333, 1969849876u, 2305468065u},
                                    {1.4583333333333333, 3934712925u, 2272441735u},
                                    {1.975, 3223233368u, 902603153u},
                                    {1.9833333333333334, 620741819u, 1783430435u},
                                    {1.9916666666666667, 156039823u, 443708956u},
                                    {2.3, 395877812u, 1126231322u},
                                    {2.55, 3328266295u, 4242819997u},
                                    {2.558333333333333, 1447026411u, 4144549282u},
                                    {3.1, 2066034117u, 1293202909u},
                                    {5, 2066034117u, 2066034117u}};
        PixelFx         fx(PixelFxKind::EggHatch);
        auto            s = fx.settings();
        s.spawn.speckle   = 4293982975LL;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::EggHatch, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.poofPinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2497395033u, 2062859693u},
                                {0.008333333333333333, 3021882053u, 547551293u},
                                {0.016666666666666666, 254338596u, 809311732u},
                                {0.025, 3343657712u, 207153533u},
                                {0.03333333333333333, 1473522038u, 580720070u},
                                {0.06666666666666667, 4181216742u, 1493315726u},
                                {0.075, 2648395239u, 580205427u},
                                {0.1, 740824943u, 1266057464u},
                                {0.2, 1572127728u, 2777439414u},
                                {0.3, 1546296947u, 1606749691u},
                                {0.4, 2081751888u, 1004506742u},
                                {0.5, 303333485u, 2788220656u},
                                {0.6, 3861689711u, 4187660932u},
                                {0.8, 1730517129u, 490589293u},
                                {1, 2886880356u, 3063549330u},
                                {1.2, 506226687u, 2413340229u},
                                {1.3, 2522607265u, 2935953949u},
                                {1.4, 1658999237u, 4052582853u},
                                {1.6, 1658999237u, 1658999237u},
                                {2, 1658999237u, 1658999237u},
                                {3, 1658999237u, 1658999237u},
                                {4, 1658999237u, 1658999237u},
                                {5, 1658999237u, 1658999237u}};
    PixelFx         fx(PixelFxKind::Poof);
    REQUIRE(fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        if (checksum(fx.color()) != f.color)
            test_pixelfx_diagnostic::dumpMismatch("spawn/poof", f.time, fx.color(), fx.light());
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.spawn.poofPinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {
            {0, 3419944182u, 3456787973u},   {0.025, 3386508182u, 1573041828u}, {0.075, 2621838645u, 1221318396u},
            {0.3, 187601502u, 1823504576u},  {0.6, 655373327u, 836475742u},     {1.2, 2545064317u, 46907318u},
            {1.6, 1658999237u, 1658999237u}, {4, 1658999237u, 1658999237u},     {5, 1658999237u, 1658999237u}};
        PixelFx fx(PixelFxKind::Poof);
        auto    s     = fx.settings();
        s.seed        = 123u;
        s.palette     = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.stars = {4294967295u, 4294901760u};
        auto config   = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2497395033u, 2062859693u},   {0.025, 2938237320u, 3256468032u}, {0.075, 3181561164u, 1856306854u},
            {0.3, 2177188189u, 3104119879u}, {0.6, 3970451027u, 2944191572u},   {1.2, 3855704547u, 3125787275u},
            {1.6, 1658999237u, 1658999237u}, {4, 1658999237u, 1658999237u},     {5, 1658999237u, 1658999237u}};
        PixelFx fx(PixelFxKind::Poof);
        auto    s     = fx.settings();
        s.seed        = 0u;
        s.spawn.stars = {4280951850u};
        auto config   = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2497395033u, 2062859693u},   {0.025, 3298421344u, 3589046802u}, {0.075, 3029955131u, 892013267u},
            {0.3, 3128361620u, 2880198131u}, {0.6, 4230258486u, 1157380227u},   {1.2, 1658999237u, 3632235703u},
            {1.6, 1658999237u, 1658999237u}, {4, 1658999237u, 1658999237u},     {5, 1658999237u, 1658999237u}};
        PixelFx fx(PixelFxKind::Poof);
        auto    s     = fx.settings();
        s.seed        = 4294967295u;
        s.spawn.stars = {4294967295u};
        auto config   = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 2512701754u, 3980377237u},   {0.025, 3942672326u, 1642472796u}, {0.075, 3825715000u, 861373185u},
            {0.3, 1968203716u, 4022087372u}, {0.6, 2288372344u, 2995940206u},   {1.2, 3463992368u, 2413340229u},
            {1.6, 1658999237u, 1658999237u}, {4, 1658999237u, 1658999237u},     {5, 1658999237u, 1658999237u}};
        PixelFx fx(PixelFxKind::Poof);
        auto    s     = fx.settings();
        s.palette     = {4281862768u};
        s.spawn.stars = {4294901760u};
        auto config   = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {
            {0, 1168639321u, 2423469325u},   {0.025, 3335576462u, 2136591167u}, {0.075, 110851917u, 1656770559u},
            {0.3, 1143024711u, 355825977u},  {0.6, 3299745333u, 3138541521u},   {1.2, 1735999169u, 2413340229u},
            {1.6, 1658999237u, 1658999237u}, {4, 1658999237u, 1658999237u},     {5, 1658999237u, 1658999237u}};
        PixelFx fx(PixelFxKind::Poof);
        auto    s = fx.settings();
        s.palette = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        s.spawn.stars = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.poofLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Poof);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.poofPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Poof), reference(PixelFxKind::Poof);
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

TEST_CASE("stylize.pixelfx.spawn.poofStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::Poof);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.respawnBeamPinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                {0.008333333333333333, 3541077542u, 1037466988u},
                                {0.2, 212624310u, 3770840173u},
                                {0.24166666666666667, 3745485475u, 1418115500u},
                                {0.25, 662712017u, 2160532468u},
                                {0.25833333333333336, 75299326u, 2957806967u},
                                {0.31666666666666665, 535584178u, 1251898470u},
                                {0.325, 4057260800u, 15161011u},
                                {0.3333333333333333, 744700938u, 3965182308u},
                                {0.39166666666666666, 735540710u, 339355909u},
                                {0.4, 1483058312u, 1331507475u},
                                {0.4083333333333333, 3450522100u, 133661549u},
                                {0.5, 2905909173u, 3651115564u},
                                {0.8, 1279888099u, 3870596352u},
                                {1, 1386911697u, 3898404064u},
                                {1.2, 257454593u, 1320377254u},
                                {1.3416666666666666, 3693608130u, 84600131u},
                                {1.35, 2191453758u, 941537541u},
                                {1.3583333333333334, 2534761899u, 709826814u},
                                {1.3833333333333333, 1368434228u, 336814509u},
                                {1.3916666666666666, 395860549u, 3603566655u},
                                {1.4, 4226132581u, 1004904046u},
                                {1.4666666666666666, 3538285483u, 245649157u},
                                {1.475, 3759271784u, 1928958007u},
                                {1.4833333333333334, 2185396114u, 2852402475u},
                                {1.6, 3114976939u, 1423142321u},
                                {1.8, 1078048220u, 1920962424u},
                                {2, 2712855263u, 1262364453u},
                                {2.5, 1792380357u, 1792380357u},
                                {3, 1792380357u, 1792380357u},
                                {5, 1792380357u, 1792380357u}};
    PixelFx         fx(PixelFxKind::RespawnBeam);
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
TEST_CASE("stylize.pixelfx.spawn.respawnBeamPinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 1479178763u, 1574231939u},
                                    {0.31666666666666665, 1467745743u, 865773995u},
                                    {0.39166666666666666, 3750908146u, 3559549410u},
                                    {0.5, 3534380570u, 2968640436u},
                                    {1.2, 3980330678u, 1210208048u},
                                    {1.3583333333333334, 3185783488u, 2270608058u},
                                    {1.4, 17610670u, 633905694u},
                                    {1.4833333333333334, 3626983553u, 3592530365u},
                                    {2, 823650508u, 3008053888u},
                                    {5, 1792380357u, 1792380357u}};
        PixelFx         fx(PixelFxKind::RespawnBeam);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.width     = 23.5;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 4277283902u, 3821923459u},
                                    {0.31666666666666665, 1710246335u, 846166877u},
                                    {0.39166666666666666, 1812048021u, 2659223623u},
                                    {0.5, 3974225618u, 53596393u},
                                    {1.2, 4289233350u, 1476929239u},
                                    {1.3583333333333334, 3813946385u, 2706777789u},
                                    {1.4, 2769742279u, 4057421661u},
                                    {1.4833333333333334, 4003716535u, 1140374319u},
                                    {2, 2491236609u, 534044415u},
                                    {5, 1792380357u, 1792380357u}};
        PixelFx         fx(PixelFxKind::RespawnBeam);
        auto            s = fx.settings();
        s.seed            = 0u;
        s.spawn.width     = 0u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 1724483259u, 504929140u},
                                    {0.31666666666666665, 3367823953u, 3101779823u},
                                    {0.39166666666666666, 1161979416u, 1991667683u},
                                    {0.5, 771142099u, 3108804947u},
                                    {1.2, 1647359614u, 738078091u},
                                    {1.3583333333333334, 540689791u, 532423311u},
                                    {1.4, 3058521564u, 1374719500u},
                                    {1.4833333333333334, 2303663187u, 2262353253u},
                                    {2, 1912985993u, 3262973920u},
                                    {5, 1792380357u, 1792380357u}};
        PixelFx         fx(PixelFxKind::RespawnBeam);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        s.spawn.width     = 256u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 676747769u, 3006497545u},
                                    {0.31666666666666665, 2749633230u, 3739202506u},
                                    {0.39166666666666666, 1192125660u, 1714516878u},
                                    {0.5, 1972538242u, 2032289575u},
                                    {1.2, 4041010184u, 1054494900u},
                                    {1.3583333333333334, 3933699299u, 1879798371u},
                                    {1.4, 4173378182u, 3702698616u},
                                    {1.4833333333333334, 3063506680u, 4193027992u},
                                    {2, 874705182u, 585709692u},
                                    {5, 1792380357u, 1792380357u}};
        PixelFx         fx(PixelFxKind::RespawnBeam);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 3861157466u, 488275751u},
                                    {0.31666666666666665, 2657041771u, 683525513u},
                                    {0.39166666666666666, 3390436366u, 989721078u},
                                    {0.5, 2481942480u, 4128712430u},
                                    {1.2, 1908869768u, 3315131918u},
                                    {1.3583333333333334, 1837585985u, 2189337084u},
                                    {1.4, 2678760076u, 3531677362u},
                                    {1.4833333333333334, 4073079291u, 33971558u},
                                    {2, 3521745394u, 2657668030u},
                                    {5, 1792380357u, 1792380357u}};
        PixelFx         fx(PixelFxKind::RespawnBeam);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.respawnBeamLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::RespawnBeam);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.respawnBeamPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::RespawnBeam), reference(PixelFxKind::RespawnBeam);
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

TEST_CASE("stylize.pixelfx.spawn.respawnBeamStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::RespawnBeam);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.respawnBeamPinnedActorSnapshotsAndOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 3745485475u, 1418115500u},
                                    {0.31666666666666665, 535584178u, 1251898470u},
                                    {0.39166666666666666, 735540710u, 339355909u},
                                    {0.5, 3414794012u, 1131393827u},
                                    {1.2, 2835592120u, 1494868113u},
                                    {1.3583333333333334, 4294791783u, 3643646668u},
                                    {1.4, 1974072700u, 2007035546u},
                                    {1.4833333333333334, 3587410092u, 1433604385u},
                                    {2, 2138162911u, 3396713376u},
                                    {5, 1792380357u, 1792380357u}};
        auto            actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::RespawnBeam);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 3784990404u, 2166283936u},
                                    {0.31666666666666665, 1461065u, 780466948u},
                                    {0.39166666666666666, 3232200344u, 2676866384u},
                                    {0.5, 3650644621u, 4157595942u},
                                    {1.2, 4252726013u, 1071512706u},
                                    {1.3583333333333334, 3694153614u, 921927072u},
                                    {1.4, 3268905366u, 3684213676u},
                                    {1.4833333333333334, 2498935417u, 3060373180u},
                                    {2, 441163878u, 406095743u},
                                    {5, 1792380357u, 1792380357u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::RespawnBeam);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(123)));
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 3745485475u, 1418115500u},
                                    {0.31666666666666665, 535584178u, 1251898470u},
                                    {0.39166666666666666, 735540710u, 339355909u},
                                    {0.5, 2149928542u, 3457419526u},
                                    {1.2, 4210055742u, 272262631u},
                                    {1.3583333333333334, 2216293183u, 475750684u},
                                    {1.4, 1745761336u, 4065114263u},
                                    {1.4833333333333334, 2524840820u, 11937622u},
                                    {2, 1769597407u, 3879679858u},
                                    {5, 1792380357u, 1792380357u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::RespawnBeam);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1792380357u, 1792380357u},
                                    {0.24166666666666667, 3745485475u, 1418115500u},
                                    {0.31666666666666665, 535584178u, 1251898470u},
                                    {0.39166666666666666, 735540710u, 339355909u},
                                    {0.5, 2149928542u, 894290124u},
                                    {1.2, 303247461u, 3620432592u},
                                    {1.3583333333333334, 28119429u, 2646966839u},
                                    {1.4, 826462508u, 1581223389u},
                                    {1.4833333333333334, 2281044286u, 1847705127u},
                                    {2, 312711327u, 4226148310u},
                                    {5, 1792380357u, 1792380357u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::RespawnBeam);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.shatterDeathPinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1039239061u, 3400205765u},
                                {0.008333333333333333, 3717220963u, 3766406572u},
                                {0.1, 160279529u, 2561339465u},
                                {0.2, 2639789601u, 1874117919u},
                                {0.4, 3452395135u, 404585802u},
                                {0.49166666666666664, 968794761u, 4122059976u},
                                {0.5, 1349466434u, 4245416925u},
                                {0.5083333333333333, 759836253u, 2562636602u},
                                {0.5416666666666666, 1862476564u, 404773733u},
                                {0.55, 3181985929u, 3083541038u},
                                {0.5583333333333333, 2280737546u, 2522284355u},
                                {0.8, 3311145376u, 399937056u},
                                {1.0416666666666667, 604916096u, 421707333u},
                                {1.05, 3328135334u, 2465680751u},
                                {1.0583333333333333, 1869091041u, 977935875u},
                                {1.2416666666666667, 2210292488u, 3338455127u},
                                {1.25, 2392009987u, 1602708827u},
                                {1.3916666666666666, 163857077u, 3639567531u},
                                {1.4, 239604254u, 321824257u},
                                {1.4083333333333334, 3925902928u, 2397277404u},
                                {1.4666666666666666, 2548528364u, 1434321338u},
                                {1.475, 498562484u, 1964701531u},
                                {1.4833333333333334, 2505905517u, 3859226137u},
                                {1.7, 3686732739u, 2168987310u},
                                {2, 3496385857u, 3653783513u},
                                {2.4, 1836495340u, 1789458540u},
                                {2.8, 4035028444u, 2123324589u},
                                {3.3, 3400205765u, 3400205765u},
                                {4, 3400205765u, 3400205765u},
                                {5, 3400205765u, 3400205765u}};
    PixelFx         fx(PixelFxKind::ShatterDeath);
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
TEST_CASE("stylize.pixelfx.spawn.shatterDeathPinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 965438565u, 3400205765u},
                                    {0.2, 2644254984u, 2902221731u},
                                    {0.5, 1610005606u, 356130267u},
                                    {0.55, 49998838u, 1613724152u},
                                    {1.0416666666666667, 1416950319u, 3369792667u},
                                    {1.2416666666666667, 2592803483u, 3665759811u},
                                    {1.4, 3771051480u, 1189498321u},
                                    {1.475, 1831575186u, 145957626u},
                                    {2, 3596054315u, 1958832995u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        PixelFx         fx(PixelFxKind::ShatterDeath);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1039239061u, 3400205765u},
                                    {0.2, 2724566304u, 987356454u},
                                    {0.5, 2472461479u, 1562013203u},
                                    {0.55, 1088940133u, 96899340u},
                                    {1.0416666666666667, 4254469460u, 1989984095u},
                                    {1.2416666666666667, 2979551125u, 785988610u},
                                    {1.4, 1041518569u, 2921834829u},
                                    {1.475, 3959009438u, 2086980722u},
                                    {2, 2924373449u, 3838121840u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        PixelFx         fx(PixelFxKind::ShatterDeath);
        auto            s = fx.settings();
        s.seed            = 0u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1039239061u, 3400205765u},
                                    {0.2, 3226407444u, 2049760267u},
                                    {0.5, 2869961355u, 3285543646u},
                                    {0.55, 3086890061u, 61954682u},
                                    {1.0416666666666667, 720672471u, 3158175389u},
                                    {1.2416666666666667, 2124478470u, 1672223245u},
                                    {1.4, 1483243168u, 2921834829u},
                                    {1.475, 2597653353u, 736499885u},
                                    {2, 2443796830u, 3494691928u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        PixelFx         fx(PixelFxKind::ShatterDeath);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2489176901u, 3400205765u},
                                    {0.2, 3581153732u, 3004595289u},
                                    {0.5, 2360463993u, 1111468206u},
                                    {0.55, 1219467876u, 14531601u},
                                    {1.0416666666666667, 4119029303u, 283095974u},
                                    {1.2416666666666667, 270454674u, 3514078861u},
                                    {1.4, 26196862u, 2826630315u},
                                    {1.475, 3831421244u, 1990169745u},
                                    {2, 1113375792u, 1971371162u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        PixelFx         fx(PixelFxKind::ShatterDeath);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 956026661u, 3400205765u},
                                    {0.2, 1994745679u, 2307618245u},
                                    {0.5, 3937398755u, 413908762u},
                                    {0.55, 1728720832u, 3546739526u},
                                    {1.0416666666666667, 1556388074u, 1817948847u},
                                    {1.2416666666666667, 2184264126u, 1018982137u},
                                    {1.4, 3168961467u, 818799057u},
                                    {1.475, 56557166u, 1575295451u},
                                    {2, 1821152944u, 1653297263u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        PixelFx         fx(PixelFxKind::ShatterDeath);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.shatterDeathLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::ShatterDeath);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.shatterDeathPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ShatterDeath), reference(PixelFxKind::ShatterDeath);
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

TEST_CASE("stylize.pixelfx.spawn.shatterDeathStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::ShatterDeath);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.shatterDeathPinnedActorSnapshotsAndOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1160882458u, 3400205765u},
                                    {0.2, 358181240u, 1438283699u},
                                    {0.5, 2385186078u, 1991916249u},
                                    {0.55, 1266958363u, 3524654187u},
                                    {1.0416666666666667, 1090851919u, 3266922509u},
                                    {1.2416666666666667, 367459519u, 3901393362u},
                                    {1.4, 1109902770u, 1735927012u},
                                    {1.475, 912571076u, 680007580u},
                                    {2, 2206508202u, 2893660157u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        auto            actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::ShatterDeath);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 340381981u, 3400205765u},
                                    {0.2, 2597954499u, 4243765485u},
                                    {0.5, 3985628316u, 3580056961u},
                                    {0.55, 2280380852u, 448830914u},
                                    {1.0416666666666667, 1307396708u, 1765716682u},
                                    {1.2416666666666667, 4699526u, 801586771u},
                                    {1.4, 1027664118u, 4168507822u},
                                    {1.475, 1119462411u, 2673030355u},
                                    {2, 3653257739u, 1376804463u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::ShatterDeath);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(123)));
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2782205701u, 3400205765u},
                                    {0.2, 2999371284u, 2554167171u},
                                    {0.5, 3181377389u, 1330291948u},
                                    {0.55, 2075877077u, 1268711826u},
                                    {1.0416666666666667, 1487777010u, 367435863u},
                                    {1.2416666666666667, 213415720u, 212119417u},
                                    {1.4, 1579731037u, 3835901644u},
                                    {1.475, 2294438252u, 2810235623u},
                                    {2, 2434548550u, 2908246779u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::ShatterDeath);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1054329565u, 3400205765u},
                                    {0.2, 477534601u, 2497496450u},
                                    {0.5, 833319736u, 2755221059u},
                                    {0.55, 1360970416u, 2677502243u},
                                    {1.0416666666666667, 2436549792u, 1238521632u},
                                    {1.2416666666666667, 3583937778u, 477845544u},
                                    {1.4, 467185416u, 2921834829u},
                                    {1.475, 3905350278u, 1561033653u},
                                    {2, 3283356125u, 2567012477u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::ShatterDeath);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3400205765u, 3400205765u},
                                    {0.2, 942807430u, 1248754817u},
                                    {0.5, 1376452691u, 85670530u},
                                    {0.55, 3260109366u, 1583924438u},
                                    {1.0416666666666667, 156147970u, 3166961053u},
                                    {1.2416666666666667, 2482253264u, 3851471347u},
                                    {1.4, 3594817247u, 510761605u},
                                    {1.475, 4153489072u, 104641927u},
                                    {2, 3879019052u, 2184881303u},
                                    {3.3, 3400205765u, 3400205765u},
                                    {5, 3400205765u, 3400205765u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::ShatterDeath);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.soulRisePinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 3166643333u, 1261520325u},
                                {0.008333333333333333, 3166643333u, 882109677u},
                                {0.2, 201642069u, 2848109817u},
                                {0.3416666666666667, 1868164889u, 3721468341u},
                                {0.35, 793119199u, 1159598319u},
                                {0.35833333333333334, 2694111164u, 1508509864u},
                                {0.4083333333333333, 3436276094u, 4285708327u},
                                {0.4166666666666667, 4132934711u, 3703483453u},
                                {0.5, 885721390u, 2316835974u},
                                {0.6, 3267409574u, 1018847615u},
                                {0.8, 3558441180u, 68195158u},
                                {1, 1127433055u, 2751064914u},
                                {1.2, 1788813371u, 2463822787u},
                                {1.5, 475359898u, 3616404807u},
                                {1.7916666666666667, 2140469272u, 3281625439u},
                                {1.8, 3202839772u, 1449909874u},
                                {1.8083333333333333, 2837344218u, 1377166269u},
                                {2, 387530891u, 2693905494u},
                                {2.0416666666666665, 1861181278u, 746315187u},
                                {2.05, 2731914337u, 4043310650u},
                                {2.058333333333333, 4263364566u, 745862096u},
                                {2.5, 2847356878u, 3459493823u},
                                {2.5416666666666665, 3459910586u, 536793554u},
                                {2.55, 3060049890u, 2653183460u},
                                {2.558333333333333, 604267088u, 4031710202u},
                                {2.65, 1915990888u, 2624713526u},
                                {3, 762347441u, 1589865343u},
                                {3.1, 1261520325u, 1261520325u},
                                {3.2, 1261520325u, 1261520325u},
                                {4, 1261520325u, 1261520325u},
                                {5, 1261520325u, 1261520325u}};
    PixelFx         fx(PixelFxKind::SoulRise);
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
TEST_CASE("stylize.pixelfx.spawn.soulRisePinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 696468469u, 1261520325u},
                                    {0.3416666666666667, 1868164889u, 2285923017u},
                                    {0.4083333333333333, 3627719175u, 2215685398u},
                                    {0.6, 1692912889u, 1548588733u},
                                    {1.2, 2813332788u, 836014085u},
                                    {1.8, 2962423554u, 2408193525u},
                                    {2.0416666666666665, 3655799935u, 2844419453u},
                                    {2.5, 2180780399u, 93669105u},
                                    {2.558333333333333, 2080441602u, 1410487073u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        PixelFx         fx(PixelFxKind::SoulRise);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.height    = 72.5;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3166643333u, 1261520325u},
                                    {0.3416666666666667, 1868164889u, 3721468341u},
                                    {0.4083333333333333, 425364727u, 3304869598u},
                                    {0.6, 2052404383u, 3572002610u},
                                    {1.2, 33892580u, 1833476805u},
                                    {1.8, 3886859207u, 3224061566u},
                                    {2.0416666666666665, 2132041076u, 3033088773u},
                                    {2.5, 322556522u, 1918786874u},
                                    {2.558333333333333, 3158876284u, 1458162639u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        PixelFx         fx(PixelFxKind::SoulRise);
        auto            s = fx.settings();
        s.seed            = 0u;
        s.spawn.height    = 0u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3166643333u, 1261520325u},
                                    {0.3416666666666667, 1868164889u, 3721468341u},
                                    {0.4083333333333333, 1161935665u, 1187938451u},
                                    {0.6, 804738812u, 2405722807u},
                                    {1.2, 3909768703u, 1663570918u},
                                    {1.8, 2330139117u, 2423614807u},
                                    {2.0416666666666665, 2330139117u, 1261520325u},
                                    {2.5, 2330139117u, 1261520325u},
                                    {2.558333333333333, 2330139117u, 1261520325u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        PixelFx         fx(PixelFxKind::SoulRise);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        s.spawn.height    = 256u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 4088966229u, 1261520325u},
                                    {0.3416666666666667, 1868164889u, 3182954813u},
                                    {0.4083333333333333, 3909218687u, 3541898038u},
                                    {0.6, 4095514213u, 4212216008u},
                                    {1.2, 2082195649u, 4212976653u},
                                    {1.8, 2650665302u, 452183488u},
                                    {2.0416666666666665, 951336828u, 2473098270u},
                                    {2.5, 1208730235u, 910926191u},
                                    {2.558333333333333, 2924520434u, 3883374713u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        PixelFx         fx(PixelFxKind::SoulRise);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3646969333u, 1261520325u},
                                    {0.3416666666666667, 1868164889u, 2511647437u},
                                    {0.4083333333333333, 3346041633u, 3227757285u},
                                    {0.6, 577173503u, 518492317u},
                                    {1.2, 3610191775u, 1701764999u},
                                    {1.8, 3338583618u, 1923353908u},
                                    {2.0416666666666665, 4003695527u, 383535602u},
                                    {2.5, 1677065709u, 877795998u},
                                    {2.558333333333333, 2924239605u, 2344021109u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        PixelFx         fx(PixelFxKind::SoulRise);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.soulRiseLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::SoulRise);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.soulRisePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SoulRise), reference(PixelFxKind::SoulRise);
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

TEST_CASE("stylize.pixelfx.spawn.soulRiseStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::SoulRise);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.soulRisePinnedActorSnapshotsAndOwnership") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 4001415877u, 1261520325u},
                                    {0.3416666666666667, 3205659053u, 3721468341u},
                                    {0.4083333333333333, 2991250653u, 3709799605u},
                                    {0.6, 4019070279u, 617957595u},
                                    {1.2, 2052774747u, 2461850908u},
                                    {1.8, 1976300061u, 2428223038u},
                                    {2.0416666666666665, 3360907343u, 3991793749u},
                                    {2.5, 3865673494u, 2742052048u},
                                    {2.558333333333333, 2540225618u, 2469701522u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        auto            actor =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::SoulRise);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 233684245u, 1261520325u},
                                    {0.3416666666666667, 4272275393u, 3721468341u},
                                    {0.4083333333333333, 4173051612u, 3874209549u},
                                    {0.6, 3725045517u, 4160980612u},
                                    {1.2, 205934047u, 648950772u},
                                    {1.8, 3561316785u, 3527850517u},
                                    {2.0416666666666665, 1640707882u, 725936140u},
                                    {2.5, 1594052937u, 344980678u},
                                    {2.558333333333333, 4261917013u, 3989514731u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::SoulRise);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        options->set("seed", eve::Value(int64_t(123)));
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3128487133u, 1261520325u},
                                    {0.3416666666666667, 1044898345u, 3721468341u},
                                    {0.4083333333333333, 3005586070u, 4096422803u},
                                    {0.6, 1866660024u, 2375886192u},
                                    {1.2, 2681454127u, 3073825572u},
                                    {1.8, 3276172324u, 2535046673u},
                                    {2.0416666666666665, 2130713238u, 2561202325u},
                                    {2.5, 3689333939u, 254923797u},
                                    {2.558333333333333, 2455375228u, 1407762723u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 5, "height": 4, "scale": 1, "x": -2.25, "y": -4.5, "head": [0, -4], "chest": [0, -2], "eyes": [], "mask": [1, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1], "pixels": [0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867, 0, 4279383126, 2148606515, 1122867]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::SoulRise);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 88122517u, 1261520325u},
                                    {0.3416666666666667, 2423877429u, 3721468341u},
                                    {0.4083333333333333, 3513921743u, 2601259496u},
                                    {0.6, 958955949u, 4005981071u},
                                    {1.2, 1389055719u, 2390026866u},
                                    {1.8, 2775767884u, 3464244203u},
                                    {2.0416666666666665, 444013260u, 4124843244u},
                                    {2.5, 2730570024u, 189940497u},
                                    {2.558333333333333, 1105398526u, 1471377837u},
                                    {3.1, 1261520325u, 1261520325u},
                                    {5, 1261520325u, 1261520325u}};
        auto            actor    = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 1, "height": 1, "scale": 1, "x": 0, "y": -1, "head": [0, -1], "chest": [0, -0.5], "eyes": [], "mask": [1], "pixels": [0]})actor");
        REQUIRE(actor);
        PixelFx fx(PixelFxKind::SoulRise);
        auto    value   = fx.configurationValue();
        auto*   options = value.find("options");
        REQUIRE(options);
        options->set("actor", actor.value());
        auto config = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, value);
        REQUIRE(config);
        options->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.spawnPortalPinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2598485750u, 458893493u},
                                {0.008333333333333333, 1957135582u, 2017355215u},
                                {0.1, 3461520774u, 366418180u},
                                {0.10833333333333334, 2061208665u, 3917960804u},
                                {0.175, 495870180u, 1157620375u},
                                {0.18333333333333332, 4187130623u, 445422522u},
                                {0.20833333333333334, 3139078182u, 1233166468u},
                                {0.21666666666666667, 546322343u, 1780938978u},
                                {0.2916666666666667, 585832938u, 3864635378u},
                                {0.3, 1827375008u, 785182447u},
                                {0.30833333333333335, 3188649855u, 2665422278u},
                                {0.5166666666666667, 1854344898u, 1322243364u},
                                {0.525, 630525667u, 1826338902u},
                                {0.7583333333333333, 975045911u, 379885020u},
                                {0.7666666666666667, 3257116825u, 3844851908u},
                                {0.9916666666666667, 2442617112u, 2645816375u},
                                {1, 1758471402u, 1785721226u},
                                {1.0083333333333333, 739877926u, 2308363310u},
                                {1.2, 1973166961u, 3571004827u},
                                {1.4, 3108049391u, 782606127u},
                                {1.7, 905221368u, 4154231366u},
                                {1.8916666666666666, 1073927618u, 2617853279u},
                                {1.9, 3386621555u, 4017575708u},
                                {1.9083333333333334, 3110898756u, 552997798u},
                                {2, 3986307473u, 991742351u},
                                {2.216666666666667, 810990242u, 3037918173u},
                                {2.225, 2191899560u, 657758467u},
                                {2.2333333333333334, 3633650017u, 3487907040u},
                                {2.5, 2349112742u, 3719302362u},
                                {3, 2026926670u, 3331000929u},
                                {3.8, 1245914053u, 1245914053u},
                                {4, 1245914053u, 1245914053u},
                                {5, 1245914053u, 1245914053u}};
    PixelFx         fx(PixelFxKind::SpawnPortal);
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
TEST_CASE("stylize.pixelfx.spawn.spawnPortalPinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3215062984u, 449675117u},
                                    {0.10833333333333334, 1913774570u, 3695209515u},
                                    {0.20833333333333334, 1953573787u, 65797045u},
                                    {0.3, 217684946u, 1355609838u},
                                    {0.525, 156992028u, 2246319380u},
                                    {0.9916666666666667, 1767201774u, 2621857232u},
                                    {1.2, 3348982644u, 1903047660u},
                                    {1.8916666666666666, 2647040033u, 2688795660u},
                                    {2, 4207742386u, 340961514u},
                                    {2.2333333333333334, 1846909154u, 2574779528u},
                                    {3.8, 1245914053u, 1245914053u},
                                    {5, 1245914053u, 1245914053u}};
        PixelFx         fx(PixelFxKind::SpawnPortal);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.width     = 45.5;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2598485750u, 3193882549u},
                                    {0.10833333333333334, 670613039u, 2340739547u},
                                    {0.20833333333333334, 3874913695u, 1175500065u},
                                    {0.3, 3240771187u, 366763028u},
                                    {0.525, 530295452u, 1106628401u},
                                    {0.9916666666666667, 2682813426u, 2621685425u},
                                    {1.2, 3597083468u, 2680382508u},
                                    {1.8916666666666666, 1402863679u, 1529530079u},
                                    {2, 1500408804u, 508024320u},
                                    {2.2333333333333334, 2201051410u, 896133103u},
                                    {3.8, 1245914053u, 1245914053u},
                                    {5, 1245914053u, 1245914053u}};
        PixelFx         fx(PixelFxKind::SpawnPortal);
        auto            s = fx.settings();
        s.seed            = 0u;
        s.spawn.width     = 0u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2598485750u, 458893493u},
                                    {0.10833333333333334, 2508117599u, 3843779341u},
                                    {0.20833333333333334, 1183245066u, 2080664090u},
                                    {0.3, 2297877759u, 645022173u},
                                    {0.525, 4147021042u, 3306218692u},
                                    {0.9916666666666667, 1320300129u, 1188222022u},
                                    {1.2, 4184806733u, 3124984586u},
                                    {1.8916666666666666, 1054504459u, 145716774u},
                                    {2, 3215630905u, 534589011u},
                                    {2.2333333333333334, 2024999344u, 568186181u},
                                    {3.8, 1245914053u, 1245914053u},
                                    {5, 1245914053u, 1245914053u}};
        PixelFx         fx(PixelFxKind::SpawnPortal);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        s.spawn.width     = 256u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 411840238u, 2977237117u},
                                    {0.10833333333333334, 1827603318u, 2447332402u},
                                    {0.20833333333333334, 2012599671u, 159663910u},
                                    {0.3, 2012553162u, 2268853594u},
                                    {0.525, 2583650095u, 670408269u},
                                    {0.9916666666666667, 3479582958u, 122031703u},
                                    {1.2, 2984509927u, 2999308005u},
                                    {1.8916666666666666, 1551988248u, 1349624540u},
                                    {2, 1148927362u, 4018300654u},
                                    {2.2333333333333334, 3895530191u, 1675016777u},
                                    {3.8, 1245914053u, 1245914053u},
                                    {5, 1245914053u, 1245914053u}};
        PixelFx         fx(PixelFxKind::SpawnPortal);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1168542188u, 3870522901u},
                                    {0.10833333333333334, 906263047u, 1186672309u},
                                    {0.20833333333333334, 2132947664u, 1533378394u},
                                    {0.3, 2052314053u, 465078261u},
                                    {0.525, 3829597522u, 1215065114u},
                                    {0.9916666666666667, 1840791055u, 464253051u},
                                    {1.2, 2894063437u, 2800406033u},
                                    {1.8916666666666666, 4045976749u, 1445233788u},
                                    {2, 2272840073u, 147138245u},
                                    {2.2333333333333334, 2275318006u, 367565403u},
                                    {3.8, 1245914053u, 1245914053u},
                                    {5, 1245914053u, 1245914053u}};
        PixelFx         fx(PixelFxKind::SpawnPortal);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.spawnPortalLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::SpawnPortal);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.spawnPortalPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SpawnPortal), reference(PixelFxKind::SpawnPortal);
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

TEST_CASE("stylize.pixelfx.spawn.spawnPortalStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::SpawnPortal);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    for (double t : {4., 5.}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.summonRisePinnedSourcePhasesAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                {0.008333333333333333, 260485629u, 1048628554u},
                                {0.2, 867835912u, 1577133161u},
                                {0.49166666666666664, 2492359188u, 195349494u},
                                {0.5, 269289121u, 3044872665u},
                                {0.5083333333333333, 2826410960u, 1090852258u},
                                {0.775, 1526638252u, 147234007u},
                                {0.7833333333333333, 2354869551u, 2061436789u},
                                {1.0416666666666667, 3677834037u, 1750367387u},
                                {1.05, 1565695836u, 1790705555u},
                                {1.0583333333333333, 3028421009u, 2967747001u},
                                {1.2, 1163067902u, 4162204236u},
                                {1.5, 1666193730u, 2671175994u},
                                {1.6833333333333333, 3436208844u, 2011821492u},
                                {1.6916666666666667, 4194994337u, 2708364945u},
                                {1.7, 3355389783u, 2838869985u},
                                {2, 3843398568u, 2711327019u},
                                {2.2916666666666665, 2449797382u, 1211634471u},
                                {2.3, 2801628249u, 3859074417u},
                                {2.308333333333333, 3266745617u, 3812318494u},
                                {2.4, 1489436535u, 295856436u},
                                {3, 1951161346u, 148519408u},
                                {3.3916666666666666, 2432330799u, 3167905633u},
                                {3.4, 1308859343u, 287718610u},
                                {3.408333333333333, 420704225u, 2309529741u},
                                {4, 1906950885u, 1818118869u},
                                {4.2, 769835167u, 1050619382u},
                                {4.6, 3446172101u, 3446172101u},
                                {5, 3446172101u, 3446172101u},
                                {6, 3446172101u, 3446172101u},
                                {7, 3446172101u, 3446172101u}};
    PixelFx         fx(PixelFxKind::SummonRise);
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
TEST_CASE("stylize.pixelfx.spawn.summonRisePinnedSeedAndMaterialProfiles") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                    {0.49166666666666664, 3359879250u, 3469858497u},
                                    {0.775, 1561904792u, 169365633u},
                                    {1.05, 1258427236u, 3861996683u},
                                    {1.5, 792897638u, 3202205774u},
                                    {1.7, 1086294197u, 2122038809u},
                                    {2.3, 2070469934u, 2484877050u},
                                    {3, 184626418u, 684970061u},
                                    {3.408333333333333, 144516240u, 1875469700u},
                                    {4.6, 3446172101u, 3446172101u},
                                    {7, 3446172101u, 3446172101u}};
        PixelFx         fx(PixelFxKind::SummonRise);
        auto            s = fx.settings();
        s.seed            = 123u;
        s.palette         = {4279373632u, 4281862768u, 4286604976u, 4292677584u, 4293980400u};
        s.spawn.body      = {4281862768u, 4294967295u};
        s.spawn.horn      = {4294967295u, 4294901760u};
        s.spawn.stone     = {4280951850u, 4285556334u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                    {0.49166666666666664, 1440808944u, 1646117342u},
                                    {0.775, 2642462414u, 2413787116u},
                                    {1.05, 2574248040u, 1459131334u},
                                    {1.5, 2585767510u, 3489721157u},
                                    {1.7, 1792330306u, 913537648u},
                                    {2.3, 3992319228u, 4258982595u},
                                    {3, 1348238179u, 827312352u},
                                    {3.408333333333333, 2714067056u, 1575232235u},
                                    {4.6, 3446172101u, 3446172101u},
                                    {7, 3446172101u, 3446172101u}};
        PixelFx         fx(PixelFxKind::SummonRise);
        auto            s = fx.settings();
        s.seed            = 0u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                    {0.49166666666666664, 562422287u, 1561961789u},
                                    {0.775, 2080506132u, 1121357071u},
                                    {1.05, 722401948u, 527493913u},
                                    {1.5, 865177687u, 1184633575u},
                                    {1.7, 1114231663u, 2956877589u},
                                    {2.3, 1082881667u, 1151565635u},
                                    {3, 2057719080u, 1127641787u},
                                    {3.408333333333333, 725273217u, 3379183835u},
                                    {4.6, 3446172101u, 3446172101u},
                                    {7, 3446172101u, 3446172101u}};
        PixelFx         fx(PixelFxKind::SummonRise);
        auto            s = fx.settings();
        s.seed            = 4294967295u;
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                    {0.49166666666666664, 154656252u, 1832199715u},
                                    {0.775, 2501590550u, 1587260371u},
                                    {1.05, 1927687341u, 3882757728u},
                                    {1.5, 1759250314u, 3202862731u},
                                    {1.7, 270871953u, 2580536861u},
                                    {2.3, 2075786114u, 1333029897u},
                                    {3, 1875686691u, 2838626441u},
                                    {3.408333333333333, 1599562541u, 1306519843u},
                                    {4.6, 3446172101u, 3446172101u},
                                    {7, 3446172101u, 3446172101u}};
        PixelFx         fx(PixelFxKind::SummonRise);
        auto            s = fx.settings();
        s.palette         = {4281862768u};
        s.spawn.body      = {4294901760u};
        s.spawn.horn      = {4294901760u};
        s.spawn.stone     = {4294901760u};
        auto config       = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3446172101u, 3446172101u},
                                    {0.49166666666666664, 3634156844u, 4066418193u},
                                    {0.775, 1903148772u, 4284819161u},
                                    {1.05, 4148035334u, 1908569971u},
                                    {1.5, 2419366799u, 891372338u},
                                    {1.7, 4279506545u, 521982218u},
                                    {2.3, 1321358796u, 4275197276u},
                                    {3, 2104811806u, 1307399853u},
                                    {3.408333333333333, 1986607605u, 3345975494u},
                                    {4.6, 3446172101u, 3446172101u},
                                    {7, 3446172101u, 3446172101u}};
        PixelFx         fx(PixelFxKind::SummonRise);
        auto            s = fx.settings();
        s.palette         = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        s.spawn.body = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        s.spawn.horn = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        s.spawn.stone = {
            4278190080u, 4278343151u, 4278496222u, 4278649293u, 4278802364u, 4278955435u, 4279108506u, 4279261577u,
            4279414648u, 4279567719u, 4279720790u, 4279873861u, 4280026932u, 4280180003u, 4280333074u, 4280486145u,
            4280639216u, 4280792287u, 4280945358u, 4281098429u, 4281251500u, 4281404571u, 4281557642u, 4281710713u,
            4281863784u, 4282016855u, 4282169926u, 4282322997u, 4282476068u, 4282629139u, 4282782210u, 4282935281u,
            4283088352u, 4283241423u, 4283394494u, 4283547565u, 4283700636u, 4283853707u, 4284006778u, 4284159849u,
            4284312920u, 4284465991u, 4284619062u, 4284772133u, 4284925204u, 4285078275u, 4285231346u, 4285384417u,
            4285537488u, 4285690559u, 4285843630u, 4285996701u, 4286149772u, 4286302843u, 4286455914u, 4286608985u,
            4286762056u, 4286915127u, 4287068198u, 4287221269u, 4287374340u, 4287527411u, 4287680482u, 4287833553u};
        auto config = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.summonRiseLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::SummonRise);
    const auto info  = fx.info();
    uint32_t   first = 0;
    for (double t : {.3, .9, 1.5, 2.1, 2.8, 5., .3}) {
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
                REQUIRE(checksum(fx.color()) == first);
            else
                first = checksum(fx.color());
        }
    }
}
TEST_CASE("stylize.pixelfx.spawn.summonRisePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SummonRise), reference(PixelFxKind::SummonRise);
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

TEST_CASE("stylize.pixelfx.spawn.summonRiseStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::SummonRise);
    auto    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto c = std::vector<uint32_t>(fx.color().begin(), fx.color().end()),
               l = std::vector<uint32_t>(fx.light().begin(), fx.light().end());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
    REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    seek = fx.prepareSeek(1.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
    seek = fx.prepareSeek(4.);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    REQUIRE(!fx.done());
    for (double t : {fx.info().duration + 1, fx.info().duration + 2}) {
        seek = fx.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto p) { return p == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto p) { return p == 0; }));
    }
}


TEST_CASE("stylize.pixelfx.spawn.poofConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::Poof);
    auto    s     = fx.settings();
    s.spawn.stars = {0xff112233u};
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::Poof, s);
    REQUIRE(prepared);
    s.spawn.stars[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.stars.size() == 3);
    for (auto c : owned.spawn.stars) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Poof, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.stars.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Poof, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto bad        = owned;
        bad.spawn.stars = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Poof, bad));
        REQUIRE(fx.configurationValue() == value);
        bad.spawn.stars.assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Poof, bad));
    }
}
TEST_CASE("stylize.pixelfx.spawn.respawnBeamConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::RespawnBeam);
    auto    s     = fx.settings();
    s.spawn.width = 23.5;
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
    REQUIRE(prepared);
    s.spawn.width = 256;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.width == 23.5);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double v : {-.01, 256.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad        = owned;
        bad.spawn.width = v;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double v : {0., 256.}) {
        auto valid        = owned;
        valid.spawn.width = v;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, valid));
    }
    {
        auto bad         = owned;
        bad.actor.source = PixelFxActorSource::None;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto empty = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(empty);
        auto  v       = value;
        auto* options = v.find("options");
        REQUIRE(options);
        options->set("actor", empty.value());
        auto result = PixelFx::prepareConfigurationValue(PixelFxKind::RespawnBeam, v);
        REQUIRE(!result);
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto seek = fx.prepareSeek(0);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
    }
    {
        auto seek = fx.prepareSeek(1.3833333333333333);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
    }
    {
        auto seek = fx.prepareSeek(1.3916666666666666);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(3);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
}
TEST_CASE("stylize.pixelfx.spawn.shatterDeathConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::ShatterDeath);
    auto    s        = fx.settings();
    auto    prepared = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
    REQUIRE(prepared);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    {
        auto bad         = owned;
        bad.actor.source = PixelFxActorSource::None;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto empty = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(empty);
        auto  v       = value;
        auto* options = v.find("options");
        REQUIRE(options);
        options->set("actor", empty.value());
        auto result = PixelFx::prepareConfigurationValue(PixelFxKind::ShatterDeath, v);
        REQUIRE(result);
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto seek = fx.prepareSeek(0);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(1.3916666666666666);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(1.4);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
    }
    {
        auto seek = fx.prepareSeek(4);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Hidden);
    }
}
TEST_CASE("stylize.pixelfx.spawn.soulRiseConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::SoulRise);
    auto    s      = fx.settings();
    s.spawn.height = 23.5;
    auto prepared  = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
    REQUIRE(prepared);
    s.spawn.height = 256;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.height == 23.5);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double v : {-.01, 256.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad         = owned;
        bad.spawn.height = v;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SoulRise, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double v : {0., 256.}) {
        auto valid         = owned;
        valid.spawn.height = v;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SoulRise, valid));
    }
    {
        auto bad         = owned;
        bad.actor.source = PixelFxActorSource::None;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SoulRise, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto empty = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(empty);
        auto  v       = value;
        auto* options = v.find("options");
        REQUIRE(options);
        options->set("actor", empty.value());
        auto result = PixelFx::prepareConfigurationValue(PixelFxKind::SoulRise, v);
        REQUIRE(!result);
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto seek = fx.prepareSeek(0);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(0.35);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(2.55);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
    {
        auto seek = fx.prepareSeek(4);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
    }
}
TEST_CASE("stylize.pixelfx.spawn.spawnPortalConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::SpawnPortal);
    auto    s     = fx.settings();
    s.spawn.width = 23.5;
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, s);
    REQUIRE(prepared);
    s.spawn.width = 256;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.width == 23.5);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::SpawnPortal, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double v : {-.01, 256.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad        = owned;
        bad.spawn.width = v;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double v : {0., 256.}) {
        auto valid        = owned;
        valid.spawn.width = v;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SpawnPortal, valid));
    }
}
TEST_CASE("stylize.pixelfx.spawn.summonRiseConfigurationOwnershipBoundsAndAtomicRoundtrip") {
    PixelFx fx(PixelFxKind::SummonRise);
    auto    s     = fx.settings();
    s.spawn.body  = {0xff112233u};
    s.spawn.horn  = {0xff112233u};
    s.spawn.stone = {0xff112233u};
    auto prepared = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
    REQUIRE(prepared);
    s.spawn.body[0]  = 0xffabcdefu;
    s.spawn.horn[0]  = 0xffabcdefu;
    s.spawn.stone[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.spawn.body.size() == 5);
    for (auto c : owned.spawn.body) REQUIRE(c == 0xff112233u);
    REQUIRE(owned.spawn.horn.size() == 5);
    for (auto c : owned.spawn.horn) REQUIRE(c == 0xff112233u);
    REQUIRE(owned.spawn.stone.size() == 5);
    for (auto c : owned.spawn.stone) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::SummonRise, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.body.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto bad       = owned;
        bad.spawn.body = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
        bad.spawn.body.assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
    }
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.horn.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto bad       = owned;
        bad.spawn.horn = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
        bad.spawn.horn.assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
    }
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.spawn.stone.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    {
        auto bad        = owned;
        bad.spawn.stone = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
        REQUIRE(fx.configurationValue() == value);
        bad.spawn.stone.assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SummonRise, bad));
    }
}
TEST_CASE("stylize.pixelfx.spawn.summonRiseIndependentMaterialsAcrossInstances") {
    PixelFx a(PixelFxKind::SummonRise), reference(PixelFxKind::SummonRise);
    for (double t : {0., .5, 1.05, 1.7, 2.3, 3.4, 4.2, 5.}) {
        auto sa = a.prepareSeek(t), sr = reference.prepareSeek(t);
        REQUIRE(sa);
        REQUIRE(sr);
        REQUIRE(a.commitSeek(std::move(sa).takeValue()));
        REQUIRE(reference.commitSeek(std::move(sr).takeValue()));
        reference.draw();
        const std::vector<uint32_t> color(reference.color().begin(), reference.color().end()),
            light(reference.light().begin(), reference.light().end());
        PixelFx other(PixelFxKind::SummonRise);
        auto    s     = other.settings();
        s.spawn.body  = {0xff112233u};
        s.spawn.horn  = {0xffee8822u};
        s.spawn.stone = {0xff884466u};
        s.seed        = 123;
        auto cfg      = PixelFx::prepareConfiguration(PixelFxKind::SummonRise, s);
        REQUIRE(cfg);
        REQUIRE(other.commitConfiguration(std::move(cfg).takeValue()));
        auto seek = other.prepareSeek(t);
        REQUIRE(seek);
        REQUIRE(other.commitSeek(std::move(seek).takeValue()));
        other.draw();
        a.draw();
        reference.draw();
        REQUIRE(std::equal(a.color().begin(), a.color().end(), color.begin()));
        REQUIRE(std::equal(a.light().begin(), a.light().end(), light.begin()));
        REQUIRE(std::equal(reference.color().begin(), reference.color().end(), color.begin()));
        REQUIRE(std::equal(reference.light().begin(), reference.light().end(), light.begin()));
    }
}

TEST_CASE("stylize.pixelfx.spawn.respawnBeamPinnedStockBackBodyFrontComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{1, 0, 1792380357u, 1792380357u},
                                {1, 0.25, 662712017u, 2160532468u},
                                {1, 0.325, 4057260800u, 15161011u},
                                {1, 0.4, 1483058312u, 1331507475u},
                                {1, 0.8, 1776302826u, 4182733309u},
                                {1, 1.35, 1196372663u, 2292275948u},
                                {1, 1.3833333333333333, 2329024769u, 2219877608u},
                                {1, 1.3916666666666666, 3542685475u, 2946971205u},
                                {1, 1.5, 349142817u, 3166080787u},
                                {1, 2, 3838005994u, 3396713376u},
                                {1, 3, 4180500692u, 1792380357u},
                                {2, 0, 1792380357u, 1792380357u},
                                {2, 0.25, 662712017u, 2160532468u},
                                {2, 0.325, 4057260800u, 15161011u},
                                {2, 0.4, 1483058312u, 1331507475u},
                                {2, 0.8, 1279888099u, 3870596352u},
                                {2, 1.35, 2191453758u, 941537541u},
                                {2, 1.3833333333333333, 1368434228u, 336814509u},
                                {2, 1.3916666666666666, 395860549u, 3603566655u},
                                {2, 1.5, 4118632325u, 3998807078u},
                                {2, 2, 1418275569u, 1262364453u},
                                {2, 3, 1518130415u, 1792380357u}};
    PixelFx         fx(PixelFxKind::RespawnBeam);
    const auto      info = fx.info();
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto cfg       = PixelFx::prepareConfiguration(PixelFxKind::RespawnBeam, s);
        REQUIRE(cfg);
        REQUIRE(fx.commitConfiguration(std::move(cfg).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (fx.actorVisibility() == PixelFxActorVisibility::Visible) {
            REQUIRE(fx.drawActor(body));
            for (size_t i = 0; i < body.size(); ++i)
                if (body[i]) color[i] = body[i];
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.spawn.shatterDeathPinnedStockBackBodyFrontComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{1, 0, 3433812431u, 3400205765u},
                                {1, 0.2, 447038392u, 1438283699u},
                                {1, 0.5, 2385186078u, 1991916249u},
                                {1, 0.5083333333333333, 674086183u, 319443325u},
                                {1, 0.8, 99954508u, 3614952830u},
                                {1, 1.05, 4068936017u, 124195906u},
                                {1, 1.2, 995565529u, 3829220945u},
                                {1, 1.3916666666666666, 1559974228u, 178970918u},
                                {1, 1.4, 1109902770u, 1735927012u},
                                {1, 1.4083333333333334, 3946590361u, 3510383492u},
                                {1, 1.8, 2789757901u, 2606159810u},
                                {1, 2.7, 716147443u, 1115163384u},
                                {1, 4, 3400205765u, 3400205765u},
                                {2, 0, 1471476207u, 3400205765u},
                                {2, 0.2, 2282474140u, 1874117919u},
                                {2, 0.5, 1349466434u, 4245416925u},
                                {2, 0.5083333333333333, 759836253u, 2562636602u},
                                {2, 0.8, 3311145376u, 399937056u},
                                {2, 1.05, 3328135334u, 2465680751u},
                                {2, 1.2, 1845846159u, 169685958u},
                                {2, 1.3916666666666666, 163857077u, 3639567531u},
                                {2, 1.4, 239604254u, 321824257u},
                                {2, 1.4083333333333334, 3925902928u, 2397277404u},
                                {2, 1.8, 1969854346u, 478140718u},
                                {2, 2.7, 3808065264u, 3184715681u},
                                {2, 4, 3400205765u, 3400205765u}};
    PixelFx         fx(PixelFxKind::ShatterDeath);
    const auto      info = fx.info();
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto cfg       = PixelFx::prepareConfiguration(PixelFxKind::ShatterDeath, s);
        REQUIRE(cfg);
        REQUIRE(fx.commitConfiguration(std::move(cfg).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (fx.actorVisibility() == PixelFxActorVisibility::Visible) {
            REQUIRE(fx.drawActor(body));
            for (size_t i = 0; i < body.size(); ++i)
                if (body[i]) color[i] = body[i];
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.spawn.soulRisePinnedStockBackBodyFrontComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{1, 0, 4294735612u, 1261520325u},    {1, 0.3, 3198685984u, 3530028753u},
                                {1, 0.35, 214139063u, 1557597025u},  {1, 0.4166666666666667, 4202722688u, 3460212533u},
                                {1, 0.8, 1944420924u, 2531574132u},  {1, 1.2, 1723725513u, 2461850908u},
                                {1, 1.8, 4247165492u, 2428223038u},  {1, 2.05, 41614894u, 3797026260u},
                                {1, 2.55, 2587975006u, 1555690823u}, {1, 2.65, 585249818u, 3222709045u},
                                {1, 3.2, 2134867412u, 1261520325u},  {1, 4, 2134867412u, 1261520325u},
                                {2, 0, 1763130275u, 1261520325u},    {2, 0.3, 3784357871u, 3530028753u},
                                {2, 0.35, 793119199u, 1159598319u},  {2, 0.4166666666666667, 4132934711u, 3703483453u},
                                {2, 0.8, 3993596526u, 68195158u},    {2, 1.2, 474387740u, 2463822787u},
                                {2, 1.8, 3060612169u, 1449909874u},  {2, 2.05, 481517690u, 4043310650u},
                                {2, 2.55, 2541312105u, 2653183460u}, {2, 2.65, 4197583745u, 2624713526u},
                                {2, 3.2, 4287423215u, 1261520325u},  {2, 4, 4287423215u, 1261520325u}};
    PixelFx         fx(PixelFxKind::SoulRise);
    const auto      info = fx.info();
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto cfg       = PixelFx::prepareConfiguration(PixelFxKind::SoulRise, s);
        REQUIRE(cfg);
        REQUIRE(fx.commitConfiguration(std::move(cfg).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        std::vector<uint32_t> color(size_t(info.width) * size_t(info.height)), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        if (fx.actorVisibility() == PixelFxActorVisibility::Visible) {
            REQUIRE(fx.drawActor(body));
            for (size_t i = 0; i < body.size(); ++i)
                if (body[i]) color[i] = body[i];
        }
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}

}  // namespace test_pixelfx_spawn_unit
