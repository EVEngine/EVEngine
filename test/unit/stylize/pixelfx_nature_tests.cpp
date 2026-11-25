// Nature owns liquid drops, etched acid and subsequent nature kernels; separate from completed Shadow suites.
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/NatureGas.h"
#include "stylize/pixelfx/NatureGeometry.h"
#include "stylize/pixelfx/NatureSimulation.h"
#include "stylize/pixelfx/Rig.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_nature_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
uint32_t alphaChecksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels) h = (h ^ (c >> 24)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.nature.acidSplashPinnedPhasesAlphaLightAndColorOracles") {
    // Independent source planes. Alpha/light hashes are strict. Steam RGB uses the
    // approved one-LSB contract; full raw comparisons retain all exact/difference data.
    struct Frame {
        double   time;
        uint32_t alpha, light;
    };
    constexpr Frame frames[] = {{0, 1944750598u, 3466607365u},
                                {0.008333333333333333, 416031597u, 3178602437u},
                                {0.1, 3313871002u, 2653623769u},
                                {0.2, 3191112558u, 1042102533u},
                                {0.2916666666666667, 3159209033u, 1319789369u},
                                {0.3, 2932860541u, 2042724333u},
                                {0.30833333333333335, 1723243868u, 2194138958u},
                                {0.36666666666666664, 3559096623u, 1958468066u},
                                {0.375, 3396243159u, 865762174u},
                                {0.38333333333333336, 4031782322u, 1109833462u},
                                {0.39166666666666666, 714545999u, 4034104179u},
                                {0.4, 4152756354u, 4005672943u},
                                {0.5583333333333333, 2212878179u, 1723824513u},
                                {0.5666666666666667, 3762162626u, 2029528945u},
                                {0.575, 3484388630u, 633873925u},
                                {0.6916666666666667, 3788077993u, 1444071290u},
                                {0.7, 703600863u, 125191965u},
                                {0.7083333333333334, 2482325082u, 3525701267u},
                                {1.0916666666666666, 1094264512u, 2713045669u},
                                {1.1, 1263685347u, 796624146u},
                                {1.1083333333333334, 2435139691u, 3858193848u},
                                {1.9916666666666667, 3947692843u, 3801865279u},
                                {2.191666666666667, 2103270553u, 1264825015u},
                                {2.2, 1036295168u, 2298786061u},
                                {2.2083333333333335, 2933548179u, 3410723274u},
                                {2.5, 3329201039u, 1893850053u},
                                {2.716666666666667, 3642717135u, 1893850053u},
                                {2.725, 3858174948u, 1893850053u},
                                {2.75, 600902981u, 1893850053u},
                                {2.7583333333333333, 600902981u, 1893850053u},
                                {2.8, 600902981u, 1893850053u},
                                {2.808333333333333, 600902981u, 1893850053u},
                                {4, 600902981u, 1893850053u},
                                {5, 600902981u, 1893850053u}};
    struct Oracle {
        size_t   frame, index;
        uint32_t color;
    };
    constexpr Oracle colors[] = {
        {0, 3772, 4280999072u},   {0, 6897, 4279521836u},   {0, 6902, 4279521836u},   {0, 7450, 4280069738u},
        {0, 18308, 0u},           {0, 18480, 0u},           {0, 18492, 0u},           {0, 18504, 0u},
        {0, 19044, 0u},           {1, 6531, 4291887096u},   {1, 6897, 4279521836u},   {1, 6902, 4279521836u},
        {1, 7634, 4279521836u},   {1, 18308, 0u},           {1, 18480, 0u},           {1, 18492, 0u},
        {1, 18504, 0u},           {1, 19044, 0u},           {2, 5612, 4280999072u},   {2, 8921, 4280999072u},
        {2, 8926, 4280999072u},   {2, 9659, 4279521836u},   {2, 18308, 0u},           {2, 18480, 0u},
        {2, 18492, 0u},           {2, 18504, 0u},           {2, 19044, 0u},           {3, 9292, 4280999072u},
        {3, 12050, 4279521836u},  {3, 12053, 4279521836u},  {3, 13523, 4279521836u},  {3, 18308, 0u},
        {3, 18480, 0u},           {3, 18492, 0u},           {3, 18504, 0u},           {3, 19044, 0u},
        {4, 16835, 4291887096u},  {4, 17018, 4279521836u},  {4, 17021, 4279521836u},  {4, 18308, 4280069738u},
        {4, 18480, 0u},           {4, 18491, 4280069738u},  {4, 18492, 4280069738u},  {4, 18504, 0u},
        {4, 19044, 0u},           {5, 16284, 4285202396u},  {5, 17748, 4285202396u},  {5, 17764, 4285202396u},
        {5, 18308, 4291887096u},  {5, 18480, 0u},           {5, 18492, 4291887096u},  {5, 18504, 0u},
        {5, 18672, 2989153440u},  {5, 18679, 2987949637u},  {5, 19044, 4285202396u},  {5, 19228, 4285202396u},
        {6, 16468, 4285202396u},  {6, 17749, 4285202396u},  {6, 17763, 4285202396u},  {6, 18308, 4291887096u},
        {6, 18480, 0u},           {6, 18492, 4291887096u},  {6, 18504, 0u},           {6, 18671, 2989153440u},
        {6, 18680, 2987949637u},  {6, 18857, 2989153440u},  {6, 19044, 4285202396u},  {7, 15176, 4280999072u},
        {7, 17003, 4291887096u},  {7, 17036, 4280999072u},  {7, 17201, 2350415429u},  {7, 17928, 2350689898u},
        {7, 18136, 3692524864u},  {7, 18308, 4291887096u},  {7, 18480, 4280999072u},  {7, 18492, 4291887096u},
        {7, 18504, 4281377906u},  {7, 18850, 4280328053u},  {7, 19044, 2988224106u},  {7, 19223, 2989153440u},
        {8, 14991, 4280999072u},  {8, 15932, 4280999072u},  {8, 17197, 2350689898u},  {8, 17370, 4291887096u},
        {8, 17743, 2350689898u},  {8, 17952, 2350141996u},  {8, 18308, 4280069738u},  {8, 18480, 4280999072u},
        {8, 18492, 4291887096u},  {8, 18504, 4279864400u},  {8, 18847, 4279795269u},  {8, 19044, 2988224106u},
        {8, 19222, 2989153440u},  {9, 14807, 4280999072u},  {9, 16853, 4280999072u},  {9, 17015, 2689843343u},
        {9, 17368, 4291887096u},  {9, 17742, 2350689898u},  {9, 17769, 2350141996u},  {9, 18308, 4291887096u},
        {9, 18480, 4280999072u},  {9, 18492, 4280069738u},  {9, 18504, 4279864400u},  {9, 18847, 4280598404u},
        {9, 19044, 2988224106u},  {9, 19221, 2989153440u},  {10, 14623, 4291887096u}, {10, 16301, 4291887096u},
        {10, 17013, 2350689898u}, {10, 17366, 4291887096u}, {10, 17742, 2350689898u}, {10, 18137, 3692524864u},
        {10, 18308, 4291887096u}, {10, 18480, 4280999072u}, {10, 18492, 4280069738u}, {10, 18504, 4279864400u},
        {10, 18844, 4279795269u}, {10, 19044, 2988224106u}, {10, 19220, 2989153440u}, {11, 14439, 4291887096u},
        {11, 16302, 4291887096u}, {11, 17012, 2350689898u}, {11, 17548, 4291887096u}, {11, 17742, 2350689898u},
        {11, 18137, 3692524864u}, {11, 18308, 4291887096u}, {11, 18480, 4280999072u}, {11, 18492, 4280069738u},
        {11, 18504, 4279864400u}, {11, 19044, 2988224106u}, {11, 19045, 4279934814u}, {11, 19220, 2989153440u},
        {12, 13518, 4285202396u}, {12, 15914, 249622764u},  {12, 17159, 4291887096u}, {12, 17248, 4291887096u},
        {12, 18084, 719384812u},  {12, 18162, 383840492u},  {12, 18308, 4280069738u}, {12, 18480, 4280999072u},
        {12, 18492, 4280069738u}, {12, 18504, 4280999072u}, {12, 19044, 4279795269u}, {12, 19211, 4280999072u},
        {12, 19219, 2989153440u}, {13, 13518, 4285202396u}, {13, 15913, 601944300u},  {13, 17342, 4291887096u},
        {13, 17434, 4291887096u}, {13, 18084, 752939244u},  {13, 18162, 618721516u},  {13, 18308, 4280069738u},
        {13, 18480, 4280999072u}, {13, 18492, 4280069738u}, {13, 18504, 4285202396u}, {13, 19044, 4279795269u},
        {13, 19211, 4280999072u}, {13, 19219, 2989153440u}, {14, 13702, 4285202396u}, {14, 15914, 803270892u},
        {14, 17525, 4291887096u}, {14, 17619, 4291887096u}, {14, 17717, 635498732u},  {14, 18085, 1323364588u},
        {14, 18162, 887156972u},  {14, 18308, 4280069738u}, {14, 18480, 4280999072u}, {14, 18492, 4280069738u},
        {14, 18504, 4280069738u}, {14, 19044, 4279795269u}, {14, 19211, 4280999072u}, {15, 15180, 249622764u},
        {15, 15909, 4280069738u}, {15, 17992, 434172140u},  {15, 18073, 1893789932u}, {15, 18308, 4291887096u},
        {15, 18438, 4291887096u}, {15, 18480, 4280999072u}, {15, 18492, 4280069738u}, {15, 18504, 4280069738u},
        {15, 18547, 4291887096u}, {15, 19044, 4280069738u}, {15, 19396, 4280999072u}, {16, 15180, 803270891u},
        {16, 16277, 4280069738u}, {16, 17992, 887156972u},  {16, 18073, 1977676012u}, {16, 18308, 4280999072u},
        {16, 18438, 4291887096u}, {16, 18480, 4280999072u}, {16, 18492, 4280069738u}, {16, 18504, 4280069738u},
        {16, 18547, 4291887096u}, {16, 19044, 4280069738u}, {16, 19396, 4280999072u}, {17, 15180, 803270636u},
        {17, 16461, 4280069738u}, {17, 17808, 232845548u},  {17, 18073, 2028007660u}, {17, 18308, 4280999072u},
        {17, 18438, 4291887096u}, {17, 18480, 4280999072u}, {17, 18492, 4280069738u}, {17, 18504, 4280069738u},
        {17, 18547, 4291887096u}, {17, 19044, 4280069738u}, {17, 19395, 4280999072u}, {18, 12973, 182513900u},
        {18, 16231, 2061562092u}, {18, 16889, 484503788u},  {18, 17043, 4291887096u}, {18, 18106, 199291116u},
        {18, 18308, 4285202396u}, {18, 18438, 4285202396u}, {18, 18480, 4280069738u}, {18, 18492, 4280999072u},
        {18, 18504, 4280069738u}, {18, 18547, 4285202396u}, {18, 19044, 4280069738u}, {18, 19580, 4280999072u},
        {19, 12789, 48296172u},   {19, 16047, 1105260780u}, {19, 16860, 4291887096u}, {19, 16889, 450949356u},
        {19, 18106, 316731628u},  {19, 18308, 4285202396u}, {19, 18438, 4285202396u}, {19, 18480, 4280069738u},
        {19, 18492, 4280999072u}, {19, 18504, 4280069738u}, {19, 18547, 4285202396u}, {19, 19044, 4280069738u},
        {19, 19580, 4280999072u}, {20, 12789, 48296172u},   {20, 16231, 2061562092u}, {20, 16860, 4291887096u},
        {20, 16889, 148959468u},  {20, 18106, 467726572u},  {20, 18308, 4285202396u}, {20, 18438, 4285202396u},
        {20, 18480, 4280069738u}, {20, 18492, 4280999072u}, {20, 18504, 4280069738u}, {20, 18547, 4285202396u},
        {20, 19044, 4280069738u}, {20, 19580, 4280999072u}, {21, 15150, 31518956u},   {21, 16128, 920711404u},
        {21, 16434, 283177196u},  {21, 17192, 4291887096u}, {21, 18104, 1960898796u}, {21, 18308, 0u},
        {21, 18438, 4279247396u}, {21, 18480, 4278915345u}, {21, 18492, 4278915345u}, {21, 18504, 4278915345u},
        {21, 18546, 4279247396u}, {21, 19044, 4294967295u}, {21, 19579, 4280999072u}, {22, 14273, 31518956u},
        {22, 15393, 501281004u},  {22, 15700, 635498732u},  {22, 17391, 4291887096u}, {22, 18308, 0u},
        {22, 18315, 836825324u},  {22, 18438, 4279247396u}, {22, 18480, 4278915345u}, {22, 18492, 4278915345u},
        {22, 18504, 4278915345u}, {22, 18546, 4279247396u}, {22, 19044, 4294967295u}, {22, 19579, 4279795269u},
        {23, 14420, 48296172u},   {23, 15393, 987820268u},  {23, 15700, 618721516u},  {23, 17391, 4280069738u},
        {23, 18308, 0u},          {23, 18315, 1222701292u}, {23, 18438, 4279247396u}, {23, 18480, 4278915345u},
        {23, 18492, 4278915345u}, {23, 18504, 4278915345u}, {23, 18546, 4279247396u}, {23, 19044, 4294967295u},
        {23, 19579, 4279795269u}, {24, 14236, 48296172u},   {24, 15393, 954265836u},  {24, 15700, 769716460u},
        {24, 17391, 4280069738u}, {24, 18308, 0u},          {24, 18315, 1507913964u}, {24, 18440, 4278915345u},
        {24, 18480, 4278915345u}, {24, 18492, 4278915345u}, {24, 18504, 4278915345u}, {24, 18544, 4278915345u},
        {24, 19044, 4294967295u}, {24, 19579, 4279795269u}, {25, 14814, 48296172u},   {25, 15528, 417394924u},
        {25, 16126, 249622764u},  {25, 18111, 685830380u},  {25, 18279, 4279247396u}, {25, 18308, 0u},
        {25, 18458, 4280069738u}, {25, 18480, 4278915345u}, {25, 18492, 4278915345u}, {25, 18504, 4278915345u},
        {25, 18522, 4279247396u}, {25, 19044, 0u},          {25, 19580, 4278915345u}, {26, 14618, 48296172u},
        {26, 14980, 31518956u},   {26, 15744, 48296172u},   {26, 17008, 48296172u},   {26, 18308, 0u},
        {26, 18480, 0u},          {26, 18492, 0u},          {26, 18504, 0u},          {26, 19044, 0u},
        {27, 14988, 31518956u},   {27, 15349, 48296172u},   {27, 15744, 31518956u},   {27, 17024, 31518956u},
        {27, 18308, 0u},          {27, 18480, 0u},          {27, 18492, 0u},          {27, 18504, 0u},
        {27, 19044, 0u},          {28, 18308, 0u},          {28, 18480, 0u},          {28, 18492, 0u},
        {28, 18504, 0u},          {28, 19044, 0u},          {29, 18308, 0u},          {29, 18480, 0u},
        {29, 18492, 0u},          {29, 18504, 0u},          {29, 19044, 0u},          {30, 18308, 0u},
        {30, 18480, 0u},          {30, 18492, 0u},          {30, 18504, 0u},          {30, 19044, 0u},
        {31, 18308, 0u},          {31, 18480, 0u},          {31, 18492, 0u},          {31, 18504, 0u},
        {31, 19044, 0u},          {32, 18308, 0u},          {32, 18480, 0u},          {32, 18492, 0u},
        {32, 18504, 0u},          {32, 19044, 0u},          {33, 18308, 0u},          {33, 18480, 0u},
        {33, 18492, 0u},          {33, 18504, 0u},          {33, 19044, 0u}};
    PixelFx fx(PixelFxKind::AcidSplash);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < std::size(frames); ++i) {
        auto seek = fx.prepareSeek(frames[i].time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(alphaChecksum(fx.color()) == frames[i].alpha);
        REQUIRE(checksum(fx.light()) == frames[i].light);
        for (const auto& expected : colors)
            if (expected.frame == i) {
                const auto actual = fx.color()[expected.index];
                REQUIRE((actual >> 24) == (expected.color >> 24));
                for (int s : {0, 8, 16})
                    REQUIRE(std::abs(int((actual >> s) & 255u) - int((expected.color >> s) & 255u)) <= 1);
            }
    }
}
TEST_CASE("stylize.pixelfx.nature.acidSplashOwningPaletteAndAtomicBounds") {
    PixelFx fx(PixelFxKind::AcidSplash);
    auto    settings = fx.settings();
    settings.palette = {0xff112233u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::AcidSplash, settings);
    REQUIRE(prepared);
    settings.palette[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.palette.size() == 8);
    for (uint32_t c : owned.palette) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::AcidSplash, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        settings.palette.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::AcidSplash, settings));
        REQUIRE(fx.configurationValue() == value);
    }
    settings.palette = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::AcidSplash, settings));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.nature.acidSplashLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::AcidSplash);
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
TEST_CASE("stylize.pixelfx.nature.acidSplashPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::AcidSplash), reference(PixelFxKind::AcidSplash);
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

TEST_CASE("stylize.pixelfx.nature.acidSplashStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::AcidSplash);
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

TEST_CASE("stylize.pixelfx.nature.dropsPinnedDelayCrossingsOrderAndDraw") {
    nature::Drops      drops;
    const nature::Drop initial[] = {{-7.0, -2.0, 40.0, 100.0, 1.0, 0.0}, {3.0, -1.0, -20.0, 80.0, 2.0, 0.0},
                                    {7.0, 1.0, 0.0, 5.0, 1.0, 0.0},      {0.0, -3.0, 0.0, 0.0, 1.0, 0.02},
                                    {-4.0, -7.0, 0.0, 0.0, 2.0, 0.0},    {9.0, -0.1, 1.0, 10.0, 1.0, 0.0},
                                    {11.0, -0.1, -1.0, 10.0, 1.0, 0.0}};
    for (auto d : initial) drops.add(d);
    struct Event {
        int          tick;
        nature::Drop drop;
        double       floor;
    };
    std::vector<Event>    events;
    PixelFx               defaults(PixelFxKind::AcidSplash);
    const auto            settings = defaults.settings();
    const PixelFxInfo     info{PixelFxKind::AcidSplash, "nature/drop-fixture", 96, 80, 48, 64, 2.8, false};
    std::vector<uint32_t> color(96 * 80), light(color.size());
    Paint                 p{info, color, light, 1};
    int                   tick = 0;
    while (tick < 0) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 7);
    REQUIRE(events.size() == 0);
    {
        const nature::Drop expected[] = {{-7.0, -2.0, 40.0, 100.0, 1.0, 0.0}, {3.0, -1.0, -20.0, 80.0, 2.0, 0.0},
                                         {7.0, 1.0, 0.0, 5.0, 1.0, 0.0},      {0.0, -3.0, 0.0, 0.0, 1.0, 0.02},
                                         {-4.0, -7.0, 0.0, 0.0, 2.0, 0.0},    {9.0, -0.1, 1.0, 10.0, 1.0, 0.0},
                                         {11.0, -0.1, -1.0, 10.0, 1.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 3324873296u);
    REQUIRE(checksum(light) == 1968320934u);
    while (tick < 1) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 5);
    REQUIRE(events.size() == 2);
    {
        const nature::Drop expected[] = {{-6.668333333333333, -1.1029427083333334, 39.8, 107.64687500000001, 1.0, 0.0},
                                         {2.83375, -0.2682769097222223, -19.95, 87.80677083333333, 2.0, 0.0},
                                         {7.0, 1.1073697916666667, 0.0, 12.884375, 1.0, 0.0},
                                         {0.0, -3.0, 0.0, 0.0, 1.0, 0.011666666666666667},
                                         {-4.0, -6.934110243055556, 0.0, 7.906770833333334, 2.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 3138129685u);
    REQUIRE(checksum(light) == 1996173287u);
    while (tick < 2) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 4);
    REQUIRE(events.size() == 3);
    {
        const nature::Drop expected[] = {
            {-6.338324999999999, -0.14232076822916662, 39.601, 115.27463281250002, 1.0, 0.0},
            {7.0, 1.2802784505208333, 0.0, 20.7490390625, 1.0, 0.0},
            {0.0, -3.0, 0.0, 0.0, 1.0, 0.003333333333333334},
            {-4.0, -6.8024130913628476, 0.0, 15.803658203125, 2.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 1746014008u);
    REQUIRE(checksum(light) == 2816862916u);
    while (tick < 3) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 3);
    REQUIRE(events.size() == 4);
    {
        const nature::Drop expected[] = {{7.0, 1.5185621293945313, 0.0, 28.59404146484375, 1.0, 0.0},
                                         {0.0, -3.0, 0.0, 0.0, 1.0, -0.004999999999999999},
                                         {-4.0, -6.60499080416531, 0.0, 23.69067446370443, 2.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 1241190381u);
    REQUIRE(checksum(light) == 360136719u);
    while (tick < 4) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 3);
    REQUIRE(events.size() == 4);
    {
        const nature::Drop expected[] = {
            {7.0, 1.8220573907377116, 0.0, 36.419431361181644, 1.0, 0.0},
            {0.0, -2.9341927083333332, 0.0, 7.8968750000000005, 1.0, -0.004999999999999999},
            {-4.0, -6.341925537882326, 0.0, 31.567831953958134, 2.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 908619720u);
    REQUIRE(checksum(light) == 1720125775u);
    while (tick < 12) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 3);
    REQUIRE(events.size() == 4);
    {
        const nature::Drop expected[] = {
            {7.0, 6.578158309609238, 0.0, 98.32235840312507, 1.0, 0.0},
            {0.0, -0.05832794900684313, 0.0, 70.36528660120506, 1.0, -0.004999999999999999},
            {-4.0, -1.8840811005776672, 0.0, 94.23165172974886, 2.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 2233555784u);
    REQUIRE(checksum(light) == 445698077u);
    while (tick < 24) {
        ++tick;
        drops.update(
            1. / 120, [](double) { return 0.; },
            [&](const nature::Drop& d, double floor) { events.push_back({tick, d, floor}); });
    }
    REQUIRE(drops.list.size() == 1);
    REQUIRE(events.size() == 6);
    {
        const nature::Drop expected[] = {{7.0, 21.338285092175557, 0.0, 188.88322252866894, 1.0, 0.0}};
        for (size_t i = 0; i < std::size(expected); ++i) {
            const auto& a = drops.list[i];
            const auto& b = expected[i];
            REQUIRE(std::abs(a.x - b.x) < 1e-12);
            REQUIRE(std::abs(a.y - b.y) < 1e-12);
            REQUIRE(std::abs(a.vx - b.vx) < 1e-12);
            REQUIRE(std::abs(a.vy - b.vy) < 1e-12);
            REQUIRE(a.size == b.size);
            REQUIRE(a.delay == b.delay);
        }
    }
    std::fill(color.begin(), color.end(), 0);
    std::fill(light.begin(), light.end(), 0);
    drops.draw(p, settings.palette);
    REQUIRE(checksum(color) == 2999778757u);
    REQUIRE(checksum(light) == 2999778757u);
    const Event expectedEvents[] = {
        {1, {10.991708333333333, 0.04893229166666668, -0.995, 17.871875000000003, 1.0, 0.0}, 0.0},
        {1, {9.008291666666667, 0.04893229166666668, 0.995, 17.871875000000003, 1.0, 0.0}, 0.0},
        {2, {2.667915625, 0.5284212836371528, -19.900125, 95.603783203125, 2.0, 0.0}, 0.0},
        {3, {-6.009966708333333, 0.8817069086914063, 39.402995, 122.88332123046877, 1.0, 0.0}, 0.0},
        {13, {0.0, 0.5923907875323405, 0.0, 78.08624838470205, 1.0, -0.004999999999999999}, 0.0},
        {15, {-4.0, 0.860834816632444, 0.0, 117.56939902628802, 2.0, 0.0}, 0.0}};
    REQUIRE(events.size() == std::size(expectedEvents));
    for (size_t i = 0; i < events.size(); ++i) {
        REQUIRE(events[i].tick == expectedEvents[i].tick);
        REQUIRE(std::abs(events[i].drop.x - expectedEvents[i].drop.x) < 1e-12);
        REQUIRE(events[i].floor == expectedEvents[i].floor);
    }
}

TEST_CASE("stylize.pixelfx.nature.stoneShapePinnedCornersAndRngConsumption") {
    struct Expected {
        uint32_t             seed;
        bool                 explicitCount;
        size_t               count;
        uint32_t             rng;
        std::array<Point, 8> points;
    };
    const Expected expected[] = {{11u,
                                  false,
                                  6,
                                  1703683430u,
                                  {{{-1.0296598731084643, -0.22334386823055538},
                                    {-0.23165670095555557, -0.6313300231744159},
                                    {0.6420051694707694, -0.6335006337385071},
                                    {0.9005088856001987, 0.12744622350080168},
                                    {0.11862515170878254, 0.7765707997801511},
                                    {-0.7200111024901108, 0.602944063145127}}}},
                                 {11u,
                                  true,
                                  8,
                                  2903413573u,
                                  {{{-0.9491467825112139, -0.09860353191798196},
                                    {-0.4954341908505473, -0.6813430392635974},
                                    {-0.10410565735307516, -0.7957376713886588},
                                    {0.7082109563432082, -0.4772358670542482},
                                    {1.0552416910492908, 0.05811469328152525},
                                    {0.6719170810562652, 0.6222954788785705},
                                    {-0.11925823135921194, 0.6658894077197558},
                                    {-0.5455835929670524, 0.537790731137809}}}},
                                 {123u,
                                  false,
                                  7,
                                  1071847872u,
                                  {{{0.4798669972702517, 0.6161397489809791},
                                    {-0.6157647875142952, 0.7378699016137995},
                                    {-1.0336760800394087, 0.007797003423630882},
                                    {-0.781884685280103, -0.4810605595305017},
                                    {0.1940806085409951, -0.6545482645615649},
                                    {0.5798848177920952, -0.6453163257913652},
                                    {0.917790897681309, 0.11894659924907543}}}},
                                 {123u,
                                  true,
                                  8,
                                  2903413685u,
                                  {{{0.18682269154454306, -0.5958994124749447},
                                    {0.8657946784035238, -0.45209260671815304},
                                    {1.0079913525331075, 0.3158011521963299},
                                    {0.36538910228294896, 0.6062815425611013},
                                    {-0.3131955826210515, 0.8074138925354731},
                                    {-0.5593552244181546, 0.34673507626839595},
                                    {-0.8300081626122344, -0.17579785694518105},
                                    {-0.4931563986530521, -0.6170675126557109}}}},
                                 {0u,
                                  false,
                                  6,
                                  1703683420u,
                                  {{{1.0581243717610094, 0.28020969686970765},
                                    {0.5723236836149813, 0.6669601286964135},
                                    {-0.5530418094222664, 0.5988065450309079},
                                    {-0.8598293985615997, -0.23424141713058605},
                                    {-0.38063582699034304, -0.5799469100585654},
                                    {0.3813513921912338, -0.622055622694968}}}},
                                 {0u,
                                  true,
                                  8,
                                  2903413563u,
                                  {{{-0.766608391938752, -0.5649287455590133},
                                    {0.18087878549670164, -0.5694536191333379},
                                    {0.7562203816187125, -0.46739755485367324},
                                    {1.117714338222488, -0.015359102183174624},
                                    {0.6516524894287253, 0.4506649655379382},
                                    {0.12804516180780412, 0.6112197320502257},
                                    {-0.49606417911717327, 0.42347626112031334},
                                    {-0.7469224957857098, -0.004321872576301703}}}},
                                 {4294967295u,
                                  false,
                                  7,
                                  1071847748u,
                                  {{{0.16550243440084078, 0.9559219152507143},
                                    {-0.5075720370547293, 0.779817560941374},
                                    {-0.7634219870707497, 0.11853730462648593},
                                    {-0.588779981427637, -0.8156790628143555},
                                    {-0.07503657755197897, -0.7813195846972185},
                                    {0.8055356297308218, -0.3880021380258113},
                                    {0.9910788737049481, 0.40697401265053645}}}},
                                 {4294967295u,
                                  true,
                                  8,
                                  2903413561u,
                                  {{{0.9323229942532718, -0.4384157196203994},
                                    {0.8975320699550478, 0.20008218095036845},
                                    {0.49226780430270356, 0.5794618985704192},
                                    {0.02850135618579321, 0.8438907931700027},
                                    {-0.7030723598567082, 0.2820604685226625},
                                    {-1.0132729476203715, -0.050574336826880915},
                                    {-0.6281195010318772, -0.6108199662717796},
                                    {0.23755093652765796, -0.5470429024352071}}}}};
    for (const auto& e : expected) {
        Rng        rng(e.seed);
        const auto shape = e.explicitCount ? nature::stoneShape(rng, 8) : nature::randomStoneShape(rng);
        REQUIRE(shape.count == e.count);
        REQUIRE(rng.state == e.rng);
        for (size_t i = 0; i < shape.count; ++i) {
            REQUIRE(std::abs(shape.points[i].x - e.points[i].x) < 1e-12);
            REQUIRE(std::abs(shape.points[i].y - e.points[i].y) < 1e-12);
        }
    }
}
TEST_CASE("stylize.pixelfx.nature.drawRockPinnedFacetsTinyStonesWhiteAlphaTransformAndCrop") {
    const std::array<uint32_t, 8> palette{4279373328u, 4280424990u, 4281739824u, 4283186758u,
                                          4284765278u, 4286607484u, 4288582816u, 4291347662u};
    struct Case {
        uint32_t seed;
        bool     explicitCount;
        double   size, angle, alpha;
        bool     white, transformed;
        int      w, h, ox, oy;
        uint32_t expected;
    };
    const Case cases[] = {{11u, false, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {11u, false, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {11u, false, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {11u, false, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {11u, false, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {11u, false, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {11u, false, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {11u, false, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {11u, false, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 2171865308u},
                          {11u, false, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 426533674u},
                          {11u, false, 1.4, 0, 1, true, false, 64, 80, 32, 64, 1349470052u},
                          {11u, false, 1.4, 0, 1, true, true, 32, 48, 16, 32, 1544253332u},
                          {11u, false, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039394118u},
                          {11u, false, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 4254939188u},
                          {11u, false, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3671208069u},
                          {11u, false, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 2248462533u},
                          {11u, false, 3, 0, 0.43, false, false, 64, 80, 32, 64, 3940672586u},
                          {11u, false, 3, 0, 0.43, false, true, 32, 48, 16, 32, 2129610120u},
                          {11u, false, 3, 0, 1, true, false, 64, 80, 32, 64, 2129670889u},
                          {11u, false, 3, 0, 1, true, true, 32, 48, 16, 32, 2855389512u},
                          {11u, false, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 456181989u},
                          {11u, false, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 3063000588u},
                          {11u, false, 3, 0.37, 1, true, false, 64, 80, 32, 64, 1329031004u},
                          {11u, false, 3, 0.37, 1, true, true, 32, 48, 16, 32, 2581080276u},
                          {11u, false, 11, 0, 0.43, false, false, 64, 80, 32, 64, 742848962u},
                          {11u, false, 11, 0, 0.43, false, true, 32, 48, 16, 32, 1908601142u},
                          {11u, false, 11, 0, 1, true, false, 64, 80, 32, 64, 1371765724u},
                          {11u, false, 11, 0, 1, true, true, 32, 48, 16, 32, 1909547509u},
                          {11u, false, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 2007354300u},
                          {11u, false, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 484242133u},
                          {11u, false, 11, 0.37, 1, true, false, 64, 80, 32, 64, 1327326897u},
                          {11u, false, 11, 0.37, 1, true, true, 32, 48, 16, 32, 471949413u},
                          {11u, false, 24, 0, 0.43, false, false, 64, 80, 32, 64, 2281919701u},
                          {11u, false, 24, 0, 0.43, false, true, 32, 48, 16, 32, 671748613u},
                          {11u, false, 24, 0, 1, true, false, 64, 80, 32, 64, 3206932456u},
                          {11u, false, 24, 0, 1, true, true, 32, 48, 16, 32, 711659009u},
                          {11u, false, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 100429921u},
                          {11u, false, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 1301495871u},
                          {11u, false, 24, 0.37, 1, true, false, 64, 80, 32, 64, 2909418612u},
                          {11u, false, 24, 0.37, 1, true, true, 32, 48, 16, 32, 2596643860u},
                          {11u, false, 32, 0, 0.43, false, false, 64, 80, 32, 64, 3184519319u},
                          {11u, false, 32, 0, 0.43, false, true, 32, 48, 16, 32, 3531673136u},
                          {11u, false, 32, 0, 1, true, false, 64, 80, 32, 64, 3041123681u},
                          {11u, false, 32, 0, 1, true, true, 32, 48, 16, 32, 652743232u},
                          {11u, false, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 3564347024u},
                          {11u, false, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 26058594u},
                          {11u, false, 32, 0.37, 1, true, false, 64, 80, 32, 64, 28300677u},
                          {11u, false, 32, 0.37, 1, true, true, 32, 48, 16, 32, 684822497u},
                          {11u, true, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {11u, true, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {11u, true, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {11u, true, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {11u, true, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {11u, true, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {11u, true, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {11u, true, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {11u, true, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {11u, true, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 3146161624u},
                          {11u, true, 1.4, 0, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {11u, true, 1.4, 0, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {11u, true, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {11u, true, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 279935666u},
                          {11u, true, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {11u, true, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {11u, true, 3, 0, 0.43, false, false, 64, 80, 32, 64, 939930058u},
                          {11u, true, 3, 0, 0.43, false, true, 32, 48, 16, 32, 41321950u},
                          {11u, true, 3, 0, 1, true, false, 64, 80, 32, 64, 2209323133u},
                          {11u, true, 3, 0, 1, true, true, 32, 48, 16, 32, 1953744828u},
                          {11u, true, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 930387578u},
                          {11u, true, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 553199027u},
                          {11u, true, 3, 0.37, 1, true, false, 64, 80, 32, 64, 1255557149u},
                          {11u, true, 3, 0.37, 1, true, true, 32, 48, 16, 32, 3960182997u},
                          {11u, true, 11, 0, 0.43, false, false, 64, 80, 32, 64, 889497216u},
                          {11u, true, 11, 0, 0.43, false, true, 32, 48, 16, 32, 2499169928u},
                          {11u, true, 11, 0, 1, true, false, 64, 80, 32, 64, 591185004u},
                          {11u, true, 11, 0, 1, true, true, 32, 48, 16, 32, 2220214321u},
                          {11u, true, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 3275796976u},
                          {11u, true, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 1269093573u},
                          {11u, true, 11, 0.37, 1, true, false, 64, 80, 32, 64, 2017735653u},
                          {11u, true, 11, 0.37, 1, true, true, 32, 48, 16, 32, 585475972u},
                          {11u, true, 24, 0, 0.43, false, false, 64, 80, 32, 64, 1542748268u},
                          {11u, true, 24, 0, 0.43, false, true, 32, 48, 16, 32, 3744627231u},
                          {11u, true, 24, 0, 1, true, false, 64, 80, 32, 64, 2100865144u},
                          {11u, true, 24, 0, 1, true, true, 32, 48, 16, 32, 4255994064u},
                          {11u, true, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 3261068365u},
                          {11u, true, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 1421489298u},
                          {11u, true, 24, 0.37, 1, true, false, 64, 80, 32, 64, 2984254884u},
                          {11u, true, 24, 0.37, 1, true, true, 32, 48, 16, 32, 1281505188u},
                          {11u, true, 32, 0, 0.43, false, false, 64, 80, 32, 64, 907203212u},
                          {11u, true, 32, 0, 0.43, false, true, 32, 48, 16, 32, 3801469507u},
                          {11u, true, 32, 0, 1, true, false, 64, 80, 32, 64, 2173216900u},
                          {11u, true, 32, 0, 1, true, true, 32, 48, 16, 32, 2588847388u},
                          {11u, true, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 3928736916u},
                          {11u, true, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 2492818830u},
                          {11u, true, 32, 0.37, 1, true, false, 64, 80, 32, 64, 1174237044u},
                          {11u, true, 32, 0.37, 1, true, true, 32, 48, 16, 32, 1732476024u},
                          {123u, false, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {123u, false, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {123u, false, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {123u, false, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {123u, false, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {123u, false, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {123u, false, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {123u, false, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {123u, false, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 2171865308u},
                          {123u, false, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 3146161624u},
                          {123u, false, 1.4, 0, 1, true, false, 64, 80, 32, 64, 1349470052u},
                          {123u, false, 1.4, 0, 1, true, true, 32, 48, 16, 32, 1544253332u},
                          {123u, false, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039394118u},
                          {123u, false, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 4254939188u},
                          {123u, false, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3671208069u},
                          {123u, false, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 2248462533u},
                          {123u, false, 3, 0, 0.43, false, false, 64, 80, 32, 64, 3641077766u},
                          {123u, false, 3, 0, 0.43, false, true, 32, 48, 16, 32, 30406318u},
                          {123u, false, 3, 0, 1, true, false, 64, 80, 32, 64, 3571387164u},
                          {123u, false, 3, 0, 1, true, true, 32, 48, 16, 32, 2965384753u},
                          {123u, false, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 3778984659u},
                          {123u, false, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 1308951296u},
                          {123u, false, 3, 0.37, 1, true, false, 64, 80, 32, 64, 2018432200u},
                          {123u, false, 3, 0.37, 1, true, true, 32, 48, 16, 32, 2207890048u},
                          {123u, false, 11, 0, 0.43, false, false, 64, 80, 32, 64, 1035547233u},
                          {123u, false, 11, 0, 0.43, false, true, 32, 48, 16, 32, 2735061524u},
                          {123u, false, 11, 0, 1, true, false, 64, 80, 32, 64, 2396153473u},
                          {123u, false, 11, 0, 1, true, true, 32, 48, 16, 32, 2152141201u},
                          {123u, false, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 2374276352u},
                          {123u, false, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 242588605u},
                          {123u, false, 11, 0.37, 1, true, false, 64, 80, 32, 64, 4005506644u},
                          {123u, false, 11, 0.37, 1, true, true, 32, 48, 16, 32, 2976036693u},
                          {123u, false, 24, 0, 0.43, false, false, 64, 80, 32, 64, 289754360u},
                          {123u, false, 24, 0, 0.43, false, true, 32, 48, 16, 32, 3062759958u},
                          {123u, false, 24, 0, 1, true, false, 64, 80, 32, 64, 3362199052u},
                          {123u, false, 24, 0, 1, true, true, 32, 48, 16, 32, 1176508152u},
                          {123u, false, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 442909739u},
                          {123u, false, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 3341362867u},
                          {123u, false, 24, 0.37, 1, true, false, 64, 80, 32, 64, 1662646772u},
                          {123u, false, 24, 0.37, 1, true, true, 32, 48, 16, 32, 3949178125u},
                          {123u, false, 32, 0, 0.43, false, false, 64, 80, 32, 64, 3864003256u},
                          {123u, false, 32, 0, 0.43, false, true, 32, 48, 16, 32, 4252845197u},
                          {123u, false, 32, 0, 1, true, false, 64, 80, 32, 64, 3869194268u},
                          {123u, false, 32, 0, 1, true, true, 32, 48, 16, 32, 451799608u},
                          {123u, false, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 2347604415u},
                          {123u, false, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 1566162754u},
                          {123u, false, 32, 0.37, 1, true, false, 64, 80, 32, 64, 1270520333u},
                          {123u, false, 32, 0.37, 1, true, true, 32, 48, 16, 32, 4199008285u},
                          {123u, true, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {123u, true, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {123u, true, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {123u, true, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {123u, true, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {123u, true, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {123u, true, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {123u, true, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {123u, true, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {123u, true, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 426533674u},
                          {123u, true, 1.4, 0, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {123u, true, 1.4, 0, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {123u, true, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {123u, true, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 4254939188u},
                          {123u, true, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {123u, true, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {123u, true, 3, 0, 0.43, false, false, 64, 80, 32, 64, 2740170191u},
                          {123u, true, 3, 0, 0.43, false, true, 32, 48, 16, 32, 1315717661u},
                          {123u, true, 3, 0, 1, true, false, 64, 80, 32, 64, 3095942001u},
                          {123u, true, 3, 0, 1, true, true, 32, 48, 16, 32, 1113829072u},
                          {123u, true, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 3722992379u},
                          {123u, true, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 179058033u},
                          {123u, true, 3, 0.37, 1, true, false, 64, 80, 32, 64, 2245272225u},
                          {123u, true, 3, 0.37, 1, true, true, 32, 48, 16, 32, 3800202880u},
                          {123u, true, 11, 0, 0.43, false, false, 64, 80, 32, 64, 2495252005u},
                          {123u, true, 11, 0, 0.43, false, true, 32, 48, 16, 32, 185025373u},
                          {123u, true, 11, 0, 1, true, false, 64, 80, 32, 64, 3572225784u},
                          {123u, true, 11, 0, 1, true, true, 32, 48, 16, 32, 2316897233u},
                          {123u, true, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 4157798519u},
                          {123u, true, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 2160731852u},
                          {123u, true, 11, 0.37, 1, true, false, 64, 80, 32, 64, 3195751008u},
                          {123u, true, 11, 0.37, 1, true, true, 32, 48, 16, 32, 532697321u},
                          {123u, true, 24, 0, 0.43, false, false, 64, 80, 32, 64, 2706784976u},
                          {123u, true, 24, 0, 0.43, false, true, 32, 48, 16, 32, 1315340100u},
                          {123u, true, 24, 0, 1, true, false, 64, 80, 32, 64, 934185493u},
                          {123u, true, 24, 0, 1, true, true, 32, 48, 16, 32, 2718824225u},
                          {123u, true, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 2179677818u},
                          {123u, true, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 3797921803u},
                          {123u, true, 24, 0.37, 1, true, false, 64, 80, 32, 64, 1630338753u},
                          {123u, true, 24, 0.37, 1, true, true, 32, 48, 16, 32, 2486431505u},
                          {123u, true, 32, 0, 0.43, false, false, 64, 80, 32, 64, 2485575173u},
                          {123u, true, 32, 0, 0.43, false, true, 32, 48, 16, 32, 3804050660u},
                          {123u, true, 32, 0, 1, true, false, 64, 80, 32, 64, 3453153536u},
                          {123u, true, 32, 0, 1, true, true, 32, 48, 16, 32, 181032973u},
                          {123u, true, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 4172875651u},
                          {123u, true, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 2108602024u},
                          {123u, true, 32, 0.37, 1, true, false, 64, 80, 32, 64, 2441940300u},
                          {123u, true, 32, 0.37, 1, true, true, 32, 48, 16, 32, 2103252948u},
                          {0u, false, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {0u, false, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {0u, false, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {0u, false, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {0u, false, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {0u, false, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {0u, false, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {0u, false, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {0u, false, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 2464937521u},
                          {0u, false, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 426533674u},
                          {0u, false, 1.4, 0, 1, true, false, 64, 80, 32, 64, 910263845u},
                          {0u, false, 1.4, 0, 1, true, true, 32, 48, 16, 32, 211507577u},
                          {0u, false, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039394118u},
                          {0u, false, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 4254939188u},
                          {0u, false, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3671208069u},
                          {0u, false, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 2248462533u},
                          {0u, false, 3, 0, 0.43, false, false, 64, 80, 32, 64, 3254766827u},
                          {0u, false, 3, 0, 0.43, false, true, 32, 48, 16, 32, 1607761024u},
                          {0u, false, 3, 0, 1, true, false, 64, 80, 32, 64, 4089865552u},
                          {0u, false, 3, 0, 1, true, true, 32, 48, 16, 32, 60198197u},
                          {0u, false, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 1577922040u},
                          {0u, false, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 2941798651u},
                          {0u, false, 3, 0.37, 1, true, false, 64, 80, 32, 64, 1857811296u},
                          {0u, false, 3, 0.37, 1, true, true, 32, 48, 16, 32, 902406469u},
                          {0u, false, 11, 0, 0.43, false, false, 64, 80, 32, 64, 4125545621u},
                          {0u, false, 11, 0, 0.43, false, true, 32, 48, 16, 32, 2668999059u},
                          {0u, false, 11, 0, 1, true, false, 64, 80, 32, 64, 2510272872u},
                          {0u, false, 11, 0, 1, true, true, 32, 48, 16, 32, 1142330437u},
                          {0u, false, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 1011997138u},
                          {0u, false, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 2640591482u},
                          {0u, false, 11, 0.37, 1, true, false, 64, 80, 32, 64, 715746972u},
                          {0u, false, 11, 0.37, 1, true, true, 32, 48, 16, 32, 357609400u},
                          {0u, false, 24, 0, 0.43, false, false, 64, 80, 32, 64, 3791246972u},
                          {0u, false, 24, 0, 0.43, false, true, 32, 48, 16, 32, 3398821762u},
                          {0u, false, 24, 0, 1, true, false, 64, 80, 32, 64, 2560755157u},
                          {0u, false, 24, 0, 1, true, true, 32, 48, 16, 32, 2818573721u},
                          {0u, false, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 2078928224u},
                          {0u, false, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 3860141040u},
                          {0u, false, 24, 0.37, 1, true, false, 64, 80, 32, 64, 1487944625u},
                          {0u, false, 24, 0.37, 1, true, true, 32, 48, 16, 32, 2447705113u},
                          {0u, false, 32, 0, 0.43, false, false, 64, 80, 32, 64, 2755720753u},
                          {0u, false, 32, 0, 0.43, false, true, 32, 48, 16, 32, 179370036u},
                          {0u, false, 32, 0, 1, true, false, 64, 80, 32, 64, 3293597093u},
                          {0u, false, 32, 0, 1, true, true, 32, 48, 16, 32, 403088761u},
                          {0u, false, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 104081245u},
                          {0u, false, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 2248175436u},
                          {0u, false, 32, 0.37, 1, true, false, 64, 80, 32, 64, 1077062629u},
                          {0u, false, 32, 0.37, 1, true, true, 32, 48, 16, 32, 4153710981u},
                          {0u, true, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {0u, true, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {0u, true, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {0u, true, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {0u, true, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {0u, true, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {0u, true, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {0u, true, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {0u, true, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {0u, true, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 3146161624u},
                          {0u, true, 1.4, 0, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {0u, true, 1.4, 0, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {0u, true, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {0u, true, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 4254939188u},
                          {0u, true, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {0u, true, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {0u, true, 3, 0, 0.43, false, false, 64, 80, 32, 64, 4190817132u},
                          {0u, true, 3, 0, 0.43, false, true, 32, 48, 16, 32, 1831644728u},
                          {0u, true, 3, 0, 1, true, false, 64, 80, 32, 64, 3235547005u},
                          {0u, true, 3, 0, 1, true, true, 32, 48, 16, 32, 2137822268u},
                          {0u, true, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 4004325659u},
                          {0u, true, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 1739876179u},
                          {0u, true, 3, 0.37, 1, true, false, 64, 80, 32, 64, 259504820u},
                          {0u, true, 3, 0.37, 1, true, true, 32, 48, 16, 32, 283820988u},
                          {0u, true, 11, 0, 0.43, false, false, 64, 80, 32, 64, 2714117707u},
                          {0u, true, 11, 0, 0.43, false, true, 32, 48, 16, 32, 2885895472u},
                          {0u, true, 11, 0, 1, true, false, 64, 80, 32, 64, 3298280344u},
                          {0u, true, 11, 0, 1, true, true, 32, 48, 16, 32, 2385669765u},
                          {0u, true, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 4271741509u},
                          {0u, true, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 1001253607u},
                          {0u, true, 11, 0.37, 1, true, false, 64, 80, 32, 64, 3690033064u},
                          {0u, true, 11, 0.37, 1, true, true, 32, 48, 16, 32, 3182881777u},
                          {0u, true, 24, 0, 0.43, false, false, 64, 80, 32, 64, 1288233895u},
                          {0u, true, 24, 0, 0.43, false, true, 32, 48, 16, 32, 1653135344u},
                          {0u, true, 24, 0, 1, true, false, 64, 80, 32, 64, 542964841u},
                          {0u, true, 24, 0, 1, true, true, 32, 48, 16, 32, 985537525u},
                          {0u, true, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 1636901783u},
                          {0u, true, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 1395352731u},
                          {0u, true, 24, 0.37, 1, true, false, 64, 80, 32, 64, 1055918932u},
                          {0u, true, 24, 0.37, 1, true, true, 32, 48, 16, 32, 3366660221u},
                          {0u, true, 32, 0, 0.43, false, false, 64, 80, 32, 64, 2776587942u},
                          {0u, true, 32, 0, 0.43, false, true, 32, 48, 16, 32, 3941095979u},
                          {0u, true, 32, 0, 1, true, false, 64, 80, 32, 64, 198736405u},
                          {0u, true, 32, 0, 1, true, true, 32, 48, 16, 32, 2143815668u},
                          {0u, true, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 3184511143u},
                          {0u, true, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 1912604910u},
                          {0u, true, 32, 0.37, 1, true, false, 64, 80, 32, 64, 3204469144u},
                          {0u, true, 32, 0.37, 1, true, true, 32, 48, 16, 32, 3082401809u},
                          {4294967295u, false, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {4294967295u, false, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {4294967295u, false, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {4294967295u, false, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {4294967295u, false, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {4294967295u, false, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {4294967295u, false, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {4294967295u, false, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {4294967295u, false, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 2436654410u},
                          {4294967295u, false, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 1137778274u},
                          {4294967295u, false, 1.4, 0, 1, true, false, 64, 80, 32, 64, 2333875820u},
                          {4294967295u, false, 1.4, 0, 1, true, true, 32, 48, 16, 32, 1132962928u},
                          {4294967295u, false, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 2171865308u},
                          {4294967295u, false, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 350789192u},
                          {4294967295u, false, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 1349470052u},
                          {4294967295u, false, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 1544253332u},
                          {4294967295u, false, 3, 0, 0.43, false, false, 64, 80, 32, 64, 709156273u},
                          {4294967295u, false, 3, 0, 0.43, false, true, 32, 48, 16, 32, 2487053266u},
                          {4294967295u, false, 3, 0, 1, true, false, 64, 80, 32, 64, 3367295408u},
                          {4294967295u, false, 3, 0, 1, true, true, 32, 48, 16, 32, 1738104136u},
                          {4294967295u, false, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 1688046963u},
                          {4294967295u, false, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 3394124970u},
                          {4294967295u, false, 3, 0.37, 1, true, false, 64, 80, 32, 64, 1802172389u},
                          {4294967295u, false, 3, 0.37, 1, true, true, 32, 48, 16, 32, 436017476u},
                          {4294967295u, false, 11, 0, 0.43, false, false, 64, 80, 32, 64, 399130359u},
                          {4294967295u, false, 11, 0, 0.43, false, true, 32, 48, 16, 32, 256042928u},
                          {4294967295u, false, 11, 0, 1, true, false, 64, 80, 32, 64, 257179905u},
                          {4294967295u, false, 11, 0, 1, true, true, 32, 48, 16, 32, 2122388488u},
                          {4294967295u, false, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 3779758820u},
                          {4294967295u, false, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 848785249u},
                          {4294967295u, false, 11, 0.37, 1, true, false, 64, 80, 32, 64, 3792180425u},
                          {4294967295u, false, 11, 0.37, 1, true, true, 32, 48, 16, 32, 3002037920u},
                          {4294967295u, false, 24, 0, 0.43, false, false, 64, 80, 32, 64, 719866912u},
                          {4294967295u, false, 24, 0, 0.43, false, true, 32, 48, 16, 32, 1352060188u},
                          {4294967295u, false, 24, 0, 1, true, false, 64, 80, 32, 64, 1056625036u},
                          {4294967295u, false, 24, 0, 1, true, true, 32, 48, 16, 32, 730733681u},
                          {4294967295u, false, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 508537186u},
                          {4294967295u, false, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 759660754u},
                          {4294967295u, false, 24, 0.37, 1, true, false, 64, 80, 32, 64, 1569187825u},
                          {4294967295u, false, 24, 0.37, 1, true, true, 32, 48, 16, 32, 3179913565u},
                          {4294967295u, false, 32, 0, 0.43, false, false, 64, 80, 32, 64, 262052934u},
                          {4294967295u, false, 32, 0, 0.43, false, true, 32, 48, 16, 32, 1421180652u},
                          {4294967295u, false, 32, 0, 1, true, false, 64, 80, 32, 64, 778026513u},
                          {4294967295u, false, 32, 0, 1, true, true, 32, 48, 16, 32, 2103252948u},
                          {4294967295u, false, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 3014618445u},
                          {4294967295u, false, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 1408932236u},
                          {4294967295u, false, 32, 0.37, 1, true, false, 64, 80, 32, 64, 3601034165u},
                          {4294967295u, false, 32, 0.37, 1, true, true, 32, 48, 16, 32, 2103252948u},
                          {4294967295u, true, 1.2, 0, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {4294967295u, true, 1.2, 0, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {4294967295u, true, 1.2, 0, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {4294967295u, true, 1.2, 0, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {4294967295u, true, 1.2, 0.37, 0.43, false, false, 64, 80, 32, 64, 1039753732u},
                          {4294967295u, true, 1.2, 0.37, 0.43, false, true, 32, 48, 16, 32, 4170413084u},
                          {4294967295u, true, 1.2, 0.37, 1, true, false, 64, 80, 32, 64, 122896057u},
                          {4294967295u, true, 1.2, 0.37, 1, true, true, 32, 48, 16, 32, 3836582589u},
                          {4294967295u, true, 1.4, 0, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {4294967295u, true, 1.4, 0, 0.43, false, true, 32, 48, 16, 32, 3146161624u},
                          {4294967295u, true, 1.4, 0, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {4294967295u, true, 1.4, 0, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {4294967295u, true, 1.4, 0.37, 0.43, false, false, 64, 80, 32, 64, 79072951u},
                          {4294967295u, true, 1.4, 0.37, 0.43, false, true, 32, 48, 16, 32, 3146161624u},
                          {4294967295u, true, 1.4, 0.37, 1, true, false, 64, 80, 32, 64, 3740283824u},
                          {4294967295u, true, 1.4, 0.37, 1, true, true, 32, 48, 16, 32, 3331081652u},
                          {4294967295u, true, 3, 0, 0.43, false, false, 64, 80, 32, 64, 3877989642u},
                          {4294967295u, true, 3, 0, 0.43, false, true, 32, 48, 16, 32, 886195444u},
                          {4294967295u, true, 3, 0, 1, true, false, 64, 80, 32, 64, 222566804u},
                          {4294967295u, true, 3, 0, 1, true, true, 32, 48, 16, 32, 3196847276u},
                          {4294967295u, true, 3, 0.37, 0.43, false, false, 64, 80, 32, 64, 414371571u},
                          {4294967295u, true, 3, 0.37, 0.43, false, true, 32, 48, 16, 32, 2558008981u},
                          {4294967295u, true, 3, 0.37, 1, true, false, 64, 80, 32, 64, 4015175313u},
                          {4294967295u, true, 3, 0.37, 1, true, true, 32, 48, 16, 32, 2205092569u},
                          {4294967295u, true, 11, 0, 0.43, false, false, 64, 80, 32, 64, 4179174204u},
                          {4294967295u, true, 11, 0, 0.43, false, true, 32, 48, 16, 32, 3135972796u},
                          {4294967295u, true, 11, 0, 1, true, false, 64, 80, 32, 64, 1547924736u},
                          {4294967295u, true, 11, 0, 1, true, true, 32, 48, 16, 32, 273801469u},
                          {4294967295u, true, 11, 0.37, 0.43, false, false, 64, 80, 32, 64, 1240281110u},
                          {4294967295u, true, 11, 0.37, 0.43, false, true, 32, 48, 16, 32, 871935329u},
                          {4294967295u, true, 11, 0.37, 1, true, false, 64, 80, 32, 64, 3503180036u},
                          {4294967295u, true, 11, 0.37, 1, true, true, 32, 48, 16, 32, 2904675661u},
                          {4294967295u, true, 24, 0, 0.43, false, false, 64, 80, 32, 64, 1024256661u},
                          {4294967295u, true, 24, 0, 0.43, false, true, 32, 48, 16, 32, 1900971886u},
                          {4294967295u, true, 24, 0, 1, true, false, 64, 80, 32, 64, 1210641004u},
                          {4294967295u, true, 24, 0, 1, true, true, 32, 48, 16, 32, 2737477845u},
                          {4294967295u, true, 24, 0.37, 0.43, false, false, 64, 80, 32, 64, 2149186028u},
                          {4294967295u, true, 24, 0.37, 0.43, false, true, 32, 48, 16, 32, 1765412682u},
                          {4294967295u, true, 24, 0.37, 1, true, false, 64, 80, 32, 64, 3143174552u},
                          {4294967295u, true, 24, 0.37, 1, true, true, 32, 48, 16, 32, 1186757653u},
                          {4294967295u, true, 32, 0, 0.43, false, false, 64, 80, 32, 64, 3110023618u},
                          {4294967295u, true, 32, 0, 0.43, false, true, 32, 48, 16, 32, 1149795370u},
                          {4294967295u, true, 32, 0, 1, true, false, 64, 80, 32, 64, 3762309124u},
                          {4294967295u, true, 32, 0, 1, true, true, 32, 48, 16, 32, 3948700700u},
                          {4294967295u, true, 32, 0.37, 0.43, false, false, 64, 80, 32, 64, 3731967734u},
                          {4294967295u, true, 32, 0.37, 0.43, false, true, 32, 48, 16, 32, 3911695260u},
                          {4294967295u, true, 32, 0.37, 1, true, false, 64, 80, 32, 64, 1792470680u},
                          {4294967295u, true, 32, 0.37, 1, true, true, 32, 48, 16, 32, 2822894136u}};
    for (const auto& e : cases) {
        Rng                   rng(e.seed);
        const auto            shape = e.explicitCount ? nature::stoneShape(rng, 8) : nature::randomStoneShape(rng);
        const PixelFxInfo     info{PixelFxKind::AcidSplash, "nature/rock-fixture", e.w, e.h, e.ox, e.oy, 1, false};
        std::vector<uint32_t> color(size_t(e.w * e.h)), light(color.size(), 0xff123456u);
        Paint                 p{info, color, light, 1};
        if (e.transformed) p.place(7, -3, true, .2);
        nature::drawRock(p, 3.25, -20.75, e.size, e.angle, shape, palette, e.alpha, e.white);
        REQUIRE(checksum(color) == e.expected);
        REQUIRE(std::all_of(light.begin(), light.end(), [](uint32_t c) { return c == 0xff123456u; }));
    }
}


TEST_CASE("stylize.pixelfx.nature.boulderPinnedFlightImpactChunksAndDust") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2167603110u, 384640245u},
                                {0.058333333333333334, 683304783u, 2165067397u},
                                {0.11666666666666667, 3444474980u, 734808377u},
                                {0.175, 3621601158u, 1826837329u},
                                {0.23333333333333334, 43717418u, 1318820631u},
                                {0.2916666666666667, 2315732664u, 4211107965u},
                                {0.3416666666666667, 738457933u, 3250366382u},
                                {0.4, 2889182401u, 2238342469u},
                                {0.4583333333333333, 3156312840u, 755335249u},
                                {0.5166666666666667, 1451153042u, 299045483u},
                                {0.575, 2450674503u, 1772540197u},
                                {0.6333333333333333, 2957384664u, 442881844u},
                                {0.6916666666666667, 148229442u, 4127543065u},
                                {0.75, 2183456460u, 2184408476u},
                                {0.8083333333333333, 1845274891u, 2469485872u},
                                {0.8666666666666667, 2170139923u, 263618877u},
                                {0.9166666666666666, 2487975658u, 173587409u},
                                {0.975, 4183651456u, 1719413189u},
                                {1.0333333333333334, 2382174127u, 1719413189u},
                                {1.0916666666666666, 3710501932u, 1719413189u},
                                {1.15, 575389009u, 1719413189u},
                                {1.2083333333333333, 3531451441u, 1719413189u},
                                {1.2666666666666666, 3809569400u, 1719413189u},
                                {1.325, 1953152594u, 1719413189u},
                                {1.3833333333333333, 1237862495u, 1719413189u},
                                {1.4333333333333333, 1520735489u, 1719413189u},
                                {1.4916666666666667, 1308383859u, 1719413189u},
                                {1.55, 454741674u, 1719413189u},
                                {1.6083333333333334, 4155927460u, 1719413189u},
                                {1.6666666666666667, 979531415u, 1719413189u},
                                {1.725, 3250083068u, 1719413189u},
                                {1.7833333333333334, 3200487188u, 1719413189u},
                                {1.8416666666666666, 3364432424u, 1719413189u},
                                {1.9, 617201854u, 1719413189u},
                                {1.9583333333333333, 3968591018u, 1719413189u},
                                {2.0166666666666666, 1458674506u, 1719413189u},
                                {2.066666666666667, 2147051983u, 1719413189u},
                                {2.125, 2944124945u, 1719413189u},
                                {2.183333333333333, 695787141u, 1719413189u},
                                {2.2416666666666667, 1677557506u, 1719413189u},
                                {2.3, 1719413189u, 1719413189u}};
    PixelFx         fx(PixelFxKind::Boulder);
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.boulderOwningOptionsAtomicBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::Boulder);
    auto    s = fx.settings();
    REQUIRE(s.nature.size == 11);
    REQUIRE(s.nature.sparks.size() == 4);
    s.nature.size   = 16;
    s.nature.sparks = {0xff112233u};
    auto prepared   = PixelFx::prepareConfiguration(PixelFxKind::Boulder, s);
    REQUIRE(prepared);
    s.nature.sparks[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.size == 16);
    REQUIRE(owned.nature.sparks.size() == 4);
    for (auto c : owned.nature.sparks) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Boulder, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double size :
         {0., .49, 32.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        s.nature.size = size;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Boulder, s));
        REQUIRE(fx.configurationValue() == value);
    }
    s.nature.size = 11;
    for (size_t count : {size_t(0), size_t(65)}) {
        s.nature.sparks.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Boulder, s));
    }
    s.nature.sparks = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Boulder, s));
    for (double size : {.5, 32.}) {
        s.nature.size   = size;
        s.nature.sparks = {0xff112233u};
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Boulder, s));
    }
}
TEST_CASE("stylize.pixelfx.nature.boulderLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Boulder);
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
TEST_CASE("stylize.pixelfx.nature.boulderPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Boulder), reference(PixelFxKind::Boulder);
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

TEST_CASE("stylize.pixelfx.nature.boulderStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::Boulder);
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


TEST_CASE("stylize.pixelfx.nature.earthSpikePinnedEruptionSplitRubbleSinkAndScars") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2987141954u, 1779407813u},
                                {0.008333333333333333, 4204912392u, 1779407813u},
                                {0.1, 4045771388u, 19871990u},
                                {0.3333333333333333, 1932192190u, 43271414u},
                                {0.3416666666666667, 626802965u, 2489722634u},
                                {0.35, 1189949122u, 1245160592u},
                                {0.38333333333333336, 3271857219u, 1341338405u},
                                {0.39166666666666666, 4169925997u, 593625137u},
                                {0.425, 2318682408u, 3479457970u},
                                {0.43333333333333335, 3022851139u, 229763134u},
                                {0.5, 3417490563u, 266299551u},
                                {0.6, 2983932580u, 1903258929u},
                                {1, 515947454u, 1854864240u},
                                {1.175, 3445823209u, 3256541749u},
                                {1.1833333333333333, 1356880171u, 1386780320u},
                                {1.3916666666666666, 443559256u, 2355134218u},
                                {1.4, 3330962301u, 1314054480u},
                                {1.4083333333333334, 4233173347u, 612148416u},
                                {1.4916666666666667, 1493065577u, 1981248598u},
                                {1.5, 4238622528u, 1809220124u},
                                {1.5083333333333333, 1087972302u, 3635854856u},
                                {1.575, 3817088366u, 623939393u},
                                {1.5833333333333333, 1618250837u, 2613961182u},
                                {1.6, 4176846731u, 1337250890u},
                                {1.7, 1291819727u, 1575680515u},
                                {1.8, 1800118154u, 2593975981u},
                                {1.9, 3794982817u, 336305772u},
                                {2, 1126876884u, 1779407813u},
                                {2.2, 4040628012u, 1779407813u},
                                {2.4, 131475061u, 1779407813u},
                                {2.6, 92979530u, 1779407813u},
                                {2.9, 1958689757u, 1779407813u},
                                {3.1, 2178785305u, 1779407813u},
                                {3.4, 1779407813u, 1779407813u},
                                {4, 1779407813u, 1779407813u},
                                {5, 1779407813u, 1779407813u}};
    PixelFx         fx(PixelFxKind::EarthSpike);
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.earthSpikeOwningOptionsAtomicBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::EarthSpike);
    auto    s = fx.settings();
    REQUIRE(s.nature.height == 62);
    REQUIRE(s.nature.glow.size() == 5);
    s.nature.height = 78;
    s.nature.glow   = {0xff112233u};
    auto prepared   = PixelFx::prepareConfiguration(PixelFxKind::EarthSpike, s);
    REQUIRE(prepared);
    s.nature.glow[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.height == 78);
    REQUIRE(owned.nature.glow.size() == 5);
    for (auto c : owned.nature.glow) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::EarthSpike, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double height :
         {0., .99, 256.01, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        s.nature.height = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::EarthSpike, s));
        REQUIRE(fx.configurationValue() == value);
    }
    s.nature.height = 62;
    for (size_t count : {size_t(0), size_t(65)}) {
        s.nature.glow.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::EarthSpike, s));
    }
    s.nature.glow = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::EarthSpike, s));
    for (double height : {1., 256.}) {
        s.nature.height = height;
        s.nature.glow   = {0xff112233u};
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::EarthSpike, s));
    }
}
TEST_CASE("stylize.pixelfx.nature.earthSpikeLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::EarthSpike);
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
TEST_CASE("stylize.pixelfx.nature.earthSpikePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::EarthSpike), reference(PixelFxKind::EarthSpike);
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

TEST_CASE("stylize.pixelfx.nature.earthSpikeStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::EarthSpike);
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


TEST_CASE("stylize.pixelfx.nature.healingBloomPinnedSproutBudBloomLiftWiltAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 3798751173u, 3798751173u},
                                {0.08333333333333333, 1629595503u, 1162967405u},
                                {0.16666666666666666, 2425369126u, 4118335141u},
                                {0.25, 3470183709u, 1433155301u},
                                {0.3333333333333333, 3242704161u, 883217567u},
                                {0.4166666666666667, 4108760508u, 3467229380u},
                                {0.49166666666666664, 896254560u, 3562412381u},
                                {0.575, 905653754u, 435445092u},
                                {0.6583333333333333, 3607556542u, 2789322561u},
                                {0.7416666666666667, 4164417060u, 1244732813u},
                                {0.825, 2592976935u, 3617800951u},
                                {0.9083333333333333, 3400647717u, 2827192823u},
                                {0.9916666666666667, 316049747u, 184520653u},
                                {1.075, 1193438168u, 1805048091u},
                                {1.1583333333333334, 2750087925u, 2978148602u},
                                {1.2416666666666667, 2934529724u, 2132848141u},
                                {1.3166666666666667, 2515902594u, 3109692512u},
                                {1.4, 672401094u, 1538286264u},
                                {1.4833333333333334, 579510498u, 1679909398u},
                                {1.5666666666666667, 967312036u, 1374044876u},
                                {1.65, 1396927497u, 3334822534u},
                                {1.7333333333333334, 813103716u, 2586279666u},
                                {1.8166666666666667, 1153165886u, 1656942661u},
                                {1.9, 556284534u, 577208153u},
                                {1.9833333333333334, 567477258u, 3214590234u},
                                {2.066666666666667, 74100200u, 2759370481u},
                                {2.1416666666666666, 1527603882u, 1556678920u},
                                {2.225, 1143309206u, 293783039u},
                                {2.308333333333333, 4173127604u, 1546236341u},
                                {2.3916666666666666, 3269796420u, 3582796350u},
                                {2.475, 1926519183u, 2960583555u},
                                {2.558333333333333, 2821113575u, 112392497u},
                                {2.6416666666666666, 648025790u, 1932404327u},
                                {2.725, 2193384366u, 185721042u},
                                {2.808333333333333, 909467253u, 136570364u},
                                {2.8916666666666666, 2532190032u, 2433728822u},
                                {2.966666666666667, 3367959121u, 3437609537u},
                                {3.05, 3648085466u, 3445847504u},
                                {3.1333333333333333, 2089623407u, 1015570415u},
                                {3.216666666666667, 1486312917u, 549956822u},
                                {3.3, 3798751173u, 3798751173u}};
    PixelFx         fx(PixelFxKind::HealingBloom);
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.healingBloomOwningPetalHeartAtomicBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::HealingBloom);
    auto    s = fx.settings();
    REQUIRE(s.nature.petal.size() == 4);
    REQUIRE(s.nature.heart.size() == 4);
    s.nature.petal = {0xff112233u};
    s.nature.heart = {0xff445566u};
    auto prepared  = PixelFx::prepareConfiguration(PixelFxKind::HealingBloom, s);
    REQUIRE(prepared);
    s.nature.petal[0] = 0xff334455u;
    s.nature.heart[0] = 0xff778899u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.petal.size() == 4);
    REQUIRE(owned.nature.heart.size() == 4);
    for (auto c : owned.nature.petal) REQUIRE(c == 0xff112233u);
    for (auto c : owned.nature.heart) REQUIRE(c == 0xff445566u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HealingBloom, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (auto member : {&PixelFxNatureSettings::petal, &PixelFxNatureSettings::heart}) {
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = owned;
            (bad.nature.*member).assign(count, 0xff112233u);
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HealingBloom, bad));
            REQUIRE(fx.configurationValue() == value);
        }
        auto bad           = owned;
        bad.nature.*member = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HealingBloom, bad));
        bad.nature.*member = std::vector<uint32_t>(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::HealingBloom, bad));
    }
}
TEST_CASE("stylize.pixelfx.nature.healingBloomNonfiniteGroundGlowSourceBitwiseContract") {
    const PixelFxInfo     info{PixelFxKind::HealingBloom, "nature/nonfinite-glow", 9, 9, 4, 4, 1, false};
    std::vector<uint32_t> color(81), light(81);
    Paint                 p{info, color, light, 1};
    struct Case {
        double   k;
        uint32_t color, direct, glow;
    };
    const Case cases[] = {{std::numeric_limits<double>::quiet_NaN(), 0xffffffffu, 4278190080u, 144301019u},
                          {std::numeric_limits<double>::infinity(), 0xff0000ffu, 4278190335u, 1216977283u},
                          {-std::numeric_limits<double>::infinity(), 0xffffffffu, 4280431428u, 4201954779u},
                          {.5, 0xff204080u, 4281488260u, 3583443155u}};
    for (const auto& q : cases) {
        std::fill(light.begin(), light.end(), 0xff223344u);
        p.add(0, 0, q.color, q.k);
        REQUIRE(light[40] == q.direct);
        std::fill(light.begin(), light.end(), 0xff223344u);
        p.glow(0, 0, 4, q.color, q.k);
        REQUIRE(checksum(light) == q.glow);
    }
}
TEST_CASE("stylize.pixelfx.nature.healingBloomLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::HealingBloom);
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
TEST_CASE("stylize.pixelfx.nature.healingBloomPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::HealingBloom), reference(PixelFxKind::HealingBloom);
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

TEST_CASE("stylize.pixelfx.nature.healingBloomStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::HealingBloom);
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


TEST_CASE("stylize.pixelfx.nature.leafStormPinnedWindLeavesSwirlAndLoop") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 1723097708u, 1418301524u},    {0.05, 1563072754u, 3415949070u}, {0.1, 96103152u, 3020892401u},
        {0.15, 3686730702u, 898881067u},  {0.2, 1351502618u, 3358511556u},  {0.25, 4207013570u, 712240746u},
        {0.3, 3418861792u, 515704034u},   {0.35, 2978346394u, 544241710u},  {0.4, 1857598713u, 2908336042u},
        {0.45, 1857786892u, 4203054843u}, {0.5, 4102031118u, 4256640454u},  {0.55, 3017579036u, 2014569232u},
        {0.6, 733720160u, 2510176533u},   {0.65, 1194553277u, 442610144u},  {0.7, 402417446u, 1017386596u},
        {0.75, 418341652u, 3577581483u},  {0.8, 3761993210u, 1964887561u},  {0.85, 2908563892u, 2081056699u},
        {0.9, 3196329468u, 2863591035u},  {0.95, 2244614207u, 2853580993u}, {1, 1751675691u, 228459103u},
        {1.05, 388336147u, 3432760262u},  {1.1, 1406985696u, 2329651832u},  {1.15, 3603218024u, 3690251608u},
        {1.2, 2415197187u, 3062731361u},  {1.25, 3667506386u, 1502403293u}, {1.3, 336469838u, 584688458u},
        {1.35, 1222468579u, 3624717642u}, {1.4, 1054927206u, 1813504384u},  {1.45, 2462483876u, 2688509118u},
        {1.5, 1516614924u, 3703207622u},  {1.55, 126058322u, 1564481884u},  {1.6, 4209467404u, 3262720832u},
        {1.65, 2947186304u, 2656616372u}, {1.7, 3154777558u, 2925507890u},  {1.75, 1154258978u, 3066316460u},
        {1.8, 2237455807u, 1137220737u},  {1.85, 2217624066u, 1019833081u}, {1.9, 1078051849u, 1440620543u},
        {1.95, 798705594u, 2072657864u},  {2, 4017500627u, 4266050194u},    {2.05, 3480325499u, 1013196438u},
        {2.1, 3095844676u, 2028580276u},  {2.15, 2998088504u, 2491279595u}, {2.2, 3734935641u, 1036773845u},
        {2.25, 1490999506u, 2848078915u}, {2.3, 2476865695u, 3125427648u},  {2.35, 2828759541u, 1788052688u},
        {2.4, 1723097708u, 1418301524u}};
    PixelFx fx(PixelFxKind::LeafStorm);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
    fx.render(0);
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    for (double t : {2.4, 4.8, 9.6}) {
        fx.render(t);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.nature.leafStormOwningLeavesAtomicBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::LeafStorm);
    auto    s = fx.settings();
    REQUIRE(s.palette.size() == 6);
    REQUIRE(s.nature.leaves.size() == 12);
    s.nature.leaves = {0xff112233u};
    auto prepared   = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s);
    REQUIRE(prepared);
    s.nature.leaves[0] = 0xff334455u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.leaves.size() == 12);
    for (auto c : owned.nature.leaves) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::LeafStorm, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        s.nature.leaves.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s));
        REQUIRE(fx.configurationValue() == value);
    }
    s.nature.leaves = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s));
    s.nature.leaves.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s));
}
TEST_CASE("stylize.pixelfx.nature.leafStormStockNoneCustomBodyOwnership") {
    PixelFx fx(PixelFxKind::LeafStorm);
    REQUIRE(fx.settings().actor.source == PixelFxActorSource::Dummy);
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    REQUIRE(checksum(body) == 3422433007u);
    auto s         = fx.settings();
    s.actor.source = PixelFxActorSource::Hero;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(checksum(body) == 1190551124u);
    s.actor        = {};
    s.actor.source = PixelFxActorSource::Custom;
    s.actor.width  = 4;
    s.actor.height = 4;
    s.actor.x      = -2;
    s.actor.y      = -4;
    s.actor.head   = {0, -4};
    s.actor.chest  = {0, -2};
    s.actor.mask.assign(16, 1);
    s.actor.pixels.assign(16, 0xff112233u);
    p = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s);
    REQUIRE(p);
    s.actor.pixels[0] = 0xff445566u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::count(body.begin(), body.end(), 0xff112233u) == 16);
    const auto value = fx.configurationValue();
    s.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s));
    REQUIRE(fx.configurationValue() == value);
    s.actor = {};
    p       = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::all_of(body.begin(), body.end(), [](uint32_t c) { return c == 0; }));
    REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
}
TEST_CASE("stylize.pixelfx.nature.leafStormPinnedBackBodyFrontComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{1, 0, 2539918005u, 1418301524u},
                                {1, 0.008333333333333333, 1160344221u, 4122643884u},
                                {1, 0.6, 3936079586u, 2510176533u},
                                {1, 1.2, 1050916872u, 3062731361u},
                                {1, 1.8, 581523629u, 1137220737u},
                                {1, 2.3916666666666666, 3508499508u, 4185386084u},
                                {1, 2.4, 2539918005u, 1418301524u},
                                {2, 0, 3663353846u, 1418301524u},
                                {2, 0.008333333333333333, 3459337323u, 4122643884u},
                                {2, 0.6, 839927498u, 2510176533u},
                                {2, 1.2, 1372250874u, 3062731361u},
                                {2, 1.8, 821671742u, 1137220737u},
                                {2, 2.3916666666666666, 2580219972u, 4185386084u},
                                {2, 2.4, 3663353846u, 1418301524u}};
    PixelFx         fx(PixelFxKind::LeafStorm);
    const auto      info = fx.info();
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto p         = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, s);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        fx.render(f.time);
        std::vector<uint32_t> color(fx.color().size()), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.leafStormPinnedPoseTrackInputs") {
    auto document = eve::Value::fromJson(
        R"track({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[0,{"turn":1.1,"crouch":0,"fx":-13,"fy":-37,"bx":13,"by":-37,"ffx":-3,"fbx":4}],[0.6,{"turn":1.1,"crouch":0,"fx":-13,"fy":-40,"bx":13,"by":-33,"ffx":-3,"fbx":4,"lean":2}],[1.2,{"turn":1.1,"crouch":-1,"fx":-13,"fy":-37,"bx":13,"by":-37,"ffx":-3,"fbx":4}],[1.8,{"turn":1.1,"crouch":0,"fx":-13,"fy":-33,"bx":13,"by":-40,"ffx":-3,"fbx":4,"lean":-2}],[2.4,{"turn":1.1,"crouch":0,"fx":-13,"fy":-37,"bx":13,"by":-37,"ffx":-3,"fbx":4}]]})track");
    REQUIRE(document);
    auto track = rig::PoseTrack::prepare(document.value());
    REQUIRE(track);
    struct Sample {
        double time, lean, crouch, fy, by;
    };
    constexpr Sample samples[] = {
        {0, 0, 0, -37, -37},
        {0.008333333333333333, 0, 0, -37, -37},
        {0.075, 0, 0, -37, -37},
        {0.08333333333333333, 0.10502400548696847, 0, -37.15753600823045, -36.789951989026065},
        {0.09166666666666666, 0.10502400548696847, 0, -37.15753600823045, -36.789951989026065},
        {0.5833333333333334, 1.9954561042524004, 0, -39.993184156378604, -33.0090877914952},
        {0.5916666666666667, 1.9954561042524004, 0, -39.993184156378604, -33.0090877914952},
        {0.6, 2, 0, -40, -33},
        {0.6083333333333333, 2, 0, -40, -33},
        {0.6583333333333333, 2, 0, -40, -33},
        {0.6666666666666666, 2, 0, -40, -33},
        {1.1916666666666667, 0.004543895747599613, -0.9977280521262002, -37.006815843621396, -36.9909122085048},
        {1.2, 0, -1, -37, -37},
        {1.2083333333333333, 0, -1, -37, -37},
        {1.7916666666666667, -1.9954561042524006, -0.0022719478737996956, -33.0090877914952, -39.993184156378604},
        {1.8, -2, 0, -33, -40},
        {1.8083333333333333, -2, 0, -33, -40},
        {2.3916666666666666, -0.004543895747599613, 0, -36.9909122085048, -37.006815843621396},
        {2.4, 0, 0, -37, -37}};
    for (const auto& q : samples) {
        auto pose = track.value().sample(q.time);
        REQUIRE(pose);
        REQUIRE(std::abs(pose.value().lean - q.lean) < 1e-12);
        REQUIRE(std::abs(pose.value().crouch - q.crouch) < 1e-12);
        REQUIRE(std::abs(pose.value().fy - q.fy) < 1e-12);
        REQUIRE(std::abs(pose.value().by - q.by) < 1e-12);
        REQUIRE(pose.value().turn.has_value());
        REQUIRE(*pose.value().turn == 1.1);
    }
}
TEST_CASE("stylize.pixelfx.nature.leafStormLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::LeafStorm);
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
TEST_CASE("stylize.pixelfx.nature.leafStormAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::LeafStorm), reference(PixelFxKind::LeafStorm);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
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
    PixelFx fx(PixelFxKind::LeafStorm);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::LeafStorm, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::LeafStorm, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
}


TEST_CASE("stylize.pixelfx.nature.poisonCloudPinnedSheetsCurlsPlumesAndCollapsedSpread") {
    // Source alpha/light hashes remain strict; RGB oracles use the approved one-LSB contract.
    struct Frame {
        double                   spread, time;
        uint32_t                 alpha, light;
        std::array<uint32_t, 20> colors;
    };
    constexpr Frame frames[] = {
        {1, 0, 3377949957u, 2510446192u, {{0u, 0u, 0u,          0u,          0u,          0u,          0u,
                                           0u, 0u, 0u,          0u,          928391230u,  2052927567u, 1382169178u,
                                           0u, 0u, 2906166923u, 2738716283u, 2939721355u, 1382169178u}}},
        {1, 0.08333333333333333, 956519524u, 3844065009u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 1013070426u, 2019373135u, 2103325009u, 0u,
                                                            0u, 2906166923u, 2738715770u, 2922487181u, 2939647602u}}},
        {1, 0.16666666666666666, 1996921213u, 2935314791u, {{0u, 0u,          0u,          0u,          0u,
                                                             0u, 0u,          0u,          0u,          0u,
                                                             0u, 1063402074u, 1935024190u, 2120102481u, 0u,
                                                             0u, 2889389707u, 2688448888u, 2905965948u, 3141427557u}}},
        {1, 0.25, 4003123657u, 3787070970u, {{0u, 0u, 0u,          0u,          0u,          0u,          0u,
                                              0u, 0u, 0u,          0u,          1935751770u, 1766398257u, 2086548049u,
                                              0u, 0u, 2889389451u, 2654894456u, 2905709965u, 2285079167u}}},
        {1, 0.3333333333333333, 3472300510u, 2995731615u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 1398946394u, 1043993124u, 3059428170u, 0u,
                                                            0u, 2855900298u, 2621341051u, 2939264397u, 0u}}},
        {1, 0.4166666666666667, 2734471315u, 1346995689u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 996293210u,  993661476u,  2891327557u, 0u,
                                                            0u, 2805568393u, 2587787132u, 3023150734u, 1280712766u}}},
        {1, 0.5, 3496524229u, 441976615u, {{0u, 0u, 0u,          0u,          0u,          0u,         0u,
                                            0u, 0u, 0u,          0u,          961945662u,  960107044u, 2287150400u,
                                            0u, 0u, 2755428220u, 2588309114u, 3073482895u, 1063402074u}}},
        {1, 0.5833333333333334, 1653710333u, 1116014588u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 1548360243u, 909775396u,  1565071666u, 258095706u,
                                                            0u, 2705162621u, 2588374650u, 3140526223u, 995500094u}}},
        {1, 0.6666666666666666, 3301105503u, 3608995365u, {{0u,          0u,          0u, 0u, 0u,          0u,
                                                            0u,          0u,          0u, 0u, 775580266u,  1598691890u,
                                                            1280712766u, 1481120049u, 0u, 0u, 2687798658u, 2638053244u,
                                                            3175069610u, 892998180u}}},
        {1, 0.75, 3708144167u, 2789753504u, {{0u,          0u,          0u,          0u,          0u,
                                              0u,          187738774u,  0u,          0u,          0u,
                                              0u,          1648957746u, 0u,          1497897265u, 0u,
                                              2922939774u, 2687798658u, 2705161594u, 3175069610u, 1280712766u}}},
        {1, 0.8333333333333334, 3328094540u, 1371011370u, {{0u,          0u,          0u,          0u,          0u,
                                                            0u,          691055254u,  0u,          0u,          0u,
                                                            0u,          2655341399u, 0u,          2571455576u, 0u,
                                                            2788786299u, 2704575360u, 2789047417u, 3106971791u, 0u}}},
        {1, 0.9166666666666666, 2192278209u, 690528587u, {{0u,          0u,          0u,          0u,          0u,
                                                           0u,          2051639435u, 0u,          0u,          0u,
                                                           0u,          2705672791u, 1382169178u, 2655341399u, 0u,
                                                           2084656990u, 2721417086u, 2839057802u, 3006373775u, 0u}}},
        {1, 1, 2656597032u, 3039241129u, {{0u,          0u,          0u,          0u,          0u,
                                           0u,          2067701911u, 0u,          1280712766u, 0u,
                                           0u,          3409867893u, 878059582u,  2304387399u, 0u,
                                           1382169178u, 2737937291u, 2872612235u, 2905710221u, 0u}}},
        {1, 1.0833333333333333, 4285709289u, 3575748364u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 2722404502u, 0u,          976884260u,  0u,
                                                            0u, 3527244665u, 861282366u,  2337876038u, 0u,
                                                            0u, 2754714764u, 2872612234u, 2805046669u, 0u}}},
        {1, 1.1666666666666667, 53898554u, 1966545949u, {{0u, 0u,          0u,          0u,          0u,
                                                          0u, 3259798935u, 0u,          1060770340u, 0u,
                                                          0u, 3409806463u, 810950718u,  2354917198u, 0u,
                                                          0u, 2771491980u, 2889389451u, 2754714764u, 0u}}},
        {1, 1.25, 3556824982u, 450255873u, {{0u,         0u,          0u,          0u,          0u,
                                             0u,         2655367336u, 0u,          1127879204u, 0u,
                                             660748890u, 2338139982u, 1464342833u, 2371694414u, 0u,
                                             0u,         2788269196u, 2923135870u, 2721160332u, 0u}}},
        {1, 1.3333333333333333, 843195401u, 3838691219u, {{0u,          0u,          0u,          0u,          0u,
                                                           0u,          2739253930u, 0u,          1180049470u, 0u,
                                                           1196826686u, 2136615240u, 2018848073u, 3192605788u, 0u,
                                                           0u,          2855378317u, 2957289120u, 2704383116u, 0u}}},
        {1, 1.4166666666666667, 123909139u, 4031299728u, {{0u,          0u,          0u,          0u,
                                                           0u,          0u,          2789520812u, 0u,
                                                           1180049470u, 0u,          1633034301u, 1935354955u,
                                                           2856996958u, 2002463819u, 0u,          1029847642u,
                                                           2939264653u, 3007165351u, 2670828684u, 2939647602u}}},
        {1, 1.5, 2326547879u, 2518119841u, {{0u,          0u,          0u,          0u,          0u,
                                             0u,          2789521325u, 0u,          1161433636u, 0u,
                                             0u,          1851468875u, 2873773918u, 1968909387u, 0u,
                                             1013070426u, 3039928206u, 3023943080u, 2637531262u, 3007079270u}}},
        {1, 1.5833333333333333, 2625032740u, 1366543633u, {{0u,          0u,          0u,          0u,
                                                            0u,          0u,          2755042704u, 1280712766u,
                                                            1094324772u, 0u,          258095706u,  1784425804u,
                                                            2873773918u, 1935354955u, 0u,          945168446u,
                                                            3107037327u, 3007165607u, 2603912064u, 2285079167u}}},
        {1, 1.6666666666666667, 2094552794u, 2390401587u, {{0u,         0u,          0u,          0u,          0u,
                                                            0u,         2670824575u, 775557668u,  1010438692u, 0u,
                                                            0u,         1733697598u, 2103259472u, 3177133397u, 0u,
                                                            825889316u, 3140526223u, 2973610148u, 2603912578u, 0u}}},
        {1, 1.75, 2913877572u, 2969238462u, {{0u,          0u,          0u,          0u,          0u,
                                              0u,          2066786966u, 758780452u,  909775396u,  0u,
                                              0u,          1716920382u, 2120036688u, 1884692542u, 0u,
                                              1280712766u, 3123749007u, 2922679938u, 2637467010u, 1382169178u}}},
        {1, 1.8333333333333333, 1446456243u, 3579069468u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          725226020u,  0u,          0u,
                                                            0u, 1716920382u, 2120366682u, 1834360894u, 0u,
                                                            0u, 3073482895u, 2889124736u, 2671020929u, 1480223338u}}},
        {1, 1.9166666666666667, 1610789009u, 3227789763u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          708448804u,  0u,          0u,
                                                            0u, 1699223857u, 1080179290u, 1750474814u, 0u,
                                                            0u, 3006765196u, 2855635327u, 2721939324u, 1348614746u}}},
        {1, 2, 971026048u, 2212744593u, {{0u, 0u,          0u,          0u,          0u,
                                          0u, 0u,          708448804u,  0u,          0u,
                                          0u, 960107044u,  1080179290u, 1043993124u, 0u,
                                          0u, 2906166923u, 2838857854u, 2772204922u, 1382169178u}}},
        {1, 2.0833333333333335, 1011536984u, 410002996u, {{0u,          0u,          0u,          0u,          0u,
                                                           0u,          0u,          708448804u,  0u,          0u,
                                                           0u,          0u,          2120366682u, 1196826686u, 0u,
                                                           2268891524u, 2822601336u, 2855635071u, 2821823885u, 0u}}},
        {1, 2.1666666666666665, 767968752u, 2549519952u, {{0u,          0u,          0u,          0u,          0u,
                                                           0u,          0u,          742003236u,  0u,          0u,
                                                           0u,          87075478u,   1817914186u, 1180049470u, 0u,
                                                           2368966016u, 2755747942u, 2905709965u, 2855378573u, 0u}}},
        {1, 2.25, 3863155506u, 1564253794u, {{0u,          0u,          0u,          0u,          0u,
                                              0u,          0u,          758780452u,  0u,          0u,
                                              0u,          1230913885u, 1935288905u, 1163272254u, 0u,
                                              1547332202u, 2772525158u, 2972819085u, 2838601101u, 0u}}},
        {1, 2.3333333333333335, 933976282u, 3076889209u, {{0u, 0u,          0u,          0u,          0u,
                                                           0u, 0u,          775557668u,  0u,          0u,
                                                           0u, 3091038846u, 2052927567u, 2622890046u, 0u,
                                                           0u, 2872547211u, 3056705422u, 2838857341u, 0u}}},
        {1, 2.4166666666666665, 858912455u, 3819782061u, {{0u, 0u,          0u,          0u,          0u,
                                                           0u, 0u,          792334884u,  0u,          0u,
                                                           0u, 3157952128u, 2136813904u, 2488672317u, 0u,
                                                           0u, 2939656332u, 3090260111u, 2872412543u, 1280712766u}}},
        {1, 2.5, 3874013790u, 3199766319u, {{0u, 0u, 0u,          0u,          0u,          0u,          0u,
                                             0u, 0u, 0u,          0u,          2956365701u, 2153656656u, 1834757453u,
                                             0u, 0u, 3006765452u, 3107037327u, 2905902209u, 1113733722u}}},
        {1, 2.5833333333333335, 54875818u, 3575516355u, {{0u, 0u,          0u,          0u,          0u,
                                                          0u, 0u,          0u,          0u,          0u,
                                                          0u, 1263870527u, 2136879440u, 2119445575u, 258095706u,
                                                          0u, 3057031821u, 3090260111u, 2923071361u, 1062608958u}}},
        {1, 2.6666666666666665, 794104121u, 3644018616u, {{0u,          0u,          0u, 0u, 0u,          0u,
                                                           0u,          0u,          0u, 0u, 775580266u,  1213603902u,
                                                           2102734152u, 1548097583u, 0u, 0u, 3090586253u, 3040319885u,
                                                           2923071874u, 926552612u}}},
        {1, 2.75, 1383304949u, 1541040482u, {{0u,          0u,          0u,          0u,          0u,
                                              0u,          0u,          0u,          0u,          0u,
                                              0u,          1180049470u, 2052468297u, 1497765935u, 0u,
                                              1513777770u, 3073809037u, 2973210764u, 2939849090u, 3342494571u}}},
        {1, 2.8333333333333335, 3083085377u, 1079950606u, {{0u,          0u,          0u,          0u,
                                                            0u,          0u,          0u,          0u,
                                                            0u,          0u,          0u,          2069241918u,
                                                            1432500826u, 1481054256u, 0u,          1480223338u,
                                                            3040254605u, 2906101900u, 2923071617u, 3091030629u}}},
        {1, 2.9166666666666665, 3102200893u, 91528225u, {{0u,          0u,          0u,          0u,
                                                          0u,          0u,          0u,          0u,
                                                          0u,          0u,          0u,          1749686835u,
                                                          1432500826u, 1800806462u, 0u,          1331837530u,
                                                          2973211020u, 2838992779u, 2906293888u, 2285079167u}}},
        {1, 3, 944773652u, 1762928834u, {{0u,          0u,          0u,          0u,          0u,
                                          0u,          0u,          0u,          0u,          0u,
                                          0u,          2170368079u, 1415723610u, 2254254159u, 0u,
                                          1382169178u, 2922679425u, 2822215563u, 2872804479u, 0u}}},
        {1, 3.0833333333333335, 133307713u, 2195463164u, {{0u, 0u,          0u,          0u,          0u,
                                                           0u, 862075482u,  0u,          0u,          0u,
                                                           0u, 2924105309u, 1365391962u, 2823179867u, 0u,
                                                           0u, 2855570304u, 2838992779u, 2856026749u, 0u}}},
        {1, 3.1666666666666665, 1632592308u, 354924402u, {{0u, 0u,          0u,          0u,          0u,
                                                           0u, 1818376794u, 0u,          0u,          0u,
                                                           0u, 2924105309u, 2336708964u, 2839957083u, 0u,
                                                           0u, 2805694845u, 2906102156u, 2839314556u, 0u}}},
        {1, 3.25, 1093122365u, 1439587954u, {{0u, 0u, 0u,          0u,          0u,          0u,          0u,
                                              0u, 0u, 0u,          660748890u,  2907328349u, 2218939228u, 2839957083u,
                                              0u, 0u, 2805694845u, 2989922701u, 2856091772u, 0u}}},
        {1, 3.3333333333333335, 4091780532u, 3222354409u, {{0u,          0u,          0u,          0u,          0u,
                                                            0u,          0u,          0u,          0u,          0u,
                                                            1196826686u, 2120036688u, 1717383247u, 2589536590u, 0u,
                                                            0u,          2822472574u, 3057563305u, 2906358397u, 0u}}},
        {1, 3.4166666666666665, 1741094684u, 4158332389u, {{0u,          0u,          0u,          0u,          0u,
                                                            0u,          0u,          0u,          0u,          0u,
                                                            1633034301u, 2120036688u, 1667117392u, 1833638451u, 0u,
                                                            2705554557u, 2839184767u, 3124672939u, 2973210251u, 0u}}},
        {1, 3.5, 1619588068u, 2510313506u, {{0u,          0u,          0u,          0u,          0u,
                                             0u,          0u,          0u,          0u,          0u,
                                             0u,          1043993124u, 2438671435u, 1850415668u, 0u,
                                             2855960186u, 2872739713u, 3158227372u, 3040319628u, 0u}}},
        {1, 3.5833333333333335, 961042405u, 456580833u, {{0u,          0u,          0u,          0u,          0u,
                                                          0u,          0u,          0u,          0u,          0u,
                                                          258095706u,  1010438692u, 2220107844u, 1867258676u, 0u,
                                                          2202162269u, 2889517186u, 3124738219u, 3073808524u, 0u}}},
        {1, 3.6666666666666665, 3162755542u, 681379920u, {{0u,         0u,          0u,          0u,          0u,
                                                           0u,         0u,          0u,          0u,          0u,
                                                           0u,         2738062202u, 2604616019u, 3176869967u, 0u,
                                                           909775396u, 2940448422u, 3057097100u, 3107363212u, 0u}}},
        {1, 3.75, 1815240457u, 2503831076u, {{0u,          0u,          0u,          0u,          0u,
                                              0u,          0u,          0u,          0u,          0u,
                                              0u,          3392830841u, 1783175474u, 1800149813u, 0u,
                                              1280712766u, 2957290917u, 2956433291u, 3090585996u, 1382169178u}}},
        {1, 3.8333333333333335, 1084280473u, 1546127131u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 3006565248u, 1917921603u, 1783372597u, 0u,
                                                            0u, 2940446882u, 2855835018u, 3057096844u, 962738778u}}},
        {1, 3.9166666666666665, 2338874794u, 1452996939u, {{0u, 0u,          0u,          0u,          0u,
                                                            0u, 0u,          0u,          0u,          0u,
                                                            0u, 1851138110u, 2002595663u, 2186682430u, 0u,
                                                            0u, 2922944139u, 2772204923u, 3006764939u, 861282366u}}},
        {1, 4, 3377949957u, 2510446192u, {{0u, 0u, 0u,          0u,          0u,          0u,          0u,
                                           0u, 0u, 0u,          0u,          928391230u,  2052927567u, 1382169178u,
                                           0u, 0u, 2906166923u, 2738716283u, 2939721355u, 1382169178u}}},
        {0, 0, 2789302005u, 3472892651u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1213603902u, 0u, 0u,
                                           0u, 0u, 4013198722u, 0u, 0u, 0u, 0u, 3056705422u, 0u, 0u}}},
        {0, 0.25, 1973791015u, 4261590212u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1882636717u, 0u, 0u,
                                              0u, 0u, 4116008291u, 0u, 0u, 0u, 0u, 3241379709u, 0u, 0u}}},
        {0, 0.5, 4098696033u, 3832315215u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1766367390u, 0u, 0u,
                                             0u, 0u, 4132913242u, 0u, 0u, 0u, 0u, 3006437000u, 0u, 0u}}},
        {0, 0.75, 4002316186u, 1571295920u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 2052412519u, 0u, 0u,
                                              0u, 0u, 4099615820u, 0u, 0u, 0u, 0u, 3929118344u, 0u, 0u}}},
        {0, 1, 1144260305u, 2541325183u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 2083564182u, 0u, 0u,
                                           0u, 0u, 4014169187u, 0u, 0u, 0u, 0u, 3258091646u, 0u, 0u}}},
        {0, 1.25, 1677494839u, 2449751844u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 2419367053u, 0u, 0u,
                                              0u, 0u, 3996939637u, 0u, 0u, 0u, 0u, 2889124480u, 0u, 0u}}},
        {0, 1.5, 1068582797u, 3925122947u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 3543313561u, 0u, 0u,
                                             0u, 0u, 3612485185u, 0u, 0u, 0u, 0u, 3543637137u, 0u, 0u}}},
        {0, 1.75, 2012721234u, 1973261471u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 3140528790u, 0u, 0u,
                                              0u, 0u, 3494914884u, 0u, 0u, 0u, 0u, 3157820542u, 0u, 0u}}},
        {0, 2, 1824792142u, 2630374734u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1631935332u, 0u, 0u,
                                           0u, 0u, 3611057505u, 0u, 0u, 0u, 0u, 3744505743u, 0u, 0u}}},
        {0, 2.25, 642666290u, 3164737988u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1080179290u, 0u, 0u,
                                             0u, 0u, 4099294815u, 0u, 0u, 0u, 0u, 3409151868u, 0u, 0u}}},
        {0, 2.5, 1226501860u, 2721379022u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 509753946u,  0u, 0u,
                                             0u, 0u, 4198140804u, 0u, 0u, 0u, 0u, 3090260111u, 0u, 0u}}},
        {0, 2.75, 3828085280u, 3555698229u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 56769114u,   0u, 0u,
                                              0u, 0u, 4132135276u, 0u, 0u, 0u, 0u, 3627260299u, 0u, 0u}}},
        {0, 3, 2701508636u, 3957977732u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 0u,          0u, 0u,
                                           0u, 0u, 3914091613u, 0u, 0u, 0u, 0u, 3124202884u, 0u, 0u}}},
        {0, 3.25, 2322537175u, 1846909962u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 525738046u,  0u, 0u,
                                              0u, 0u, 3696574295u, 0u, 0u, 0u, 0u, 3258228110u, 0u, 0u}}},
        {0, 3.5, 3948591086u, 2661811049u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 476199514u,  0u, 0u,
                                             0u, 0u, 4282441932u, 0u, 0u, 0u, 0u, 3828582527u, 0u, 0u}}},
        {0, 3.75, 2278513899u, 1783303319u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 0u,          0u, 0u,
                                              0u, 0u, 4047012476u, 0u, 0u, 0u, 0u, 3342303611u, 0u, 0u}}},
        {0, 4, 2789302005u, 3472892651u, {{0u, 0u, 0u,          0u, 0u, 0u, 0u, 1213603902u, 0u, 0u,
                                           0u, 0u, 4013198722u, 0u, 0u, 0u, 0u, 3056705422u, 0u, 0u}}}};
    constexpr std::array<std::array<int, 2>, 20> points{{{-96, -48}, {-48, -48}, {0, -48}, {48, -48}, {96, -48},
                                                         {-96, -24}, {-48, -24}, {0, -24}, {48, -24}, {96, -24},
                                                         {-96, -12}, {-48, -12}, {0, -12}, {48, -12}, {96, -12},
                                                         {-96, -2},  {-48, -2},  {0, -2},  {48, -2},  {96, -2}}};
    PixelFx                                      fx(PixelFxKind::PoisonCloud);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        if (fx.settings().nature.spread != f.spread) {
            auto s          = fx.settings();
            s.nature.spread = f.spread;
            auto p          = PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, s);
            REQUIRE(p);
            REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        }
        fx.render(f.time);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        for (size_t i = 0; i < points.size(); ++i) {
            const auto q      = points[i];
            const auto actual = fx.color()[size_t((q[1] + 86) * 256 + q[0] + 128)], expected = f.colors[i];
            REQUIRE((actual >> 24) == (expected >> 24));
            for (int s : {0, 8, 16}) REQUIRE(std::abs(int((actual >> s) & 255u) - int((expected >> s) & 255u)) <= 1);
        }
    }
}
TEST_CASE("stylize.pixelfx.nature.poisonCloudSpreadBoundsAtomicRoundTripAndLoop") {
    PixelFx fx(PixelFxKind::PoisonCloud);
    auto    s = fx.settings();
    REQUIRE(s.nature.spread == 1);
    s.nature.spread = 2;
    s.palette       = {0xff112233u};
    auto p          = PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, s);
    REQUIRE(p);
    s.nature.spread = 0;
    s.palette[0]    = 0xff445566u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.spread == 2);
    REQUIRE(owned.palette.size() == 8);
    for (auto c : owned.palette) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::PoisonCloud, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double spread :
         {-.001, 4.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        s.nature.spread = spread;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, s));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double spread : {0., 4.}) {
        s.nature.spread = spread;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, s));
    }
    fx.render(0);
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    for (double t : {4., 8., 16.}) {
        fx.render(t);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
    }
}
TEST_CASE("stylize.pixelfx.nature.poisonCloudStopAndSeekRestart") {
    PixelFx fx(PixelFxKind::PoisonCloud);
    auto    seek = fx.prepareSeek(1.25);
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
    seek = fx.prepareSeek(1.25);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    REQUIRE(std::equal(c.begin(), c.end(), fx.color().begin()));
    REQUIRE(std::equal(l.begin(), l.end(), fx.light().begin()));
}
TEST_CASE("stylize.pixelfx.nature.poisonCloudLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::PoisonCloud);
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
TEST_CASE("stylize.pixelfx.nature.poisonCloudAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::PoisonCloud), reference(PixelFxKind::PoisonCloud);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
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
    PixelFx fx(PixelFxKind::PoisonCloud);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::PoisonCloud, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::PoisonCloud, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
}


TEST_CASE("stylize.pixelfx.nature.quakePinnedJoltsRiftSlabsDustAndZeroReach") {
    struct Frame {
        double   reach, time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{118, 0, 1543482821u, 1543482821u},
                                {118, 0.008333333333333333, 1543482821u, 1543482821u},
                                {118, 0.058333333333333334, 1543482821u, 1543482821u},
                                {118, 0.06666666666666667, 3790376634u, 109234127u},
                                {118, 0.075, 4016243638u, 2686520498u},
                                {118, 0.2, 3254318392u, 2844396005u},
                                {118, 0.49166666666666664, 1158535676u, 769973857u},
                                {118, 0.5, 337402417u, 2297188979u},
                                {118, 0.5083333333333333, 2403767075u, 3581393659u},
                                {118, 0.8, 469287890u, 2218919522u},
                                {118, 0.9166666666666666, 935264384u, 269898682u},
                                {118, 0.925, 3577364787u, 1444463515u},
                                {118, 0.9333333333333333, 3999030696u, 2599798431u},
                                {118, 1, 352993147u, 2886452640u},
                                {118, 1.5, 3715248065u, 3204539613u},
                                {118, 2, 934308875u, 963396186u},
                                {118, 2.191666666666667, 3636722672u, 101122627u},
                                {118, 2.2, 4137875049u, 3227116866u},
                                {118, 2.2083333333333335, 502955140u, 4194345301u},
                                {118, 2.5, 1334993361u, 2875039928u},
                                {118, 2.7, 2802023791u, 3030457626u},
                                {118, 2.8, 2702503915u, 3637223251u},
                                {118, 2.9, 2493728733u, 3741956826u},
                                {118, 3, 1543482821u, 1543482821u},
                                {118, 3.1, 1543482821u, 1543482821u},
                                {118, 4, 1543482821u, 1543482821u},
                                {118, 5, 1543482821u, 1543482821u},
                                {0, 0, 1543482821u, 1543482821u},
                                {0, 0.19166666666666668, 2249800754u, 1263254738u},
                                {0, 0.39166666666666666, 764585442u, 2727592149u},
                                {0, 0.5833333333333334, 562184958u, 2246990526u},
                                {0, 0.775, 3725846233u, 2255482356u},
                                {0, 0.9666666666666667, 2631219597u, 2285028656u},
                                {0, 1.1666666666666667, 1781991349u, 2876618923u},
                                {0, 1.3583333333333334, 705079298u, 3365111358u},
                                {0, 1.55, 110088641u, 94222482u},
                                {0, 1.7416666666666667, 3845611331u, 4106593877u},
                                {0, 1.9416666666666667, 2090035746u, 929326287u},
                                {0, 2.1333333333333333, 3171403965u, 2234628409u},
                                {0, 2.325, 2748836747u, 3019297165u},
                                {0, 2.5166666666666666, 513662350u, 2124115000u},
                                {0, 2.716666666666667, 971895240u, 3710464429u},
                                {0, 2.908333333333333, 2954824547u, 3948393664u},
                                {0, 3.1, 1543482821u, 1543482821u}};
    PixelFx         fx(PixelFxKind::Quake);
    REQUIRE(fx.isSimulated());
    for (const auto& f : frames) {
        if (fx.settings().nature.reach != f.reach) {
            auto s         = fx.settings();
            s.nature.reach = f.reach;
            auto p         = PixelFx::prepareConfiguration(PixelFxKind::Quake, s);
            REQUIRE(p);
            REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        }
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.quakeReachGlowOwnershipAtomicBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::Quake);
    auto    s = fx.settings();
    REQUIRE(s.nature.reach == 118);
    REQUIRE(s.nature.glow.size() == 5);
    s.nature.reach = 154;
    s.nature.glow  = {0xff112233u};
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::Quake, s);
    REQUIRE(p);
    s.nature.reach   = 0;
    s.nature.glow[0] = 0xff445566u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.reach == 154);
    REQUIRE(owned.nature.glow.size() == 5);
    for (auto c : owned.nature.glow) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Quake, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double reach :
         {-.001, 512.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        s.nature.reach = reach;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Quake, s));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double reach : {0., 512.}) {
        s.nature.reach = reach;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Quake, s));
    }
    s.nature.reach = 118;
    for (size_t count : {size_t(0), size_t(65)}) {
        s.nature.glow.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Quake, s));
        REQUIRE(fx.configurationValue() == value);
    }
    s.nature.glow = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Quake, s));
    s.nature.glow.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Quake, s));
}
TEST_CASE("stylize.pixelfx.nature.quakeLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Quake);
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
TEST_CASE("stylize.pixelfx.nature.quakePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Quake), reference(PixelFxKind::Quake);
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

TEST_CASE("stylize.pixelfx.nature.quakeStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::Quake);
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


TEST_CASE("stylize.pixelfx.nature.rootSnarePinnedGrowthCinchBreakChipsAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 4143342453u, 2802032069u},
                                {0.008333333333333333, 3770555738u, 2802032069u},
                                {0.15833333333333333, 905429270u, 761432164u},
                                {0.16666666666666666, 4136763938u, 2276362554u},
                                {0.2, 2341702126u, 3941645202u},
                                {0.3, 3214603465u, 3448282558u},
                                {0.4, 2800585871u, 3052006063u},
                                {0.5, 4224690580u, 3298952315u},
                                {0.6166666666666667, 3807398874u, 355495309u},
                                {0.625, 1188667u, 1517438375u},
                                {0.6333333333333333, 1869398185u, 237802224u},
                                {0.7416666666666667, 1571399660u, 1225820498u},
                                {0.8, 1765108642u, 3008720966u},
                                {1, 4012247222u, 336055826u},
                                {1.25, 2757981799u, 1967372709u},
                                {1.7, 527950167u, 1133787275u},
                                {1.7083333333333333, 4222098405u, 1741608810u},
                                {1.7416666666666667, 3080263499u, 3787383818u},
                                {1.75, 1632374154u, 1859078358u},
                                {1.7583333333333333, 3110848981u, 693715281u},
                                {1.8, 638234736u, 628376975u},
                                {2, 2062257862u, 1125183612u},
                                {2.1, 1151199412u, 1938109222u},
                                {2.5, 3334823043u, 2802032069u},
                                {2.55, 3275524057u, 2802032069u},
                                {2.65, 2802032069u, 2802032069u},
                                {2.7, 2802032069u, 2802032069u},
                                {3, 2802032069u, 2802032069u},
                                {4, 2802032069u, 2802032069u},
                                {5, 2802032069u, 2802032069u}};
    PixelFx         fx(PixelFxKind::RootSnare);
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
TEST_CASE("stylize.pixelfx.nature.rootSnareGlowOwnershipBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::RootSnare);
    auto    s = fx.settings();
    REQUIRE(s.nature.glow.size() == 4);
    s.nature.glow = {0xff112233u};
    auto p        = PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s);
    REQUIRE(p);
    s.nature.glow[0] = 0xff445566u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    REQUIRE(owned.nature.glow.size() == 4);
    for (auto c : owned.nature.glow) REQUIRE(c == 0xff112233u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::RootSnare, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (size_t count : {size_t(0), size_t(65)}) {
        s.nature.glow.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s));
        REQUIRE(fx.configurationValue() == value);
    }
    s.nature.glow = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s));
    s.nature.glow.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s));
}
TEST_CASE("stylize.pixelfx.nature.rootSnarePinnedBackBodyFrontComposition") {
    struct Frame {
        int      actor;
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{1, 0, 2180568868u, 2802032069u},
                                {1, 0.008333333333333333, 1625291999u, 2802032069u},
                                {1, 0.3, 3493909991u, 3448282558u},
                                {1, 0.625, 2981002931u, 1517438375u},
                                {1, 0.8666666666666667, 2535222676u, 1041986506u},
                                {1, 1.1, 894074541u, 4101986106u},
                                {1, 1.75, 1485786369u, 1859078358u},
                                {1, 1.8, 3907369089u, 628376975u},
                                {1, 2.7, 1511018708u, 2802032069u},
                                {2, 0, 612514303u, 2802032069u},
                                {2, 0.008333333333333333, 1424351176u, 2802032069u},
                                {2, 0.3, 65978687u, 3448282558u},
                                {2, 0.625, 3248128418u, 1517438375u},
                                {2, 0.8666666666666667, 3845313303u, 1041986506u},
                                {2, 1.1, 436061697u, 4101986106u},
                                {2, 1.75, 2585366509u, 1859078358u},
                                {2, 1.8, 1103323130u, 628376975u},
                                {2, 2.7, 655500527u, 2802032069u}};
    PixelFx         fx(PixelFxKind::RootSnare);
    const auto      info = fx.info();
    for (const auto& f : frames) {
        auto s         = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto p         = PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s);
        REQUIRE(p);
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        std::vector<uint32_t> color(fx.color().size()), light(color.size()), body(color.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, color, light};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) color[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(color) == f.color);
        REQUIRE(checksum(light) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.nature.rootSnarePinnedPoseTrackPrerollSnapAndRecovery") {
    auto document = eve::Value::fromJson(
        R"track({"schema":"eve.pixelfx.pose-track","version":1,"keys":[[-0.24,{"crouch":3,"fx":11,"fy":-21,"bx":9,"by":-21,"ffx":-2,"fbx":3}],[0.3,{"turn":0.9,"lean":-1,"crouch":2,"fx":-10,"fy":-24,"bx":11,"by":-24,"ffx":-2,"fbx":3}],[0.62,{"turn":0.9,"crouch":3,"fx":-12,"fy":-34,"bx":12,"by":-28,"ffx":-2,"fbx":3},"snap"],[0.86,{"turn":0.9,"crouch":3,"fx":-12,"fy":-37,"bx":12,"by":-27,"ffx":-2,"fbx":3,"lean":-3}],[1.1,{"turn":0.9,"crouch":3,"fx":-12,"fy":-27,"bx":12,"by":-37,"ffx":-2,"fbx":3,"lean":3}],[1.34,{"turn":0.9,"crouch":3,"fx":-12,"fy":-37,"bx":12,"by":-27,"ffx":-2,"fbx":3,"lean":-3}],[1.58,{"turn":0.9,"crouch":3,"fx":-12,"fy":-27,"bx":12,"by":-37,"ffx":-2,"fbx":3,"lean":3}],[1.75,{"turn":0.9,"crouch":6,"fx":-12,"fy":-34,"bx":12,"by":-28,"ffx":-2,"fbx":3}],[1.8,{"turn":1.1,"crouch":-1,"y":-3,"fx":-14,"fy":-36,"bx":15,"by":-36,"ffx":-3,"ffy":-6,"fbx":4,"fby":-6},"snap"],[2.05,{"crouch":5,"fx":11,"fy":-21,"bx":9,"by":-21,"ffx":7,"fbx":-7,"y":0}],[2.7,{"crouch":3,"fx":11,"fy":-21,"bx":9,"by":-21,"ffx":-2,"fbx":3}]]})track");
    REQUIRE(document);
    auto track = rig::PoseTrack::prepare(document.value());
    REQUIRE(track);
    struct Sample {
        double time, lean, crouch, fy, by, y;
    };
    constexpr Sample samples[] = {
        {-0.24, 0, 3, -21, -21, 0},
        {-0.2, 0, 3, -21, -21, 0},
        {-0.008333333333333333, -0.22697721854354477, 2.773022781456455, -21.680931655630634, -21.680931655630634, 0},
        {0, -0.22697721854354477, 2.773022781456455, -21.680931655630634, -21.680931655630634, 0},
        {0.2916666666666667, -0.9843519788650104, 2.0156480211349894, -23.95305593659503, -23.95305593659503, 0},
        {0.3, -1, 2, -24, -24, 0},
        {0.6083333333333333, -1, 2, -24, -24, 0},
        {0.6166666666666667, -1, 2, -24, -24, 0},
        {0.625, 0, 3, -34, -28, 0},
        {0.85, -2.330889917695473, 3, -36.33088991769547, -27.22303669410151, 0},
        {0.8583333333333333, -2.330889917695473, 3, -36.33088991769547, -27.22303669410151, 0},
        {1.0916666666666666, 1.6617798353909432, 3, -29.230366941015095, -34.76963305898491, 0},
        {1.1, 3, 3, -27, -37, 0},
        {1.3333333333333333, -1.6617798353909485, 3, -34.769633058984915, -29.230366941015085, 0},
        {1.3416666666666666, -3, 3, -37, -27, 0},
        {1.575, 1.6617798353909485, 3, -29.230366941015085, -34.769633058984915, 0},
        {1.5833333333333333, 3, 3, -27, -37, 0},
        {1.7416666666666667, 1.5441119931248175, 4.455888006875183, -30.39707201604209, -32.63233597937445, 0},
        {1.75, 0, 6, -34, -28, 0},
        {1.7583333333333333, 0, 6, -34, -28, 0},
        {1.7916666666666667, 0, 6, -34, -28, 0},
        {1.8, 0, -1, -36, -36, -3},
        {2.0416666666666665, 0, 3.4444444444444517, -24.88888888888887, -24.88888888888887, -0.009777777777777885},
        {2.05, 0, 5, -21, -21, 0},
        {2.691666666666667, 0, 3.058800721522615, -21, -21, 0},
        {2.7, 0, 3, -21, -21, 0}};
    for (const auto& q : samples) {
        auto pose = track.value().sample(q.time);
        REQUIRE(pose);
        REQUIRE(std::abs(pose.value().lean - q.lean) < 1e-12);
        REQUIRE(std::abs(pose.value().crouch - q.crouch) < 1e-12);
        REQUIRE(std::abs(pose.value().fy - q.fy) < 1e-12);
        REQUIRE(std::abs(pose.value().by - q.by) < 1e-12);
        REQUIRE(std::abs(pose.value().y - q.y) < 1e-12);
    }
}
TEST_CASE("stylize.pixelfx.nature.rootSnareStockNoneCustomBodyOwnership") {
    PixelFx fx(PixelFxKind::RootSnare);
    REQUIRE(fx.settings().actor.source == PixelFxActorSource::Dummy);
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    REQUIRE(checksum(body) == 655500527u);
    auto s         = fx.settings();
    s.actor.source = PixelFxActorSource::Hero;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(checksum(body) == 1511018708u);
    s.actor        = {};
    s.actor.source = PixelFxActorSource::Custom;
    s.actor.width  = 4;
    s.actor.height = 4;
    s.actor.x      = -2;
    s.actor.y      = -4;
    s.actor.head   = {0, -4};
    s.actor.chest  = {0, -2};
    s.actor.mask.assign(16, 1);
    s.actor.pixels.assign(16, 0xff112233u);
    p = PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s);
    REQUIRE(p);
    s.actor.pixels[0] = 0xff445566u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::count(body.begin(), body.end(), 0xff112233u) == 16);
    const auto value = fx.configurationValue();
    s.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s));
    REQUIRE(fx.configurationValue() == value);
    s.actor = {};
    p       = PixelFx::prepareConfiguration(PixelFxKind::RootSnare, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::all_of(body.begin(), body.end(), [](uint32_t c) { return c == 0; }));
    REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
}
TEST_CASE("stylize.pixelfx.nature.rootSnareLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::RootSnare);
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
TEST_CASE("stylize.pixelfx.nature.rootSnarePartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::RootSnare), reference(PixelFxKind::RootSnare);
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

TEST_CASE("stylize.pixelfx.nature.rootSnareStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::RootSnare);
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


TEST_CASE("stylize.pixelfx.nature.gasSourceClockAndExpiry") {
    nature::Gas gas;
    gas.add(.8, -3, -20, 42, -65, 3, 12, 2.9, 7);
    gas.add(.8, 9, -5, 55, -4, 2, 6, 1.9, 5);
    REQUIRE(gas.list.size() == 2);
    REQUIRE(gas.list[0].seed == 0);
    REQUIRE(gas.list[1].seed == 37.7);
    // Independent pristine upstream Gas.at samples, not native-generated expectations.
    struct Fixture {
        double            t;
        size_t            i;
        nature::GasSample sample;
    };
    const Fixture fixtures[] = {
        {.8, 0, {-3, -20, 3, 0, 0}},
        {.8, 1, {9, -5, 2, 0, 0}},
        {.95, 0, {2.7406351439293433, -29.142092712126413, 5.970703124999998, .9999999999999994, .0714285714285714}},
        {.95, 1, {16.401525490079788, -6.0484830471328515, 3.320312499999999, .9999999999999994, .1363636363636363}},
        {1.4, 0, {12.026165801441895, -44.98062240774382, 10.875, 1, .2857142857142857}},
        {1.4, 1, {28.35576532884526, -8.460678715720546, 5.5, .7575757575757577, .5454545454545454}},
        {2.9, 0, {13.461770231287073, -64.83613486235082, 12, 0, 1}},
        {2.9, 1, {31.31312041283046, -18.184207369340434, 6, 0, 1}}};
    for (const auto& f : fixtures) {
        const auto a = nature::Gas::at(gas.list[f.i], f.t);
        REQUIRE(std::abs(a.x - f.sample.x) < 1e-12);
        REQUIRE(std::abs(a.y - f.sample.y) < 1e-12);
        REQUIRE(std::abs(a.r - f.sample.r) < 1e-12);
        REQUIRE(std::abs(a.a - f.sample.a) < 1e-14);
        REQUIRE(std::abs(a.u - f.sample.u) < 1e-14);
    }
    gas.prune(1.9);
    REQUIRE(gas.list.size() == 2);
    gas.prune(std::nextafter(1.9, 2.));
    REQUIRE(gas.list.size() == 1);
    REQUIRE(gas.list[0].x == -3);
    gas.prune(2.9);
    REQUIRE(gas.list.size() == 1);
    gas.prune(std::nextafter(2.9, 3.));
    REQUIRE(gas.list.empty());
    gas.add(3, 0, 0, 0, 0, 1, 2, 4);
    REQUIRE(gas.list[0].seed == 75.4);
}

TEST_CASE("stylize.pixelfx.nature.sporeBurstPinnedGrowthPuffSinkAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1111764677u, 1111764677u},
                                {0.008333333333333333, 776592156u, 1111764677u},
                                {0.1, 1306956162u, 1111764677u},
                                {0.2, 1425305002u, 1111764677u},
                                {0.31666666666666665, 497943964u, 1111764677u},
                                {0.325, 626184556u, 1111764677u},
                                {0.4, 2111792212u, 996400632u},
                                {0.5, 929142887u, 1488020944u},
                                {0.7416666666666667, 3541232221u, 1186211433u},
                                {0.75, 99075870u, 1294376078u},
                                {0.7916666666666666, 1371070506u, 2027639378u},
                                {0.8, 2194152126u, 43441414u},
                                {0.8083333333333333, 3051883935u, 9439494u},
                                {0.8166666666666667, 3467573123u, 4230206325u},
                                {0.9, 4110705400u, 4210502314u},
                                {1, 4222043112u, 2406733181u},
                                {1.25, 2316518758u, 4154235284u},
                                {1.5, 4083786004u, 1963206638u},
                                {2, 3188212417u, 1742150532u},
                                {2.15, 688759939u, 1661116601u},
                                {2.158333333333333, 2423655042u, 1239901154u},
                                {2.3, 3845335781u, 1895083184u},
                                {2.4, 4032142306u, 3544943247u},
                                {2.6, 4147899799u, 2515500898u},
                                {3.15, 1111764677u, 1111764677u},
                                {3.2, 1111764677u, 1111764677u},
                                {4, 1111764677u, 1111764677u},
                                {5, 1111764677u, 1111764677u}};
    PixelFx         fx(PixelFxKind::SporeBurst);
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
TEST_CASE("stylize.pixelfx.nature.sporeBurstMaterialOwnershipBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::SporeBurst);
    auto    s = fx.settings();
    REQUIRE(s.nature.cap.size() == 6);
    REQUIRE(s.nature.stem.size() == 5);
    s.nature.cap  = {0xff112233u};
    s.nature.stem = {0xff445566u};
    auto p        = PixelFx::prepareConfiguration(PixelFxKind::SporeBurst, s);
    REQUIRE(p);
    s.nature.cap[0]  = 0xffabcdefu;
    s.nature.stem[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.nature.cap) REQUIRE(c == 0xff112233u);
    for (auto c : owned.nature.stem) REQUIRE(c == 0xff445566u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::SporeBurst, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (auto member : {&PixelFxNatureSettings::cap, &PixelFxNatureSettings::stem}) {
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = owned;
            (bad.nature.*member).assign(count, 0xff112233u);
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SporeBurst, bad));
            REQUIRE(fx.configurationValue() == value);
        }
        auto bad           = owned;
        bad.nature.*member = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::SporeBurst, bad));
        (bad.nature.*member).assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SporeBurst, bad));
    }
}
TEST_CASE("stylize.pixelfx.nature.sporeBurstLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::SporeBurst);
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
TEST_CASE("stylize.pixelfx.nature.sporeBurstPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::SporeBurst), reference(PixelFxKind::SporeBurst);
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

TEST_CASE("stylize.pixelfx.nature.sporeBurstStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::SporeBurst);
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


TEST_CASE("stylize.pixelfx.nature.thornsPinnedWaveGrowthSinkAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 1795177120u, 4249898605u},
                                {0.008333333333333333, 1795177120u, 4249898605u},
                                {0.11666666666666667, 1910489651u, 1807634525u},
                                {0.125, 703409792u, 1037526824u},
                                {0.19166666666666668, 534547874u, 3061085253u},
                                {0.2, 2968896962u, 1349212264u},
                                {0.26666666666666666, 569368396u, 4071527621u},
                                {0.275, 2183123000u, 1108036707u},
                                {0.3416666666666667, 2802763501u, 901572167u},
                                {0.35, 2913080487u, 1094554235u},
                                {0.4166666666666667, 1603185603u, 4253226901u},
                                {0.425, 3064564070u, 1074973079u},
                                {0.49166666666666664, 596677316u, 377333608u},
                                {0.5, 2477852733u, 1963143085u},
                                {0.5083333333333333, 3667211837u, 937031457u},
                                {1, 1017775176u, 1831534833u},
                                {1.4416666666666667, 377796339u, 557818309u},
                                {1.45, 1430277163u, 2922696107u},
                                {1.4583333333333333, 1613130974u, 44238591u},
                                {1.75, 1961389654u, 2444935598u},
                                {1.7583333333333333, 4011883848u, 4197585609u},
                                {2.1, 3094040313u, 1893850053u},
                                {2.1083333333333334, 2713742690u, 1893850053u},
                                {2.3, 2538686035u, 1893850053u},
                                {2.4, 1893850053u, 1893850053u},
                                {3, 1893850053u, 1893850053u},
                                {4, 1893850053u, 1893850053u},
                                {5, 1893850053u, 1893850053u}};
    PixelFx         fx(PixelFxKind::Thorns);
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
TEST_CASE("stylize.pixelfx.nature.thornsMaterialOwnershipBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::Thorns);
    auto    s = fx.settings();
    REQUIRE(s.nature.glow.size() == 4);
    REQUIRE(s.nature.soil.size() == 8);
    s.nature.glow = {0xff112233u};
    s.nature.soil = {0xff445566u};
    auto p        = PixelFx::prepareConfiguration(PixelFxKind::Thorns, s);
    REQUIRE(p);
    s.nature.glow[0] = 0xffabcdefu;
    s.nature.soil[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.nature.glow) REQUIRE(c == 0xff112233u);
    for (auto c : owned.nature.soil) REQUIRE(c == 0xff445566u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Thorns, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (auto member : {&PixelFxNatureSettings::glow, &PixelFxNatureSettings::soil}) {
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = owned;
            (bad.nature.*member).assign(count, 0xff112233u);
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Thorns, bad));
            REQUIRE(fx.configurationValue() == value);
        }
        auto bad           = owned;
        bad.nature.*member = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Thorns, bad));
        (bad.nature.*member).assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Thorns, bad));
    }
}
TEST_CASE("stylize.pixelfx.nature.thornsLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Thorns);
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
TEST_CASE("stylize.pixelfx.nature.thornsPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Thorns), reference(PixelFxKind::Thorns);
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

TEST_CASE("stylize.pixelfx.nature.thornsStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::Thorns);
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


TEST_CASE("stylize.pixelfx.nature.thornsCountSourceMinimumAndAtomicBounds") {
    PixelFx fx(PixelFxKind::Thorns), two(PixelFxKind::Thorns);
    auto    s = fx.settings();
    REQUIRE(s.nature.count == 6);
    s.nature.count = 1;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::Thorns, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    s.nature.count = 2;
    p              = PixelFx::prepareConfiguration(PixelFxKind::Thorns, s);
    REQUIRE(p);
    REQUIRE(two.commitConfiguration(std::move(p).takeValue()));
    for (double t : {.125, .5, 1.5, 2., 3.}) {
        auto a = fx.prepareSeek(t), b = two.prepareSeek(t);
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(fx.commitSeek(std::move(a).takeValue()));
        REQUIRE(two.commitSeek(std::move(b).takeValue()));
        fx.draw();
        two.draw();
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), two.color().begin()));
        REQUIRE(std::equal(fx.light().begin(), fx.light().end(), two.light().begin()));
    }
    const auto before = fx.configurationValue();
    for (int n : {0, 65}) {
        s.nature.count = n;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Thorns, s));
        REQUIRE(fx.configurationValue() == before);
    }
    s.nature.count = 64;
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Thorns, s));
}

TEST_CASE("stylize.pixelfx.nature.thornsMaximumCountSourceContinuationPastDuration") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{2.4, 2599007618u, 2981855375u},  {4.841666666666667, 2557215763u, 4075682681u},
                                {4.85, 1559036154u, 3621639890u}, {5, 668039506u, 1006343214u},
                                {5.5, 3041669269u, 1647324052u},  {6, 1893850053u, 1893850053u},
                                {8, 1893850053u, 1893850053u}};
    PixelFx         fx(PixelFxKind::Thorns);
    auto            s = fx.settings();
    s.seed            = 4294967295u;
    s.nature.count    = 64;
    auto p            = PixelFx::prepareConfiguration(PixelFxKind::Thorns, s);
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


TEST_CASE("stylize.pixelfx.nature.tornadoPinnedFunnelBandsCrownFlashAndTwoLoops") {
    // Raw alpha/light are strict; RGB-only one-LSB tolerance follows the measured source sin/blend trace.
    struct Frame {
        double                   time;
        uint32_t                 alpha, light;
        std::array<uint32_t, 15> colors;
    };
    constexpr Frame frames[] = {{0,
                                 96901037u,
                                 673014301u,
                                 {{0u, 2570264355u, 4269235298u, 4265618223u, 0u, 1640279475u, 1851934541u, 0u,
                                   1654555530u, 0u, 0u, 2622246492u, 0u, 2690282633u, 0u}}},
                                {0.008333333333333333,
                                 1947389195u,
                                 1686751348u,
                                 {{0u, 2570264355u, 4269498214u, 4251076429u, 0u, 1640279475u, 1835157325u, 0u,
                                   1621001098u, 0u, 0u, 2622904678u, 0u, 2673505417u, 0u}}},
                                {0.55,
                                 2666727059u,
                                 1872771148u,
                                 {{4267787852u, 4000668256u, 3696639299u, 0u, 0u, 0u, 2141094794u, 0u, 0u, 4215943478u,
                                   4283052342u, 1902338414u, 4278984778u, 2370527604u, 0u}}},
                                {0.5583333333333333,
                                 1204951586u,
                                 549095020u,
                                 {{0u, 3983825248u, 4066263881u, 0u, 0u, 0u, 2141094794u, 0u, 4278984778u, 4165611830u,
                                   4283052342u, 4283778639u, 4286239487u, 2404082037u, 0u}}},
                                {1,
                                 2157946633u,
                                 330475903u,
                                 {{0u, 3895664419u, 4190087342u, 4287723389u, 0u, 0u, 2090763146u, 0u, 4280357396u,
                                   1212821814u, 0u, 4286239487u, 0u, 1934319731u, 0u}}},
                                {1.0083333333333333,
                                 22084635u,
                                 2592312081u,
                                 {{0u, 3878887203u, 3886913436u, 4287920512u, 0u, 0u, 2090763146u, 0u, 4280357396u,
                                   1212821814u, 0u, 4278984778u, 0u, 2051760244u, 0u}}},
                                {1.2,
                                 894443937u,
                                 1826943136u,
                                 {{0u, 4257722550u, 4117714266u, 4253444978u, 0u, 0u, 4291079603u, 4051328613u, 0u,
                                   994718006u, 0u, 2622904936u, 0u, 4280357396u, 0u}}},
                                {1.2083333333333333,
                                 3234307343u,
                                 2328256721u,
                                 {{0u, 4257985979u, 4118569832u, 4272130704u, 0u, 0u, 4291079603u, 3531234917u, 0u,
                                   961163574u, 0u, 2606127720u, 0u, 2370330225u, 0u}}},
                                {1.2416666666666667,
                                 2342763556u,
                                 111744562u,
                                 {{0u, 4240221867u, 4088042905u, 4274565816u, 0u, 0u, 4291079603u, 0u, 0u, 0u, 0u,
                                   1919115630u, 0u, 2286444145u, 0u}}},
                                {1.25,
                                 2789625095u,
                                 630675056u,
                                 {{0u, 4273183905u, 4222128534u, 4240353197u, 0u, 0u, 4291079603u, 0u, 0u, 4294967295u,
                                   0u, 1919115630u, 0u, 3024707699u, 0u}}},
                                {1.375,
                                 1460748852u,
                                 856599372u,
                                 {{0u, 4136071028u, 4037315730u, 3953495443u, 0u, 0u, 3886122637u, 0u, 0u, 4278984778u,
                                   4280046760u, 2773043284u, 0u, 3729351029u, 0u}}},
                                {1.3833333333333333,
                                 697267998u,
                                 3591184389u,
                                 {{0u, 4220351866u, 4089885110u, 4121991326u, 0u, 0u, 3366358420u, 0u, 0u, 4281243368u,
                                   4278984778u, 3007792208u, 0u, 3762970997u, 0u}}},
                                {1.4916666666666667,
                                 1289420936u,
                                 2511475453u,
                                 {{0u, 4274236594u, 2570264355u, 2570264355u, 0u, 0u, 2110041523u, 0u, 0u, 0u, 0u,
                                   2891212666u, 0u, 3091882357u, 0u}}},
                                {1.5,
                                 3268293992u,
                                 2903792362u,
                                 {{0u, 4274236594u, 2570264355u, 2570264355u, 0u, 0u, 2110041523u, 0u, 0u, 0u, 0u,
                                   2656529021u, 4285172384u, 4280357396u, 0u}}},
                                {1.5083333333333333,
                                 3446747722u,
                                 2910134996u,
                                 {{0u, 4274236594u, 2570264355u, 2570264355u, 0u, 0u, 2107540362u, 0u, 0u, 0u, 0u,
                                   2622909052u, 4285172384u, 3058327925u, 0u}}},
                                {1.9916666666666667,
                                 308973307u,
                                 2752594494u,
                                 {{0u, 2570264355u, 4269564007u, 4219166566u, 0u, 1757193643u, 2171096403u, 0u,
                                   1671332746u, 0u, 0u, 2639023708u, 0u, 2740614281u, 0u}}},
                                {2,
                                 96901037u,
                                 673014301u,
                                 {{0u, 2570264355u, 4269235298u, 4265618223u, 0u, 1640279475u, 1851934541u, 0u,
                                   1654555530u, 0u, 0u, 2622246492u, 0u, 2690282633u, 0u}}},
                                {2.0083333333333333,
                                 1947389195u,
                                 1686751348u,
                                 {{0u, 2570264355u, 4269498214u, 4251076429u, 0u, 1640279475u, 1835157325u, 0u,
                                   1621001098u, 0u, 0u, 2622904678u, 0u, 2673505417u, 0u}}},
                                {2.55,
                                 2666727059u,
                                 1872771148u,
                                 {{4267787852u, 4000668256u, 3696639299u, 0u, 0u, 0u, 2141094794u, 0u, 0u, 4215943478u,
                                   4283052342u, 1902338414u, 4278984778u, 2370527604u, 0u}}},
                                {3,
                                 2157946633u,
                                 330475903u,
                                 {{0u, 3895664419u, 4190087342u, 4287723389u, 0u, 0u, 2090763146u, 0u, 4280357396u,
                                   1212821814u, 0u, 4286239487u, 0u, 1934319731u, 0u}}},
                                {3.0083333333333333,
                                 22084635u,
                                 2592312081u,
                                 {{0u, 3878887203u, 3886913436u, 4287920512u, 0u, 0u, 2090763146u, 0u, 4280357396u,
                                   1212821814u, 0u, 4278984778u, 0u, 2051760244u, 0u}}},
                                {3.9916666666666667,
                                 308973307u,
                                 2752594494u,
                                 {{0u, 2570264355u, 4269564007u, 4219166566u, 0u, 1757193643u, 2171096403u, 0u,
                                   1671332746u, 0u, 0u, 2639023708u, 0u, 2740614281u, 0u}}},
                                {4,
                                 96901037u,
                                 673014301u,
                                 {{0u, 2570264355u, 4269235298u, 4265618223u, 0u, 1640279475u, 1851934541u, 0u,
                                   1654555530u, 0u, 0u, 2622246492u, 0u, 2690282633u, 0u}}}};
    constexpr std::array<std::array<int, 2>, 15> points{{{-50, -112},
                                                         {-25, -112},
                                                         {0, -112},
                                                         {25, -112},
                                                         {50, -112},
                                                         {-20, -80},
                                                         {0, -80},
                                                         {20, -80},
                                                         {-12, -49},
                                                         {11, -49},
                                                         {12, -49},
                                                         {0, -10},
                                                         {-30, 0},
                                                         {0, 0},
                                                         {30, 0}}};
    PixelFx                                      fx(PixelFxKind::Tornado);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        for (size_t i = 0; i < points.size(); ++i) {
            const auto p = points[i];
            const auto a = fx.color()[size_t((p[1] + 152) * 196 + p[0] + 98)], e = f.colors[i];
            REQUIRE((a >> 24) == (e >> 24));
            for (int s : {0, 8, 16}) REQUIRE(std::abs(int((a >> s) & 255u) - int((e >> s) & 255u)) <= 1);
        }
    }
}
TEST_CASE("stylize.pixelfx.nature.tornadoMaterialHeightOwnershipAndBounds") {
    PixelFx fx(PixelFxKind::Tornado);
    auto    s = fx.settings();
    REQUIRE(s.nature.dust.size() == 5);
    REQUIRE(s.nature.leaves.size() == 8);
    REQUIRE(s.nature.height == 112);
    s.nature.dust   = {0xff112233u};
    s.nature.leaves = {0xff445566u};
    s.nature.height = 144;
    auto p          = PixelFx::prepareConfiguration(PixelFxKind::Tornado, s);
    REQUIRE(p);
    s.nature.dust[0]   = 0xffabcdefu;
    s.nature.leaves[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.nature.dust) REQUIRE(c == 0xff112233u);
    for (auto c : owned.nature.leaves) REQUIRE(c == 0xff445566u);
    const auto value = fx.configurationValue();
    for (auto member : {&PixelFxNatureSettings::dust, &PixelFxNatureSettings::leaves}) {
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = owned;
            (bad.nature.*member).assign(count, 0xff112233u);
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Tornado, bad));
            REQUIRE(fx.configurationValue() == value);
        }
        auto bad           = owned;
        bad.nature.*member = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Tornado, bad));
        (bad.nature.*member).assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Tornado, bad));
    }
    for (double h : {0., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad          = owned;
        bad.nature.height = h;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Tornado, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double h : {1., 256.}) {
        auto valid          = owned;
        valid.nature.height = h;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Tornado, valid));
    }
}
TEST_CASE("stylize.pixelfx.nature.tornadoLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::Tornado);
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
TEST_CASE("stylize.pixelfx.nature.tornadoAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::Tornado), reference(PixelFxKind::Tornado);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
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
    PixelFx fx(PixelFxKind::Tornado);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::Tornado, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::Tornado, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Tornado, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
}


TEST_CASE("stylize.pixelfx.nature.toxicPuddlePinnedPoolBubblesRibbonsAndTwoLoops") {
    // Alpha/light stay strict; RGB1 follows the measured unchanged-source ribbon blend trace.
    struct Frame {
        double                   time;
        uint32_t                 alpha, light;
        std::array<uint32_t, 15> colors;
    };
    constexpr Frame frames[] = {{0,
                                 3621636177u,
                                 3014387199u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 328646065u, 0u, 28096691u, 0u, 0u, 0u}}},
                                {0.008333333333333333,
                                 2968250479u,
                                 4085462878u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 613858481u, 0u, 0u, 0u, 0u, 0u}}},
                                {0.13333333333333333,
                                 124545710u,
                                 2120843863u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4280997030u, 4279511070u, 4280997030u, 630700977u, 0u, 0u, 0u, 0u, 0u}}},
                                {0.14166666666666666,
                                 25143131u,
                                 166710831u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4280997030u, 4279511070u, 4280997030u, 613924017u, 0u, 0u, 0u, 0u, 0u}}},
                                {0.16666666666666666,
                                 2178137332u,
                                 2690176936u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4280997030u, 4279511070u, 4280997030u, 513260465u, 0u, 0u, 0u, 0u, 0u}}},
                                {0.35,
                                 1230955161u,
                                 3560504220u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4287037420u, 4279511070u, 4280997030u, 0u, 0u, 0u, 0u, 0u, 0u}}},
                                {0.4083333333333333,
                                 224513159u,
                                 732333161u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4287037420u, 4279511070u, 4280997030u, 0u, 0u, 0u, 0u, 0u, 0u}}},
                                {1,
                                 3661531583u,
                                 682370128u,
                                 {{4280997030u, 4279511070u, 4279778350u, 4280997030u, 4278980620u, 4278980620u,
                                   4278980620u, 4279511070u, 4280997030u, 0u, 0u, 195999924u, 0u, 0u, 0u}}},
                                {1.5,
                                 3483757221u,
                                 4245910151u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4278980620u, 4280056410u, 4280997030u, 0u, 0u, 0u, 0u, 0u, 0u}}},
                                {2,
                                 628652001u,
                                 354310301u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4278980620u, 4280056410u, 4280997030u, 0u, 0u, 0u, 0u, 0u, 0u}}},
                                {3,
                                 1756639518u,
                                 734225650u,
                                 {{4280997030u, 4279511070u, 4279778350u, 4278980620u, 4287037420u, 4287037420u,
                                   4278980620u, 4279511070u, 4280997030u, 178043826u, 195148466u, 0u, 0u, 0u, 0u}}},
                                {3.5,
                                 986674215u,
                                 3745528844u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4278980620u, 4279778350u, 4280997030u, 0u, 0u, 28227764u, 0u, 0u, 0u}}},
                                {3.9916666666666667,
                                 363719238u,
                                 4048036474u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 328580529u, 0u, 28096691u, 0u, 0u, 0u}}},
                                {4,
                                 3621636177u,
                                 3014387199u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 328646065u, 0u, 28096691u, 0u, 0u, 0u}}},
                                {4.008333333333334,
                                 2968250479u,
                                 4085462878u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 613858481u, 0u, 0u, 0u, 0u, 0u}}},
                                {5,
                                 3661531583u,
                                 682370128u,
                                 {{4280997030u, 4279511070u, 4279778350u, 4280997030u, 4278980620u, 4278980620u,
                                   4278980620u, 4279511070u, 4280997030u, 0u, 0u, 195999924u, 0u, 0u, 0u}}},
                                {6,
                                 628652001u,
                                 354310301u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4278980620u, 4280056410u, 4280997030u, 0u, 0u, 0u, 0u, 0u, 0u}}},
                                {7,
                                 1756639518u,
                                 734225650u,
                                 {{4280997030u, 4279511070u, 4279778350u, 4278980620u, 4287037420u, 4287037420u,
                                   4278980620u, 4279511070u, 4280997030u, 178043826u, 195148466u, 0u, 0u, 0u, 0u}}},
                                {7.991666666666666,
                                 363719238u,
                                 4048036474u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 328580529u, 0u, 28096691u, 0u, 0u, 0u}}},
                                {8,
                                 3621636177u,
                                 3014387199u,
                                 {{4280997030u, 4279511070u, 4278980620u, 4278980620u, 4278980620u, 4278980620u,
                                   4279778350u, 4279511070u, 4280997030u, 328646065u, 0u, 28096691u, 0u, 0u, 0u}}}};
    constexpr std::array<std::array<int, 2>, 15> points{{{-40, 0},
                                                         {-30, -4},
                                                         {-20, -4},
                                                         {-10, -4},
                                                         {0, -4},
                                                         {10, -4},
                                                         {20, -4},
                                                         {30, -4},
                                                         {40, 0},
                                                         {-5, -21},
                                                         {0, -24},
                                                         {15, -30},
                                                         {-25, -30},
                                                         {-55, 0},
                                                         {55, 0}}};
    PixelFx                                      fx(PixelFxKind::ToxicPuddle);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        fx.render(f.time);
        REQUIRE(alphaChecksum(fx.color()) == f.alpha);
        REQUIRE(checksum(fx.light()) == f.light);
        for (size_t i = 0; i < points.size(); ++i) {
            const auto p = points[i];
            const auto a = fx.color()[size_t((p[1] + 86) * 176 + p[0] + 88)], e = f.colors[i];
            REQUIRE((a >> 24) == (e >> 24));
            for (int s : {0, 8, 16}) REQUIRE(std::abs(int((a >> s) & 255u) - int((e >> s) & 255u)) <= 1);
        }
    }
}
TEST_CASE("stylize.pixelfx.nature.toxicPuddleSheenWidthOwnershipAndBounds") {
    PixelFx fx(PixelFxKind::ToxicPuddle);
    auto    s = fx.settings();
    REQUIRE(s.nature.sheen.size() == 4);
    REQUIRE(s.nature.width == 42);
    s.nature.sheen = {0xff112233u};
    s.nature.width = 63.5;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, s);
    REQUIRE(p);
    s.nature.sheen[0] = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.nature.sheen) REQUIRE(c == 0xff112233u);
    REQUIRE(owned.nature.width == 63.5);
    const auto value = fx.configurationValue();
    for (size_t count : {size_t(0), size_t(65)}) {
        auto bad = owned;
        bad.nature.sheen.assign(count, 0xff112233u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, bad));
        REQUIRE(fx.configurationValue() == value);
    }
    auto bad         = owned;
    bad.nature.sheen = {0x80112233u};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, bad));
    bad.nature.sheen.assign(64, 0xff112233u);
    REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, bad));
    for (double w : {0., 257., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid         = owned;
        invalid.nature.width = w;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, invalid));
        REQUIRE(fx.configurationValue() == value);
    }
    for (double w : {1., 256.}) {
        auto valid         = owned;
        valid.nature.width = w;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, valid));
    }
}
TEST_CASE("stylize.pixelfx.nature.toxicPuddleLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::ToxicPuddle);
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
TEST_CASE("stylize.pixelfx.nature.toxicPuddleAnalyticClockConfigurationAndPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::ToxicPuddle), reference(PixelFxKind::ToxicPuddle);
        double  elapsed = 0;
        for (double target : {.13, .3, .6, 1.2, 1.201, 3.199, 3.2, 3.201, 6.4, 6.513}) {
            while (elapsed < target) {
                const double dt = std::min(step, target - elapsed);
                REQUIRE(live.advance(dt));
                elapsed += dt;
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
    PixelFx fx(PixelFxKind::ToxicPuddle);
    auto    settings = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff102030u, 0xfff0e0d0u};
    auto prepared    = PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, settings);
    REQUIRE(prepared);
    auto stale = fx.prepareSeek(.6);
    REQUIRE(stale);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    REQUIRE(fx.settings().palette[0] == 0xff102030u);
    const auto config = fx.configurationValue();
    auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::ToxicPuddle, config);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == config);
    auto seek = fx.prepareSeek(.6);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
    const auto time  = fx.effectTime();
    const auto color = checksum(fx.color()), light = checksum(fx.light());
    settings = fx.settings();
    settings.palette.clear();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ToxicPuddle, settings));
    REQUIRE(fx.configurationValue() == config);
    REQUIRE(fx.effectTime() == time);
    REQUIRE(checksum(fx.color()) == color);
    REQUIRE(checksum(fx.light()) == light);
}


TEST_CASE("stylize.pixelfx.nature.vineWhipPinnedLashCrackSapSinkAndTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {{0, 2049598174u, 509894085u},
                                {0.008333333333333333, 3686708459u, 509894085u},
                                {0.1, 3695034286u, 509894085u},
                                {0.13333333333333333, 1163624579u, 509894085u},
                                {0.14166666666666666, 1659427811u, 509894085u},
                                {0.2, 987292922u, 509894085u},
                                {0.35, 1427552233u, 509894085u},
                                {0.35833333333333334, 3205502309u, 509894085u},
                                {0.36666666666666664, 2600171812u, 509894085u},
                                {0.4, 1674410747u, 2278921754u},
                                {0.5, 4230450429u, 3328958799u},
                                {0.55, 2319167972u, 1103025980u},
                                {0.5583333333333333, 2830483104u, 1564208412u},
                                {0.5666666666666667, 249415120u, 4255500380u},
                                {0.6, 3221818641u, 548014848u},
                                {0.7, 3457718015u, 622900500u},
                                {0.8, 2898679880u, 2806365085u},
                                {1, 422989723u, 509894085u},
                                {1.075, 1541422241u, 509894085u},
                                {1.0833333333333333, 2713967809u, 509894085u},
                                {1.0916666666666666, 1594272133u, 509894085u},
                                {1.2, 3468972191u, 509894085u},
                                {1.3, 2166768128u, 509894085u},
                                {1.35, 509894085u, 509894085u},
                                {1.4, 509894085u, 509894085u},
                                {2, 509894085u, 509894085u},
                                {4, 509894085u, 509894085u}};
    PixelFx         fx(PixelFxKind::VineWhip);
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
TEST_CASE("stylize.pixelfx.nature.vineWhipMaterialOwnershipBoundsAndRoundTrip") {
    PixelFx fx(PixelFxKind::VineWhip);
    auto    s = fx.settings();
    REQUIRE(s.nature.thorn.size() == 4);
    REQUIRE(s.nature.soil.size() == 8);
    s.nature.thorn = {0xff112233u};
    s.nature.soil  = {0xff445566u};
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::VineWhip, s);
    REQUIRE(p);
    s.nature.thorn[0] = 0xffabcdefu;
    s.nature.soil[0]  = 0xffabcdefu;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    const auto owned = fx.settings();
    for (auto c : owned.nature.thorn) REQUIRE(c == 0xff112233u);
    for (auto c : owned.nature.soil) REQUIRE(c == 0xff445566u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::VineWhip, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (auto member : {&PixelFxNatureSettings::thorn, &PixelFxNatureSettings::soil}) {
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = owned;
            (bad.nature.*member).assign(count, 0xff112233u);
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::VineWhip, bad));
            REQUIRE(fx.configurationValue() == value);
        }
        auto bad           = owned;
        bad.nature.*member = {0x80112233u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::VineWhip, bad));
        (bad.nature.*member).assign(64, 0xff112233u);
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::VineWhip, bad));
    }
}
TEST_CASE("stylize.pixelfx.nature.vineWhipLayersReverseAndDrawPurity") {
    PixelFx    fx(PixelFxKind::VineWhip);
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
TEST_CASE("stylize.pixelfx.nature.vineWhipPartitionedReplayAndDrawPurity") {
    for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
        PixelFx live(PixelFxKind::VineWhip), reference(PixelFxKind::VineWhip);
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

TEST_CASE("stylize.pixelfx.nature.vineWhipStopRestartAndStableTail") {
    PixelFx fx(PixelFxKind::VineWhip);
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


}  // namespace test_pixelfx_nature_unit
