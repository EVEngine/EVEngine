// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": ["pixelfx_fire.cpp"]
}
*/
// clang-format on

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_pixelfx_fire)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Fire.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_fire_unit {
using namespace eve::stylize;
// Source: pinned upstream fire/torch.js and fire/candle.js, exact absolute-time samples i*D/8.
// Full raw planes also compared byte-for-byte by scripts/compare_pixelfx_frames.py.
TEST_CASE("stylize.pixelfx.fire.upstreamDefaults") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {668u, 62349u, 39379u, 31616u, 144794u},
            {7680u, 203703u, 149797u, 78301u, 1958400u},
            {681u, 59267u, 35964u, 30715u, 143062u},
            {7748u, 242662u, 178505u, 93625u, 1975740u},
            {708u, 69471u, 41195u, 32185u, 156045u},
            {7748u, 241960u, 178129u, 93491u, 1975740u},
            {738u, 68694u, 44192u, 35477u, 153069u},
            {7746u, 246004u, 182030u, 96205u, 1975230u},
            {763u, 66424u, 40364u, 33860u, 149494u},
            {7759u, 254449u, 188312u, 99515u, 1978545u},
            {697u, 64680u, 39679u, 30402u, 148447u},
            {7545u, 153100u, 112492u, 57489u, 1923975u},
            {724u, 66316u, 39137u, 31162u, 149163u},
            {7632u, 179146u, 131496u, 67890u, 1946160u},
            {713u, 63932u, 40158u, 32199u, 146812u},
            {7640u, 183974u, 134876u, 70118u, 1948200u},
            {668u, 62349u, 39379u, 31616u, 144794u},
            {7680u, 203703u, 149797u, 78301u, 1958400u},
        },
        {
            {300u, 47409u, 37927u, 27116u, 72755u},
            {2850u, 68244u, 52460u, 29657u, 726750u},
            {285u, 47490u, 37622u, 26475u, 72675u},
            {2347u, 48971u, 37847u, 21195u, 598485u},
            {288u, 48559u, 38908u, 27298u, 73440u},
            {2695u, 61447u, 47289u, 26618u, 687225u},
            {296u, 50340u, 39813u, 27678u, 75480u},
            {3088u, 79370u, 60852u, 34442u, 787440u},
            {290u, 48729u, 39201u, 27795u, 73548u},
            {2307u, 47501u, 36692u, 20595u, 588285u},
            {299u, 50086u, 40175u, 28229u, 74993u},
            {3063u, 78295u, 60039u, 34011u, 781065u},
            {305u, 49132u, 39723u, 28199u, 73950u},
            {2973u, 74064u, 56819u, 32132u, 758115u},
            {299u, 48076u, 38743u, 27657u, 73544u},
            {3128u, 81456u, 62449u, 35352u, 797640u},
            {300u, 47409u, 37927u, 27116u, 72755u},
            {2850u, 68244u, 52460u, 29657u, 726750u},
        },
    };
    const std::array kinds{PixelFxKind::Torch, PixelFxKind::Candle};
    for (size_t family = 0; family < kinds.size(); ++family) {
        PixelFx fx(kinds[family]);
        REQUIRE(fx.settings().palette.size() == 9u);
        for (int i = 0; i < 9; ++i) {
            fx.render(double(i) * fx.info().duration / 8);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[family][size_t(i) * 2 + plane][m],
                               delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.fire.typedParametersAndAtomicRamps") {
    for (auto kind : {PixelFxKind::Torch, PixelFxKind::Candle}) {
        PixelFx fx(kind);
        fx.render(0.5);
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        s = fx.settings();
        s.fire.height += 8;
        fx.configure(s);
        fx.render(0.5);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) != before);
        s = PixelFx::defaults(kind);
        if (kind == PixelFxKind::Torch)
            s.fire.wood[4] = 0xffff0000u;
        else
            s.fire.wax[4] = 0xffff0000u;
        fx.configure(s);
        fx.render(0.5);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) != before);
        const auto valid = fx.settings();
        if (kind == PixelFxKind::Torch)
            s.fire.wood.clear();
        else
            s.fire.wax.clear();
        bool rejected = false;
        try {
            fx.configure(s);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        REQUIRE(rejected);
        REQUIRE(fx.settings().fire.wood == valid.fire.wood);
        REQUIRE(fx.settings().fire.wax == valid.fire.wax);
        s         = valid;
        s.palette = {0xff000000u, 0xffffffffu};
        fx.configure(s);
        REQUIRE(fx.settings().palette.size() == 9u);
        REQUIRE(fx.settings().palette.front() == 0xff000000u);
        REQUIRE(fx.settings().palette.back() == 0xffffffffu);
    }
}
TEST_CASE("stylize.pixelfx.fire.brazierReferenceAndIron") {
    constexpr uint64_t expected[18][5] = {
        {1512u, 183727u, 113808u, 83495u, 312972u}, {12956u, 383701u, 284069u, 148876u, 3303780u},
        {1493u, 190869u, 118045u, 83836u, 323627u}, {12813u, 322841u, 240269u, 125504u, 3267315u},
        {1500u, 171327u, 104129u, 77194u, 304644u}, {13089u, 459808u, 340609u, 179298u, 3337695u},
        {1467u, 158614u, 95102u, 72590u, 291726u},  {12858u, 321717u, 237869u, 123619u, 3278790u},
        {1404u, 155887u, 94004u, 71572u, 278625u},  {13147u, 490116u, 363497u, 191854u, 3352485u},
        {1414u, 175896u, 104538u, 74982u, 301902u}, {13217u, 466666u, 345071u, 181646u, 3370335u},
        {1459u, 170520u, 104686u, 77524u, 293250u}, {12956u, 382348u, 282760u, 147970u, 3303780u},
        {1507u, 171123u, 105159u, 80533u, 299311u}, {12833u, 325889u, 240501u, 124398u, 3272415u},
        {1512u, 183727u, 113808u, 83495u, 312972u}, {12956u, 383701u, 284069u, 148876u, 3303780u},
    };
    PixelFx fx(PixelFxKind::Brazier);
    for (int i = 0; i < 9; ++i) {
        fx.render(double(i) * 3 / 8);
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
    fx.render(0.5);
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.fire.iron[2]                = 0xffff8000u;
    fx.configure(settings);
    fx.render(0.5);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) != before);
    settings.fire.iron.clear();
    bool rejected = false;
    try {
        fx.configure(settings);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    REQUIRE(rejected);
    REQUIRE(fx.settings().fire.iron.size() == 5u);
}

TEST_CASE("stylize.pixelfx.fire.spiritFlameReferenceAndPalette") {
    constexpr uint64_t expected[18][5] = {
        {908u, 58753u, 148236u, 146115u, 142114u}, {7386u, 56937u, 151301u, 140687u, 1883430u},
        {757u, 51463u, 126747u, 124318u, 124523u}, {7316u, 53497u, 142108u, 132183u, 1865580u},
        {703u, 47601u, 116046u, 114272u, 120049u}, {7292u, 55443u, 147530u, 137251u, 1859460u},
        {690u, 53451u, 118352u, 116127u, 135936u}, {7336u, 57766u, 154870u, 144021u, 1870680u},
        {779u, 50931u, 128149u, 126081u, 135776u}, {7386u, 56409u, 151364u, 140696u, 1883430u},
        {825u, 57489u, 138472u, 135898u, 139621u}, {7392u, 50985u, 138032u, 128221u, 1884960u},
        {886u, 56329u, 146881u, 143939u, 152776u}, {7436u, 52185u, 141628u, 131595u, 1896180u},
        {907u, 60041u, 150216u, 147599u, 149647u}, {7432u, 55490u, 148086u, 137641u, 1895160u},
        {908u, 58753u, 148236u, 146115u, 142114u}, {7386u, 56937u, 151301u, 140687u, 1883430u},
    };
    PixelFx fx(PixelFxKind::SpiritFlame);
    for (int i = 0; i < 9; ++i) {
        fx.render(double(i) * 3 / 8);
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
    fx.render(0.5);
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.seed                        = 123;
    fx.configure(settings);
    fx.render(0.5);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) != before);
    settings.palette = {0xff000000u, 0xff80ffffu};
    fx.configure(settings);
    REQUIRE(fx.settings().palette.size() == 9u);
    fx.render(0.5);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) != before);
}

TEST_CASE("stylize.pixelfx.fire.projectileAndVortexReference") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {1015u, 163374u, 73648u, 46848u, 188105u},
            {3750u, 106968u, 78103u, 40660u, 956250u},
            {820u, 148921u, 69450u, 38777u, 169560u},
            {3764u, 127101u, 94954u, 50528u, 959820u},
            {905u, 159891u, 77534u, 47905u, 178318u},
            {3720u, 117521u, 86707u, 45702u, 948600u},
            {893u, 156789u, 76054u, 44247u, 181165u},
            {3726u, 115661u, 85788u, 44245u, 950130u},
            {887u, 157813u, 72315u, 42752u, 175031u},
            {3779u, 112838u, 83091u, 43747u, 963645u},
            {910u, 159645u, 76180u, 50000u, 183421u},
            {3714u, 99574u, 73939u, 39588u, 947070u},
            {964u, 155815u, 78869u, 53195u, 176243u},
            {3743u, 125073u, 93562u, 49016u, 954465u},
            {971u, 161179u, 83295u, 52644u, 177738u},
            {3723u, 107949u, 81195u, 43891u, 949365u},
            {1015u, 163374u, 73648u, 46848u, 188105u},
            {3750u, 106968u, 78103u, 40660u, 956250u},
        },
        {
            {2214u, 433497u, 212979u, 109092u, 482955u},
            {13796u, 378724u, 276371u, 141115u, 3517980u},
            {2353u, 458391u, 250484u, 134519u, 496238u},
            {14105u, 421923u, 308687u, 158601u, 3596775u},
            {2393u, 456160u, 253145u, 137910u, 496617u},
            {14099u, 425788u, 311781u, 160408u, 3595245u},
            {2379u, 456641u, 257123u, 135422u, 493010u},
            {14051u, 438594u, 321601u, 165890u, 3583005u},
            {2346u, 453599u, 273431u, 143583u, 480769u},
            {14230u, 471654u, 346092u, 178967u, 3628650u},
            {2206u, 428576u, 201804u, 97600u, 474878u},
            {13767u, 362244u, 264435u, 135273u, 3510585u},
            {2208u, 439536u, 206631u, 104043u, 491814u},
            {13741u, 358301u, 261180u, 133191u, 3503955u},
            {2204u, 436698u, 238918u, 121012u, 474333u},
            {13916u, 417018u, 304709u, 156363u, 3548580u},
            {2214u, 433497u, 212979u, 109092u, 482955u},
            {13796u, 378724u, 276371u, 141115u, 3517980u},
        },
    };
    size_t family = 0;
    for (auto kind : {PixelFxKind::Fireball, PixelFxKind::FireTornado}) {
        PixelFx fx(kind);
        for (int i = 0; i < 9; ++i) {
            const double t = std::round(double(i) * fx.info().duration * 120 / 8) / 120;
            fx.render(t);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[family][size_t(i) * 2 + plane][m],
                               delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        ++family;
    }
}
TEST_CASE("stylize.pixelfx.fire.physicalOptionsAndPlayback") {
    PixelFx fx(PixelFxKind::Fireball);
    fx.render(0.4);
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.fire.radius                 = 8;
    settings.fire.speed                  = 200;
    fx.configure(settings);
    fx.render(0.4);
    const std::vector<uint32_t> variant(fx.color().begin(), fx.color().end());
    REQUIRE(variant != before);
    settings.playbackSpeed = 2;
    fx.configure(settings);
    fx.render(0.2);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == variant);
    for (double bad : {0.0, 501.0, std::numeric_limits<double>::quiet_NaN()}) {
        auto invalid       = settings;
        invalid.fire.speed = bad;
        bool rejected      = false;
        try {
            fx.configure(invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        REQUIRE(rejected);
        REQUIRE(fx.settings().fire.speed == 200);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == variant);
    }
    PixelFx tornado(PixelFxKind::FireTornado);
    tornado.render(0.4);
    const std::vector<uint32_t> tall(tornado.color().begin(), tornado.color().end());
    auto                        shorter = tornado.settings();
    shorter.fire.height                 = 48;
    tornado.configure(shorter);
    tornado.render(0.4);
    REQUIRE(std::vector<uint32_t>(tornado.color().begin(), tornado.color().end()) != tall);
}

TEST_CASE("stylize.pixelfx.fire.streamReferenceAndAbsolutePhase") {
    constexpr uint64_t expected[2][18][5] = {
        {
            {4330u, 618804u, 365011u, 258015u, 739128u},
            {16276u, 328201u, 240093u, 119017u, 4150380u},
            {4257u, 560423u, 328996u, 245500u, 649483u},
            {16599u, 386371u, 280069u, 141698u, 4232745u},
            {3899u, 532130u, 304077u, 222891u, 619791u},
            {16687u, 417375u, 303081u, 153362u, 4255185u},
            {3686u, 536312u, 302276u, 207827u, 635757u},
            {16624u, 408074u, 294915u, 147868u, 4239120u},
            {3409u, 550325u, 306516u, 189518u, 646301u},
            {16552u, 390435u, 284981u, 143946u, 4220760u},
            {3239u, 578425u, 316619u, 191922u, 676651u},
            {16480u, 376269u, 273898u, 135764u, 4202400u},
            {3719u, 608151u, 345746u, 227910u, 746737u},
            {16429u, 362906u, 263201u, 129219u, 4189395u},
            {4088u, 591026u, 338646u, 248078u, 732136u},
            {16362u, 338379u, 244343u, 121990u, 4172310u},
            {4330u, 618804u, 365011u, 258015u, 739128u},
            {16276u, 328201u, 240093u, 119017u, 4150380u},
        },
        {
            {2668u, 438738u, 268700u, 177973u, 556511u},
            {11031u, 523466u, 383997u, 202499u, 2812905u},
            {2506u, 422945u, 261573u, 170434u, 537065u},
            {11032u, 523180u, 381148u, 200844u, 2813160u},
            {2621u, 437554u, 264237u, 175615u, 545717u},
            {11016u, 524664u, 383129u, 201466u, 2809080u},
            {2545u, 412248u, 253848u, 171693u, 533314u},
            {8791u, 393622u, 286660u, 147924u, 2241705u},
            {2295u, 355312u, 220688u, 149498u, 474281u},
            {11272u, 505775u, 369942u, 195512u, 2874360u},
            {2492u, 376201u, 227335u, 160192u, 498773u},
            {6513u, 311337u, 228246u, 121389u, 1660815u},
            {2450u, 398672u, 249706u, 171804u, 515883u},
            {11096u, 529366u, 388680u, 206651u, 2829480u},
            {2572u, 420886u, 268471u, 179220u, 529288u},
            {11045u, 520796u, 380588u, 200993u, 2816475u},
            {2668u, 438738u, 268700u, 177973u, 556511u},
            {11031u, 523466u, 383997u, 202499u, 2812905u},
        },
    };
    size_t family = 0;
    for (auto kind : {PixelFxKind::FireBreath, PixelFxKind::Flamethrower}) {
        PixelFx fx(kind);
        for (int i = 0; i < 9; ++i) {
            const double t = std::round(double(i) * fx.info().duration * 120 / 8) / 120;
            fx.render(t);
            size_t plane = 0;
            for (auto pixels : {fx.color(), fx.light()}) {
                std::array<uint64_t, 5> actual{};
                for (auto pixel : pixels) {
                    actual[0] += pixel != 0 ? 1u : 0u;
                    for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
                }
                for (size_t m = 0; m < 5; ++m) {
                    const auto a = actual[m], b = expected[family][size_t(i) * 2 + plane][m],
                               delta = a > b ? a - b : b - a;
                    REQUIRE(delta <= (m == 0 ? 4u : 1020u));
                }
                ++plane;
            }
        }
        ++family;
    }
}
TEST_CASE("stylize.pixelfx.fire.streamOptionsAndAtomicPilot") {
    for (auto kind : {PixelFxKind::FireBreath, PixelFxKind::Flamethrower}) {
        PixelFx fx(kind);
        fx.render(0.6);
        const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
        auto                        settings = fx.settings();
        settings.seed                        = 123;
        settings.fire.speed                  = 210;
        if (kind == PixelFxKind::FireBreath)
            settings.fire.spread = 0.36;
        else {
            settings.fire.reach = 90;
            settings.fire.pilot = {0xff823a27u, 0xffd8914cu, 0xfffff0d2u};
        }
        fx.configure(settings);
        fx.render(0.6);
        const std::vector<uint32_t> variant(fx.color().begin(), fx.color().end());
        REQUIRE(variant != before);
        settings.playbackSpeed = 2;
        fx.configure(settings);
        fx.render(0.3);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == variant);
        auto invalid = settings;
        if (kind == PixelFxKind::FireBreath)
            invalid.fire.spread = 1.1;
        else
            invalid.fire.pilot.clear();
        bool rejected = false;
        try {
            fx.configure(invalid);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        REQUIRE(rejected);
        REQUIRE(fx.settings().fire.spread == settings.fire.spread);
        REQUIRE(fx.settings().fire.pilot == settings.fire.pilot);
        REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == variant);
    }
    PixelFx fx(PixelFxKind::Flamethrower);
    auto    s    = fx.settings();
    s.fire.pilot = {0xff000000u, 0xffffffffu};
    fx.configure(s);
    REQUIRE(fx.settings().fire.pilot.size() == 3u);
    REQUIRE(fx.settings().fire.pilot.front() == 0xff000000u);
    REQUIRE(fx.settings().fire.pilot.back() == 0xffffffffu);
}

TEST_CASE("stylize.pixelfx.fire.flamePillarReference") {
    constexpr uint64_t expected[18][5] = {
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {4693u, 1071048u, 760869u, 522531u, 1196485u},
        {23709u, 1657284u, 1259140u, 697066u, 6045795u},
        {5592u, 1085911u, 776848u, 580664u, 1305673u},
        {22090u, 999541u, 758872u, 415857u, 5632950u},
        {6197u, 1090618u, 785101u, 579516u, 1260067u},
        {22221u, 1000079u, 756421u, 411534u, 5666355u},
        {3085u, 465933u, 251714u, 164182u, 586982u},
        {4641u, 238865u, 183492u, 99614u, 1183455u},
        {2528u, 221471u, 164281u, 169512u, 370814u},
        {1587u, 99696u, 73271u, 36666u, 404685u},
        {2188u, 107516u, 66600u, 80958u, 200038u},
        {1100u, 13336u, 9045u, 3693u, 280500u},
        {307u, 5499u, 4578u, 6918u, 49535u},
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
    };
    PixelFx fx(PixelFxKind::FlamePillar);
    for (int i = 0; i < 9; ++i) {
        const double time      = std::round(double(i) * 5 * 120 / 8) / 120;
        auto         candidate = fx.prepareSeek(time);
        REQUIRE(candidate);
        auto applied = fx.commitSeek(std::move(candidate).takeValue());
        REQUIRE(applied);
        fx.draw();
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
}
// Pinned upstream campfire.js absolute-time samples; full spatial planes are checked by the probe.
TEST_CASE("stylize.pixelfx.fire.campfireReferenceAndMaterials") {
    constexpr uint64_t expected[18][5] = {
        {1445u, 160442u, 94029u, 71014u, 288855u}, {8350u, 318347u, 234751u, 121473u, 2129250u},
        {1489u, 160140u, 93742u, 70715u, 280827u}, {8185u, 270729u, 200365u, 103204u, 2087175u},
        {1445u, 160448u, 93364u, 68485u, 277150u}, {8548u, 308637u, 227387u, 116645u, 2179740u},
        {1337u, 153592u, 89931u, 63124u, 272538u}, {8687u, 301371u, 221870u, 114227u, 2215185u},
        {1417u, 152619u, 88761u, 64342u, 266717u}, {9140u, 349853u, 257201u, 132782u, 2330700u},
        {1422u, 149422u, 91113u, 68351u, 268499u}, {8424u, 290129u, 218962u, 116305u, 2148120u},
        {1397u, 159850u, 94440u, 69901u, 283982u}, {8445u, 293952u, 216423u, 111009u, 2153475u},
        {1430u, 157369u, 92357u, 69323u, 290784u}, {7809u, 236608u, 174225u, 88464u, 1991295u},
        {1445u, 160442u, 94029u, 71014u, 288855u}, {8350u, 318347u, 234751u, 121473u, 2129250u},
    };
    PixelFx fx(PixelFxKind::Campfire);
    REQUIRE(fx.settings().seed == 3u);
    REQUIRE(fx.settings().fire.height == 22);
    REQUIRE(fx.settings().fire.wood.size() == 6u);
    for (int i = 0; i < 9; ++i) {
        fx.render(double(i) * 3 / 8);
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
    fx.render(.6);
    const std::vector<uint32_t> before(fx.color().begin(), fx.color().end());
    auto                        settings = fx.settings();
    settings.seed                        = 123;
    settings.fire.height                 = 35;
    settings.fire.wood = {0xff302010u, 0xff564023u, 0xff70603au, 0xff908060u, 0xffb8b090u, 0xffdae0c8u};
    auto prepared      = PixelFx::prepareConfiguration(PixelFxKind::Campfire, settings);
    REQUIRE(prepared);
    auto applied = fx.commitConfiguration(std::move(prepared).takeValue());
    REQUIRE(applied);
    fx.render(.6);
    const std::vector<uint32_t> changed(fx.color().begin(), fx.color().end());
    REQUIRE(changed != before);
    auto invalid = fx.configurationValue();
    invalid.find("options")->set("wood", eve::Value::array({eve::Value(int64_t(0x00102030))}));
    auto rejected = PixelFx::prepareConfigurationValue(PixelFxKind::Campfire, invalid);
    REQUIRE(!rejected);
    REQUIRE(std::vector<uint32_t>(fx.color().begin(), fx.color().end()) == changed);
    REQUIRE(fx.settings().fire.wood == settings.fire.wood);
}

TEST_CASE("stylize.pixelfx.fire.fireRingReferenceAndReach") {
    constexpr uint64_t expected[18][5] = {
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {1908u, 336331u, 212541u, 115145u, 370844u},
        {16912u, 559494u, 400383u, 207916u, 4312560u},
        {6335u, 902507u, 528569u, 300713u, 1224138u},
        {18390u, 408211u, 292471u, 148980u, 4689450u},
        {6093u, 492436u, 262938u, 192805u, 910543u},
        {17428u, 244910u, 175826u, 87820u, 4444140u},
        {5780u, 212904u, 126276u, 152192u, 651450u},
        {13324u, 55350u, 37953u, 16066u, 3397620u},
        {5518u, 105691u, 82102u, 134029u, 530134u},
        {0u, 0u, 0u, 0u, 0u},
        {5382u, 97681u, 76181u, 127198u, 457008u},
        {0u, 0u, 0u, 0u, 0u},
        {5312u, 95776u, 74496u, 125008u, 121116u},
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
        {0u, 0u, 0u, 0u, 0u},
    };
    PixelFx fx(PixelFxKind::FireRing);
    REQUIRE(fx.settings().seed == 6u);
    REQUIRE(fx.settings().fire.reach == 92);
    for (int i = 0; i < 9; ++i) {
        auto seek = fx.prepareSeek(std::round(double(i) * 3.9 * 120 / 8) / 120);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        size_t plane = 0;
        for (auto pixels : {fx.color(), fx.light()}) {
            std::array<uint64_t, 5> actual{};
            for (auto pixel : pixels) {
                actual[0] += pixel != 0 ? 1u : 0u;
                for (size_t c = 0; c < 4; ++c) actual[c + 1] += (pixel >> (c * 8)) & 255u;
            }
            for (size_t m = 0; m < 5; ++m) {
                const auto a = actual[m], b = expected[size_t(i) * 2 + plane][m], delta = a > b ? a - b : b - a;
                REQUIRE(delta <= (m == 0 ? 4u : 1020u));
            }
            ++plane;
        }
    }
    PixelFx whole(PixelFxKind::FireRing), split(PixelFxKind::FireRing);
    REQUIRE(whole.advance(.75));
    for (int i = 0; i < 90; ++i) REQUIRE(split.advance(1.0 / 120));
    whole.draw();
    split.draw();
    REQUIRE(std::vector<uint32_t>(whole.color().begin(), whole.color().end()) ==
            std::vector<uint32_t>(split.color().begin(), split.color().end()));
    auto settings       = whole.settings();
    settings.seed       = 123;
    settings.fire.reach = 70;
    auto prepared       = PixelFx::prepareConfiguration(PixelFxKind::FireRing, settings);
    REQUIRE(prepared);
    REQUIRE(whole.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(whole.advance(.75));
    whole.draw();
    REQUIRE(std::vector<uint32_t>(whole.color().begin(), whole.color().end()) !=
            std::vector<uint32_t>(split.color().begin(), split.color().end()));
    auto invalid = whole.configurationValue();
    invalid.find("options")->set("reach", eve::Value(201.0));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::FireRing, invalid));
    REQUIRE(whole.settings().fire.reach == 70);
}

// Numeric fixtures from pinned upstream _lib.js::sparkAt, including its bounded impact loop.
TEST_CASE("stylize.pixelfx.fire.sparkFloorTrajectory") {
    constexpr double expected[5][5] = {
        {0, -3, -6, 90, -140},
        {.2, 11.562146410235126, -21.238270765266762, 57.96327789748272, -21.075804316413127},
        {.6, 26.980828555942484, -.8379241849380037, 24.04217717692653, 104.84343320686362},
        {1, 30.934905089411817, -.25, 3.4204934986452455, 0},
        {2, 32.317458575732125, -.25, .3790014828077226, 0}};
    for (const auto& q : expected) {
        const auto                  m = pixelfx::fire::sparkAt(-3, -6, 90, -140, q[0]);
        const std::array<double, 4> actual{m.x, m.y, m.vx, m.vy};
        for (size_t i = 0; i < actual.size(); ++i) REQUIRE(std::abs(actual[i] - q[i + 1]) < 1e-10);
    }
}
}  // namespace test_pixelfx_fire_unit
#endif
