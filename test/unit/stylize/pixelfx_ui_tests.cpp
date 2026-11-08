// Pinned upstream melee geometry and raster fixtures; no runtime source interpreter required.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/UiWord.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_ui_unit {
using namespace eve::stylize;
namespace ui = eve::stylize::pixelfx::ui;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.ui.damageNumberPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 4230112899u, 2511639429u},
                                    {0.008333333333333333, 3511419589u, 335872495u},
                                    {0.03333333333333333, 208441145u, 366177528u},
                                    {0.041666666666666664, 2007160013u, 281733564u},
                                    {0.06666666666666667, 3611537482u, 2858624801u},
                                    {0.075, 756085266u, 3915740617u},
                                    {0.1, 1530123934u, 574826925u},
                                    {0.10833333333333334, 2694068533u, 3380049803u},
                                    {0.2, 3007091833u, 1472721541u},
                                    {0.3, 2242997911u, 1537181016u},
                                    {0.375, 1433975941u, 991123733u},
                                    {0.38333333333333336, 1500720578u, 273446369u},
                                    {0.4, 828245503u, 4081433482u},
                                    {0.4083333333333333, 2745322534u, 3232834530u},
                                    {0.5, 2285673035u, 175154916u},
                                    {0.6, 208876793u, 2640621069u},
                                    {0.8333333333333334, 208876793u, 4170171741u},
                                    {0.8416666666666667, 208876793u, 2335224306u},
                                    {1, 2427281048u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 4139163127u, 2651370393u},
                                    {0.008333333333333333, 3369696347u, 3697075952u},
                                    {0.03333333333333333, 192325855u, 224521341u},
                                    {0.041666666666666664, 3504626161u, 440565256u},
                                    {0.06666666666666667, 287136578u, 4238673044u},
                                    {0.075, 3714572457u, 3747617884u},
                                    {0.1, 1885336176u, 3286646693u},
                                    {0.10833333333333334, 103038675u, 875080283u},
                                    {0.2, 3432687934u, 3718543446u},
                                    {0.3, 2972348190u, 109905937u},
                                    {0.375, 4267678002u, 4261954503u},
                                    {0.38333333333333336, 2495990870u, 3047942692u},
                                    {0.4, 547248582u, 454891939u},
                                    {0.4083333333333333, 2819189997u, 4205351893u},
                                    {0.5, 202498542u, 3091180272u},
                                    {0.6, 3873920677u, 2411006361u},
                                    {0.8333333333333334, 3873920677u, 2411006361u},
                                    {0.8416666666666667, 3873920677u, 526890057u},
                                    {1, 4062003965u, 1997381231u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_text = eve::Value::fromJson(R"json("+999")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 413743573u, 2511639429u},
                                    {0.008333333333333333, 881068003u, 2704886231u},
                                    {0.03333333333333333, 2182164925u, 3260877329u},
                                    {0.041666666666666664, 1583406311u, 2756853489u},
                                    {0.06666666666666667, 2495624133u, 2598733335u},
                                    {0.075, 1327396883u, 3530935490u},
                                    {0.1, 1834032858u, 1069336373u},
                                    {0.10833333333333334, 3017234015u, 1215483503u},
                                    {0.2, 4091727209u, 4218113145u},
                                    {0.3, 2242997911u, 1537181016u},
                                    {0.375, 1433975941u, 991123733u},
                                    {0.38333333333333336, 3746546454u, 4044752385u},
                                    {0.4, 3243730051u, 3577187617u},
                                    {0.4083333333333333, 4088659054u, 2867855076u},
                                    {0.5, 950687939u, 1232286530u},
                                    {0.6, 208876793u, 534047689u},
                                    {0.8333333333333334, 208876793u, 4170171741u},
                                    {0.8416666666666667, 208876793u, 2335224306u},
                                    {1, 2427281048u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2029191u, 2511639429u},
                                    {0.008333333333333333, 2609929479u, 1576788365u},
                                    {0.03333333333333333, 2616183419u, 849938731u},
                                    {0.041666666666666664, 2058131141u, 3809703480u},
                                    {0.06666666666666667, 4170291072u, 1480639765u},
                                    {0.075, 711586236u, 2252789083u},
                                    {0.1, 715209697u, 1516985292u},
                                    {0.10833333333333334, 1580676411u, 3348145509u},
                                    {0.2, 652212332u, 2183556851u},
                                    {0.3, 2242997911u, 1537181016u},
                                    {0.375, 1433975941u, 991123733u},
                                    {0.38333333333333336, 4169809884u, 1965527869u},
                                    {0.4, 2520709835u, 3655242362u},
                                    {0.4083333333333333, 3709023458u, 3091104443u},
                                    {0.5, 2421789825u, 2893510009u},
                                    {0.6, 208876793u, 1659883224u},
                                    {0.8333333333333334, 208876793u, 4170171741u},
                                    {0.8416666666666667, 208876793u, 2335224306u},
                                    {1, 2427281048u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2601842925u, 4174686781u},
                                    {0.008333333333333333, 167643257u, 1745907954u},
                                    {0.03333333333333333, 609287333u, 1418590224u},
                                    {0.041666666666666664, 3190637169u, 760393781u},
                                    {0.06666666666666667, 3983001081u, 4128377419u},
                                    {0.075, 1533565537u, 274663522u},
                                    {0.1, 1718867249u, 1936361065u},
                                    {0.10833333333333334, 4101164001u, 1324432996u},
                                    {0.2, 653423525u, 2329463092u},
                                    {0.3, 777593713u, 1885869772u},
                                    {0.375, 1262564117u, 3229219292u},
                                    {0.38333333333333336, 2704572853u, 2925175729u},
                                    {0.4, 442739529u, 832828391u},
                                    {0.4083333333333333, 1438306917u, 2068263953u},
                                    {0.5, 586791485u, 2283479120u},
                                    {0.6, 522649717u, 112417509u},
                                    {0.8333333333333334, 522649717u, 3261463141u},
                                    {0.8416666666666667, 522649717u, 3182223013u},
                                    {1, 1913544969u, 1175592507u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2386898077u, 815061805u},
                                    {0.008333333333333333, 1047987924u, 1231154724u},
                                    {0.03333333333333333, 2160504433u, 1011753509u},
                                    {0.041666666666666664, 3161356739u, 274961835u},
                                    {0.06666666666666667, 3738612177u, 4184227821u},
                                    {0.075, 1848472074u, 2900365390u},
                                    {0.1, 2036936865u, 1494610734u},
                                    {0.10833333333333334, 990542944u, 3332385394u},
                                    {0.2, 2979257817u, 3355132982u},
                                    {0.3, 2992871392u, 1013792789u},
                                    {0.375, 2093936021u, 3782031505u},
                                    {0.38333333333333336, 1338052116u, 1704589532u},
                                    {0.4, 3493225909u, 565360572u},
                                    {0.4083333333333333, 594291990u, 1304909209u},
                                    {0.5, 3897532716u, 454483861u},
                                    {0.6, 1587407257u, 3601298563u},
                                    {0.8333333333333334, 1587407257u, 3501212017u},
                                    {0.8416666666666667, 1587407257u, 3561737376u},
                                    {1, 1617981224u, 1649768579u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2828661085u, 2511639429u},
                                    {0.008333333333333333, 2287201023u, 335872495u},
                                    {0.03333333333333333, 191831257u, 366177528u},
                                    {0.041666666666666664, 4065261353u, 281733564u},
                                    {0.06666666666666667, 1097832574u, 2858624801u},
                                    {0.075, 2828730744u, 3915740617u},
                                    {0.1, 2795884105u, 574826925u},
                                    {0.10833333333333334, 785416429u, 3380049803u},
                                    {0.2, 3296801802u, 1472721541u},
                                    {0.3, 3514909893u, 1537181016u},
                                    {0.375, 3514909893u, 991123733u},
                                    {0.38333333333333336, 2017253829u, 273446369u},
                                    {0.4, 2017253829u, 4081433482u},
                                    {0.4083333333333333, 2017253829u, 3232834530u},
                                    {0.5, 2971458969u, 175154916u},
                                    {0.6, 3514909893u, 2640621069u},
                                    {0.8333333333333334, 3514909893u, 4170171741u},
                                    {0.8416666666666667, 3514909893u, 2335224306u},
                                    {1, 3514909893u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2828661085u, 2511639429u},
                                    {0.008333333333333333, 2287201023u, 335872495u},
                                    {0.03333333333333333, 191831257u, 366177528u},
                                    {0.041666666666666664, 4065261353u, 281733564u},
                                    {0.06666666666666667, 1097832574u, 2858624801u},
                                    {0.075, 2828730744u, 3915740617u},
                                    {0.1, 2795884105u, 574826925u},
                                    {0.10833333333333334, 785416429u, 3380049803u},
                                    {0.2, 3296801802u, 1472721541u},
                                    {0.3, 3514909893u, 1537181016u},
                                    {0.375, 3514909893u, 991123733u},
                                    {0.38333333333333336, 2017253829u, 273446369u},
                                    {0.4, 2017253829u, 4081433482u},
                                    {0.4083333333333333, 2017253829u, 3232834530u},
                                    {0.5, 2971458969u, 175154916u},
                                    {0.6, 3514909893u, 2640621069u},
                                    {0.8333333333333334, 3514909893u, 4170171741u},
                                    {0.8416666666666667, 3514909893u, 2335224306u},
                                    {1, 3514909893u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json(" ")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1776963123u, 2511639429u},
                                    {0.008333333333333333, 3801880971u, 335872495u},
                                    {0.03333333333333333, 23489897u, 366177528u},
                                    {0.041666666666666664, 2281574499u, 281733564u},
                                    {0.06666666666666667, 2505855636u, 2858624801u},
                                    {0.075, 2857828605u, 3915740617u},
                                    {0.1, 461680179u, 574826925u},
                                    {0.10833333333333334, 1469932272u, 3380049803u},
                                    {0.2, 3931223633u, 1472721541u},
                                    {0.3, 2202731089u, 1537181016u},
                                    {0.375, 2500242432u, 991123733u},
                                    {0.38333333333333336, 3966762293u, 273446369u},
                                    {0.4, 819823624u, 4081433482u},
                                    {0.4083333333333333, 1095622482u, 3232834530u},
                                    {0.5, 3791299677u, 175154916u},
                                    {0.6, 2110646616u, 2640621069u},
                                    {0.8333333333333334, 2110646616u, 4170171741u},
                                    {0.8416666666666667, 2110646616u, 2335224306u},
                                    {1, 1758719395u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("abc")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2643918565u, 2511639429u},
                                    {0.008333333333333333, 2520999773u, 335872495u},
                                    {0.03333333333333333, 240807643u, 366177528u},
                                    {0.041666666666666664, 1232501599u, 281733564u},
                                    {0.06666666666666667, 3359530613u, 2858624801u},
                                    {0.075, 283534114u, 3915740617u},
                                    {0.1, 1249153280u, 574826925u},
                                    {0.10833333333333334, 2976853789u, 3380049803u},
                                    {0.2, 1180763867u, 1472721541u},
                                    {0.3, 2325407777u, 1537181016u},
                                    {0.375, 3127414469u, 991123733u},
                                    {0.38333333333333336, 901218342u, 273446369u},
                                    {0.4, 74348352u, 4081433482u},
                                    {0.4083333333333333, 3316712089u, 3232834530u},
                                    {0.5, 1454684641u, 175154916u},
                                    {0.6, 596328642u, 2640621069u},
                                    {0.8333333333333334, 596328642u, 4170171741u},
                                    {0.8416666666666667, 596328642u, 2335224306u},
                                    {1, 3680392319u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("I1.MW!?")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1541934933u, 2511639429u},
                                    {0.008333333333333333, 2073523445u, 335872495u},
                                    {0.03333333333333333, 1137230691u, 366177528u},
                                    {0.041666666666666664, 4182758804u, 281733564u},
                                    {0.06666666666666667, 2452774932u, 2858624801u},
                                    {0.075, 25518469u, 3915740617u},
                                    {0.1, 3767543903u, 574826925u},
                                    {0.10833333333333334, 2363113269u, 3380049803u},
                                    {0.2, 11507193u, 1472721541u},
                                    {0.3, 3557828668u, 1537181016u},
                                    {0.375, 2973694451u, 991123733u},
                                    {0.38333333333333336, 340314202u, 273446369u},
                                    {0.4, 1845511132u, 4081433482u},
                                    {0.4083333333333333, 2481743549u, 3232834530u},
                                    {0.5, 3750673708u, 175154916u},
                                    {0.6, 1943861972u, 2640621069u},
                                    {0.8333333333333334, 1943861972u, 4170171741u},
                                    {0.8416666666666667, 1943861972u, 2335224306u},
                                    {1, 3998631210u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("\u0131\u017f\u00df\u4e2d\ud83d\ude42")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2426318701u, 2511639429u},
                                    {0.008333333333333333, 947113947u, 335872495u},
                                    {0.03333333333333333, 250590099u, 366177528u},
                                    {0.041666666666666664, 2131871917u, 281733564u},
                                    {0.06666666666666667, 3572980088u, 2858624801u},
                                    {0.075, 1690011489u, 3915740617u},
                                    {0.1, 715815417u, 574826925u},
                                    {0.10833333333333334, 2089876528u, 3380049803u},
                                    {0.2, 131583568u, 1472721541u},
                                    {0.3, 2104511851u, 1537181016u},
                                    {0.375, 1320634372u, 991123733u},
                                    {0.38333333333333336, 3392896114u, 273446369u},
                                    {0.4, 3522846809u, 4081433482u},
                                    {0.4083333333333333, 1725563122u, 3232834530u},
                                    {0.5, 2113117986u, 175154916u},
                                    {0.6, 1129926744u, 2640621069u},
                                    {0.8333333333333334, 1129926744u, 4170171741u},
                                    {0.8416666666666667, 1129926744u, 2335224306u},
                                    {1, 2153381174u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        auto input_text =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2828661085u, 2511639429u},
                                    {0.008333333333333333, 2287201023u, 335872495u},
                                    {0.03333333333333333, 191831257u, 366177528u},
                                    {0.041666666666666664, 4065261353u, 281733564u},
                                    {0.06666666666666667, 1097832574u, 2858624801u},
                                    {0.075, 2828730744u, 3915740617u},
                                    {0.1, 2795884105u, 574826925u},
                                    {0.10833333333333334, 785416429u, 3380049803u},
                                    {0.2, 3296801802u, 1472721541u},
                                    {0.3, 3514909893u, 1537181016u},
                                    {0.375, 3514909893u, 991123733u},
                                    {0.38333333333333336, 2017253829u, 273446369u},
                                    {0.4, 2017253829u, 4081433482u},
                                    {0.4083333333333333, 2017253829u, 3232834530u},
                                    {0.5, 2971458969u, 175154916u},
                                    {0.6, 3514909893u, 2640621069u},
                                    {0.8333333333333334, 3514909893u, 4170171741u},
                                    {0.8416666666666667, 3514909893u, 2335224306u},
                                    {1, 3514909893u, 3058483324u},
                                    {1.0083333333333333, 3514909893u, 3514909893u},
                                    {1.1, 3514909893u, 3514909893u},
                                    {1.1083333333333334, 3514909893u, 3514909893u},
                                    {1.15, 3514909893u, 3514909893u},
                                    {1.1583333333333334, 3514909893u, 3514909893u},
                                    {1.3, 3514909893u, 3514909893u},
                                    {1.4, 3514909893u, 3514909893u},
                                    {5, 3514909893u, 3514909893u}};
        PixelFx         fx(PixelFxKind::DamageNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(
            R"json("\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::DamageNumber, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
}
TEST_CASE("stylize.pixelfx.ui.comboCounterPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 226712387u, 3682019709u},
                                    {0.008333333333333333, 2402850809u, 3682019709u},
                                    {0.03333333333333333, 484697842u, 3682019709u},
                                    {0.041666666666666664, 4139902397u, 3954315885u},
                                    {0.06666666666666667, 2952091305u, 3954315885u},
                                    {0.075, 2489336027u, 3954315885u},
                                    {0.1, 2704663493u, 3954315885u},
                                    {0.10833333333333334, 1603569867u, 1283949660u},
                                    {0.2, 201407188u, 419461417u},
                                    {0.3, 2965551740u, 1625701172u},
                                    {0.375, 3033734712u, 4175218408u},
                                    {0.38333333333333336, 253699734u, 671254918u},
                                    {0.4, 3711354069u, 939282837u},
                                    {0.4083333333333333, 3390788509u, 850291221u},
                                    {0.5, 732063752u, 2012058541u},
                                    {0.6, 3799652887u, 2598348657u},
                                    {0.8333333333333334, 2226820467u, 197317774u},
                                    {0.8416666666666667, 3098504215u, 1751412424u},
                                    {1, 1655929181u, 1789296876u},
                                    {1.0083333333333333, 103841880u, 188589304u},
                                    {1.1, 351249963u, 2488686986u},
                                    {1.1083333333333334, 2361201352u, 3167327451u},
                                    {1.15, 3144644405u, 2222507051u},
                                    {1.1583333333333334, 1964673910u, 1609449092u},
                                    {1.3, 3122057248u, 777899623u},
                                    {1.4, 3528958405u, 23567365u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 2302591196u, 1879896453u},
                                    {0.008333333333333333, 125863277u, 1879896453u},
                                    {0.03333333333333333, 4229305323u, 1879896453u},
                                    {0.041666666666666664, 42446237u, 3032413605u},
                                    {0.06666666666666667, 1141888839u, 3032413605u},
                                    {0.075, 2733999814u, 3032413605u},
                                    {0.1, 14177279u, 3032413605u},
                                    {0.10833333333333334, 3880593359u, 845184452u},
                                    {0.2, 3511786293u, 3074560105u},
                                    {0.3, 1802600710u, 1100496860u},
                                    {0.375, 2420217043u, 1742610523u},
                                    {0.38333333333333336, 1251428700u, 4073110099u},
                                    {0.4, 1983116536u, 3867922021u},
                                    {0.4083333333333333, 273504996u, 1559009229u},
                                    {0.5, 603579021u, 381521621u},
                                    {0.6, 2890273494u, 257158261u},
                                    {0.8333333333333334, 400517463u, 8940319u},
                                    {0.8416666666666667, 2828830491u, 3635317621u},
                                    {1, 3799843846u, 4127166610u},
                                    {1.0083333333333333, 3540224247u, 3899205058u},
                                    {1.1, 3769100325u, 3666301534u},
                                    {1.1083333333333334, 3234910494u, 1666862921u},
                                    {1.15, 3558549301u, 2924643040u},
                                    {1.1583333333333334, 2456994131u, 2941256687u},
                                    {1.3, 1546979102u, 3768567912u},
                                    {1.4, 3528958405u, 3528958405u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_count = eve::Value::fromJson(R"json(12.5)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto input_label = eve::Value::fromJson(R"json("HIT!")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 226712387u, 3682019709u},
                                    {0.008333333333333333, 2402850809u, 3682019709u},
                                    {0.03333333333333333, 484697842u, 3682019709u},
                                    {0.041666666666666664, 4139902397u, 3954315885u},
                                    {0.06666666666666667, 2952091305u, 3954315885u},
                                    {0.075, 2489336027u, 3954315885u},
                                    {0.1, 2704663493u, 3954315885u},
                                    {0.10833333333333334, 3973572357u, 1404866602u},
                                    {0.2, 3003302690u, 3001937559u},
                                    {0.3, 972344072u, 612268894u},
                                    {0.375, 1263867076u, 336279227u},
                                    {0.38333333333333336, 1828023388u, 3909025056u},
                                    {0.4, 3711354069u, 939282837u},
                                    {0.4083333333333333, 3390788509u, 850291221u},
                                    {0.5, 732063752u, 2012058541u},
                                    {0.6, 3700579023u, 3305375459u},
                                    {0.8333333333333334, 1880440135u, 3310191879u},
                                    {0.8416666666666667, 2403715235u, 1669116377u},
                                    {1, 776543097u, 916418813u},
                                    {1.0083333333333333, 961459069u, 322096247u},
                                    {1.1, 3264742535u, 912251488u},
                                    {1.1083333333333334, 3371458780u, 583650033u},
                                    {1.15, 1142004632u, 2488420301u},
                                    {1.1583333333333334, 2811359679u, 1928044469u},
                                    {1.3, 3122057248u, 3528958405u},
                                    {1.4, 3528958405u, 3528958405u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 226712387u, 3682019709u},
                                    {0.008333333333333333, 2402850809u, 3682019709u},
                                    {0.03333333333333333, 484697842u, 3682019709u},
                                    {0.041666666666666664, 4139902397u, 3954315885u},
                                    {0.06666666666666667, 2952091305u, 3954315885u},
                                    {0.075, 2489336027u, 3954315885u},
                                    {0.1, 2704663493u, 3954315885u},
                                    {0.10833333333333334, 896150799u, 160018731u},
                                    {0.2, 2524789436u, 3758205579u},
                                    {0.3, 2730513638u, 2552356268u},
                                    {0.375, 1680371128u, 263303092u},
                                    {0.38333333333333336, 615466310u, 871777534u},
                                    {0.4, 3711354069u, 939282837u},
                                    {0.4083333333333333, 3390788509u, 850291221u},
                                    {0.5, 732063752u, 2012058541u},
                                    {0.6, 808184867u, 2665117896u},
                                    {0.8333333333333334, 4047714747u, 3184011032u},
                                    {0.8416666666666667, 3743684831u, 1796716227u},
                                    {1, 776543097u, 1088621421u},
                                    {1.0083333333333333, 961459069u, 4177319621u},
                                    {1.1, 3264742535u, 2031012091u},
                                    {1.1083333333333334, 3853303073u, 4026263098u},
                                    {1.15, 1142004632u, 4002469103u},
                                    {1.1583333333333334, 2811359679u, 910584615u},
                                    {1.3, 3363763564u, 115059988u},
                                    {1.4, 3528958405u, 3526568667u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 510066917u, 3758236293u},
                                    {0.008333333333333333, 1801710485u, 3758236293u},
                                    {0.03333333333333333, 2142137217u, 3758236293u},
                                    {0.041666666666666664, 1205803553u, 542282821u},
                                    {0.06666666666666667, 1981917097u, 542282821u},
                                    {0.075, 2361658013u, 542282821u},
                                    {0.1, 1217555005u, 542282821u},
                                    {0.10833333333333334, 4081749285u, 3417864400u},
                                    {0.2, 387226985u, 2294110646u},
                                    {0.3, 1286207597u, 395517465u},
                                    {0.375, 1393717301u, 1040371484u},
                                    {0.38333333333333336, 2947530685u, 289537146u},
                                    {0.4, 3499065461u, 364368077u},
                                    {0.4083333333333333, 1664455149u, 3949621413u},
                                    {0.5, 3062454069u, 1565739973u},
                                    {0.6, 3986092389u, 861715461u},
                                    {0.8333333333333334, 771232677u, 2402578942u},
                                    {0.8416666666666667, 771232677u, 1309094274u},
                                    {1, 4108364437u, 2801610798u},
                                    {1.0083333333333333, 1930860537u, 4173220018u},
                                    {1.1, 2451451685u, 74466406u},
                                    {1.1083333333333334, 3917655689u, 2233167061u},
                                    {1.15, 2519599241u, 2114331967u},
                                    {1.1583333333333334, 3926252369u, 1411810535u},
                                    {1.3, 1906086669u, 1898163922u},
                                    {1.4, 3528958405u, 509099439u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3897838625u, 209820829u},
                                    {0.008333333333333333, 4073186475u, 209820829u},
                                    {0.03333333333333333, 2693020723u, 209820829u},
                                    {0.041666666666666664, 1476230412u, 376626197u},
                                    {0.06666666666666667, 937670264u, 376626197u},
                                    {0.075, 62742096u, 376626197u},
                                    {0.1, 1080047032u, 376626197u},
                                    {0.10833333333333334, 122921894u, 3955683910u},
                                    {0.2, 3622763073u, 1089753106u},
                                    {0.3, 3333621240u, 2681950710u},
                                    {0.375, 713441838u, 3016632368u},
                                    {0.38333333333333336, 2276610604u, 592516287u},
                                    {0.4, 451068296u, 1050249341u},
                                    {0.4083333333333333, 317251454u, 142536077u},
                                    {0.5, 3319620318u, 830573085u},
                                    {0.6, 1564744433u, 1407272797u},
                                    {0.8333333333333334, 1488408503u, 2945177589u},
                                    {0.8416666666666667, 2981149553u, 3884442563u},
                                    {1, 3118664094u, 3011266436u},
                                    {1.0083333333333333, 1571032598u, 1152700940u},
                                    {1.1, 2212563495u, 4278041428u},
                                    {1.1083333333333334, 1698425907u, 17337305u},
                                    {1.15, 963532208u, 2865810667u},
                                    {1.1583333333333334, 1144071807u, 3398424548u},
                                    {1.3, 1704687921u, 2530659861u},
                                    {1.4, 3528958405u, 458298956u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 4062457784u, 3682019709u},
                                    {0.008333333333333333, 1202315508u, 3682019709u},
                                    {0.03333333333333333, 509747827u, 3682019709u},
                                    {0.041666666666666664, 2651275483u, 3954315885u},
                                    {0.06666666666666667, 2599676383u, 3954315885u},
                                    {0.075, 9273108u, 3954315885u},
                                    {0.1, 1048237557u, 3954315885u},
                                    {0.10833333333333334, 2794384867u, 1283949660u},
                                    {0.2, 2122504155u, 419461417u},
                                    {0.3, 3612967313u, 1625701172u},
                                    {0.375, 2549922525u, 4175218408u},
                                    {0.38333333333333336, 1007610165u, 671254918u},
                                    {0.4, 4015743186u, 939282837u},
                                    {0.4083333333333333, 2208503094u, 850291221u},
                                    {0.5, 1941424138u, 2012058541u},
                                    {0.6, 493410644u, 3866601151u},
                                    {0.8333333333333334, 3623969984u, 2155732020u},
                                    {0.8416666666666667, 3556392436u, 4095601792u},
                                    {1, 4125568964u, 2437639464u},
                                    {1.0083333333333333, 2602529704u, 375221344u},
                                    {1.1, 2061490810u, 2380333894u},
                                    {1.1083333333333334, 3874538722u, 731825459u},
                                    {1.15, 3866160154u, 2621515666u},
                                    {1.1583333333333334, 1157533213u, 448605349u},
                                    {1.3, 3122057248u, 2232571358u},
                                    {1.4, 3528958405u, 1140602181u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(-999999)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1443306413u, 3682019709u},
                                    {0.008333333333333333, 4054003171u, 3682019709u},
                                    {0.03333333333333333, 3043311368u, 3682019709u},
                                    {0.041666666666666664, 2587330576u, 3954315885u},
                                    {0.06666666666666667, 2388938078u, 3954315885u},
                                    {0.075, 4197652483u, 3954315885u},
                                    {0.1, 4246643514u, 3954315885u},
                                    {0.10833333333333334, 542481125u, 1283949660u},
                                    {0.2, 1600040388u, 419461417u},
                                    {0.3, 4220811181u, 1625701172u},
                                    {0.375, 2728736894u, 4175218408u},
                                    {0.38333333333333336, 409127296u, 671254918u},
                                    {0.4, 2825567123u, 939282837u},
                                    {0.4083333333333333, 2490284467u, 850291221u},
                                    {0.5, 2994537968u, 2012058541u},
                                    {0.6, 3479354303u, 3601548404u},
                                    {0.8333333333333334, 460518083u, 1895837643u},
                                    {0.8416666666666667, 4080282511u, 3511995423u},
                                    {1, 3272246721u, 1145691589u},
                                    {1.0083333333333333, 3939845721u, 1881810338u},
                                    {1.1, 2144773462u, 1309070546u},
                                    {1.1083333333333334, 2378988844u, 3118143777u},
                                    {1.15, 2521021636u, 1139329137u},
                                    {1.1583333333333334, 3423478807u, 2515263468u},
                                    {1.3, 3122057248u, 2813378788u},
                                    {1.4, 3528958405u, 2437093808u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(-12.5)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3453534750u, 3682019709u},
                                    {0.008333333333333333, 1930815736u, 3682019709u},
                                    {0.03333333333333333, 2441010113u, 3682019709u},
                                    {0.041666666666666664, 2917592195u, 3954315885u},
                                    {0.06666666666666667, 2159936606u, 3954315885u},
                                    {0.075, 3097799922u, 3954315885u},
                                    {0.1, 482342975u, 3954315885u},
                                    {0.10833333333333334, 1932406825u, 1283949660u},
                                    {0.2, 1690871253u, 419461417u},
                                    {0.3, 2701690118u, 1625701172u},
                                    {0.375, 475034835u, 4175218408u},
                                    {0.38333333333333336, 507436037u, 671254918u},
                                    {0.4, 2680710334u, 939282837u},
                                    {0.4083333333333333, 3397394386u, 850291221u},
                                    {0.5, 3092552297u, 2012058541u},
                                    {0.6, 1495927548u, 2598348657u},
                                    {0.8333333333333334, 1298114616u, 197317774u},
                                    {0.8416666666666667, 3162139516u, 1751412424u},
                                    {1, 3007157431u, 1789296876u},
                                    {1.0083333333333333, 953376803u, 188589304u},
                                    {1.1, 608745332u, 2488686986u},
                                    {1.1083333333333334, 3708902094u, 3167327451u},
                                    {1.15, 2951140291u, 2222507051u},
                                    {1.1583333333333334, 3199351696u, 1609449092u},
                                    {1.3, 3122057248u, 777899623u},
                                    {1.4, 3528958405u, 23567365u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(-0.5)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3453534750u, 3682019709u},
                                    {0.008333333333333333, 1930815736u, 3682019709u},
                                    {0.03333333333333333, 2441010113u, 3682019709u},
                                    {0.041666666666666664, 2917592195u, 3954315885u},
                                    {0.06666666666666667, 2159936606u, 3954315885u},
                                    {0.075, 3097799922u, 3954315885u},
                                    {0.1, 482342975u, 3954315885u},
                                    {0.10833333333333334, 1932406825u, 1283949660u},
                                    {0.2, 1690871253u, 419461417u},
                                    {0.3, 2701690118u, 1625701172u},
                                    {0.375, 475034835u, 4175218408u},
                                    {0.38333333333333336, 507436037u, 671254918u},
                                    {0.4, 2680710334u, 939282837u},
                                    {0.4083333333333333, 3397394386u, 850291221u},
                                    {0.5, 3092552297u, 2012058541u},
                                    {0.6, 1495927548u, 2598348657u},
                                    {0.8333333333333334, 1298114616u, 197317774u},
                                    {0.8416666666666667, 3162139516u, 1751412424u},
                                    {1, 3007157431u, 1789296876u},
                                    {1.0083333333333333, 953376803u, 188589304u},
                                    {1.1, 608745332u, 2488686986u},
                                    {1.1083333333333334, 3708902094u, 3167327451u},
                                    {1.15, 2951140291u, 2222507051u},
                                    {1.1583333333333334, 3199351696u, 1609449092u},
                                    {1.3, 3122057248u, 777899623u},
                                    {1.4, 3528958405u, 23567365u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3493020878u, 3682019709u},
                                    {0.008333333333333333, 2776568710u, 3682019709u},
                                    {0.03333333333333333, 2282255478u, 3682019709u},
                                    {0.041666666666666664, 38936816u, 3954315885u},
                                    {0.06666666666666667, 2184682610u, 3954315885u},
                                    {0.075, 3101663253u, 3954315885u},
                                    {0.1, 3943661000u, 3954315885u},
                                    {0.10833333333333334, 1934356698u, 1283949660u},
                                    {0.2, 2015327155u, 419461417u},
                                    {0.3, 3005919675u, 1625701172u},
                                    {0.375, 2989209207u, 4175218408u},
                                    {0.38333333333333336, 3938219149u, 671254918u},
                                    {0.4, 3537055087u, 939282837u},
                                    {0.4083333333333333, 3178722027u, 850291221u},
                                    {0.5, 2526399533u, 2012058541u},
                                    {0.6, 12498536u, 4166577965u},
                                    {0.8333333333333334, 4023769760u, 2098827690u},
                                    {0.8416666666666667, 446653332u, 515916964u},
                                    {1, 1801914098u, 2468861202u},
                                    {1.0083333333333333, 1780005798u, 2057999643u},
                                    {1.1, 409667288u, 3500099198u},
                                    {1.1083333333333334, 620896538u, 86700442u},
                                    {1.15, 4056571689u, 105863818u},
                                    {1.1583333333333334, 1262392070u, 3794201436u},
                                    {1.3, 3122057248u, 3537262006u},
                                    {1.4, 3528958405u, 1921228133u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(0.5)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 226712387u, 3682019709u},
                                    {0.008333333333333333, 2402850809u, 3682019709u},
                                    {0.03333333333333333, 484697842u, 3682019709u},
                                    {0.041666666666666664, 4139902397u, 3954315885u},
                                    {0.06666666666666667, 2952091305u, 3954315885u},
                                    {0.075, 2489336027u, 3954315885u},
                                    {0.1, 2704663493u, 3954315885u},
                                    {0.10833333333333334, 1603569867u, 1283949660u},
                                    {0.2, 201407188u, 419461417u},
                                    {0.3, 2965551740u, 1625701172u},
                                    {0.375, 3033734712u, 4175218408u},
                                    {0.38333333333333336, 253699734u, 671254918u},
                                    {0.4, 3711354069u, 939282837u},
                                    {0.4083333333333333, 3390788509u, 850291221u},
                                    {0.5, 732063752u, 2012058541u},
                                    {0.6, 3799652887u, 2598348657u},
                                    {0.8333333333333334, 2226820467u, 197317774u},
                                    {0.8416666666666667, 3098504215u, 1751412424u},
                                    {1, 1655929181u, 1789296876u},
                                    {1.0083333333333333, 103841880u, 188589304u},
                                    {1.1, 351249963u, 2488686986u},
                                    {1.1083333333333334, 2361201352u, 3167327451u},
                                    {1.15, 3144644405u, 2222507051u},
                                    {1.1583333333333334, 1964673910u, 1609449092u},
                                    {1.3, 3122057248u, 777899623u},
                                    {1.4, 3528958405u, 23567365u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(5.49)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 770022053u, 3682019709u},
                                    {0.008333333333333333, 898508647u, 3682019709u},
                                    {0.03333333333333333, 832567203u, 3682019709u},
                                    {0.041666666666666664, 719217323u, 3954315885u},
                                    {0.06666666666666667, 1843277913u, 3954315885u},
                                    {0.075, 3142750885u, 3954315885u},
                                    {0.1, 1431822544u, 3954315885u},
                                    {0.10833333333333334, 3728676257u, 1283949660u},
                                    {0.2, 3763322940u, 419461417u},
                                    {0.3, 1094897418u, 1625701172u},
                                    {0.375, 1760270458u, 4175218408u},
                                    {0.38333333333333336, 915842880u, 671254918u},
                                    {0.4, 4059018447u, 939282837u},
                                    {0.4083333333333333, 4271004255u, 850291221u},
                                    {0.5, 1831031808u, 2012058541u},
                                    {0.6, 3399066721u, 2598348657u},
                                    {0.8333333333333334, 2906388869u, 197317774u},
                                    {0.8416666666666667, 3360918433u, 1751412424u},
                                    {1, 1160164055u, 1789296876u},
                                    {1.0083333333333333, 2098060899u, 188589304u},
                                    {1.1, 1121886449u, 2488686986u},
                                    {1.1083333333333334, 1833449070u, 3167327451u},
                                    {1.15, 2951231554u, 2222507051u},
                                    {1.1583333333333334, 3343696597u, 1609449092u},
                                    {1.3, 3122057248u, 777899623u},
                                    {1.4, 3528958405u, 23567365u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(5.5)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 4088064235u, 3682019709u},
                                    {0.008333333333333333, 1542250668u, 3682019709u},
                                    {0.03333333333333333, 1864116798u, 3682019709u},
                                    {0.041666666666666664, 2149726054u, 3954315885u},
                                    {0.06666666666666667, 2601203893u, 3954315885u},
                                    {0.075, 478995322u, 3954315885u},
                                    {0.1, 2402992011u, 3954315885u},
                                    {0.10833333333333334, 1254750882u, 1283949660u},
                                    {0.2, 3566145576u, 419461417u},
                                    {0.3, 1899471634u, 1625701172u},
                                    {0.375, 2872061356u, 4175218408u},
                                    {0.38333333333333336, 2976778492u, 671254918u},
                                    {0.4, 2284458535u, 939282837u},
                                    {0.4083333333333333, 1952799959u, 850291221u},
                                    {0.5, 3885512767u, 2012058541u},
                                    {0.6, 2316869347u, 4285740193u},
                                    {0.8333333333333334, 284719591u, 896293216u},
                                    {0.8416666666666667, 4019013423u, 2859112615u},
                                    {1, 2770333983u, 2856658878u},
                                    {1.0083333333333333, 3008583287u, 1093605425u},
                                    {1.1, 2828434965u, 4063912595u},
                                    {1.1083333333333334, 2682682155u, 3845199243u},
                                    {1.15, 772289796u, 2757327347u},
                                    {1.1583333333333334, 1191431899u, 860894354u},
                                    {1.3, 3122057248u, 207842876u},
                                    {1.4, 3528958405u, 2105465108u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_count = eve::Value::fromJson(R"json(999999)json");
        REQUIRE(input_count);
        value.find("options")->set("count", input_count.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 259827881u, 3682019709u},
                                    {0.008333333333333333, 1110751169u, 3682019709u},
                                    {0.03333333333333333, 941376663u, 3682019709u},
                                    {0.041666666666666664, 1248208856u, 3954315885u},
                                    {0.06666666666666667, 1557787455u, 3954315885u},
                                    {0.075, 3004705105u, 3954315885u},
                                    {0.1, 1812278418u, 3954315885u},
                                    {0.10833333333333334, 833989608u, 1283949660u},
                                    {0.2, 2055042144u, 419461417u},
                                    {0.3, 1949264472u, 1625701172u},
                                    {0.375, 3201980928u, 4175218408u},
                                    {0.38333333333333336, 1857784506u, 671254918u},
                                    {0.4, 433160816u, 939282837u},
                                    {0.4083333333333333, 790548916u, 850291221u},
                                    {0.5, 484255411u, 2012058541u},
                                    {0.6, 3074359503u, 1320661803u},
                                    {0.8333333333333334, 2379072791u, 2499506956u},
                                    {0.8416666666666667, 2379072791u, 972021508u},
                                    {1, 2308690193u, 2052475482u},
                                    {1.0083333333333333, 3757860896u, 502781634u},
                                    {1.1, 2673800875u, 3757715820u},
                                    {1.1083333333333334, 3656967388u, 457356943u},
                                    {1.15, 2534533156u, 1330874853u},
                                    {1.1583333333333334, 4035022199u, 689707654u},
                                    {1.3, 3122057248u, 4048229511u},
                                    {1.4, 3528958405u, 2378074597u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 3957595472u, 3682019709u},
                                    {0.008333333333333333, 1524881393u, 3682019709u},
                                    {0.03333333333333333, 3975916693u, 3682019709u},
                                    {0.041666666666666664, 4261110107u, 3954315885u},
                                    {0.06666666666666667, 393623015u, 3954315885u},
                                    {0.075, 1948087912u, 3954315885u},
                                    {0.1, 2532864500u, 3954315885u},
                                    {0.10833333333333334, 1526281242u, 1283949660u},
                                    {0.2, 535309399u, 419461417u},
                                    {0.3, 1621651828u, 1625701172u},
                                    {0.375, 1093279057u, 4175218408u},
                                    {0.38333333333333336, 3852621955u, 671254918u},
                                    {0.4, 1439589189u, 939282837u},
                                    {0.4083333333333333, 1960168053u, 850291221u},
                                    {0.5, 1103114638u, 2012058541u},
                                    {0.6, 3457136546u, 3611637125u},
                                    {0.8333333333333334, 3293593378u, 2621451314u},
                                    {0.8416666666666667, 255755870u, 59013274u},
                                    {1, 2083727510u, 210255428u},
                                    {1.0083333333333333, 1006947408u, 183535040u},
                                    {1.1, 4276847246u, 3049603286u},
                                    {1.1083333333333334, 1415069789u, 673579637u},
                                    {1.15, 3704392989u, 665578923u},
                                    {1.1583333333333334, 1443679366u, 2752581656u},
                                    {1.3, 3122057248u, 819799271u},
                                    {1.4, 3528958405u, 4210148805u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json(" ")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 948876585u, 3682019709u},
                                    {0.008333333333333333, 2441493423u, 3682019709u},
                                    {0.03333333333333333, 839845255u, 3682019709u},
                                    {0.041666666666666664, 461879964u, 3954315885u},
                                    {0.06666666666666667, 136054735u, 3954315885u},
                                    {0.075, 2800214770u, 3954315885u},
                                    {0.1, 2527819582u, 3954315885u},
                                    {0.10833333333333334, 1513555003u, 1283949660u},
                                    {0.2, 1596388395u, 419461417u},
                                    {0.3, 2798089263u, 1625701172u},
                                    {0.375, 2653196145u, 4175218408u},
                                    {0.38333333333333336, 934556207u, 671254918u},
                                    {0.4, 2719035461u, 939282837u},
                                    {0.4083333333333333, 2924794837u, 850291221u},
                                    {0.5, 2478349877u, 2012058541u},
                                    {0.6, 1196695034u, 3634339049u},
                                    {0.8333333333333334, 1250196462u, 1631143556u},
                                    {0.8416666666666667, 3781758546u, 4293253246u},
                                    {1, 820875094u, 1145454199u},
                                    {1.0083333333333333, 2990540728u, 76435701u},
                                    {1.1, 3804735882u, 2131889298u},
                                    {1.1083333333333334, 4213544253u, 2883544607u},
                                    {1.15, 3730677709u, 848820135u},
                                    {1.1583333333333334, 344121842u, 4024445532u},
                                    {1.3, 3122057248u, 1672017895u},
                                    {1.4, 3528958405u, 1635595589u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("hit")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 4093187880u, 3682019709u},
                                    {0.008333333333333333, 1061987998u, 3682019709u},
                                    {0.03333333333333333, 939669508u, 3682019709u},
                                    {0.041666666666666664, 2967554581u, 3954315885u},
                                    {0.06666666666666667, 1274338131u, 3954315885u},
                                    {0.075, 1056364628u, 3954315885u},
                                    {0.1, 2234372487u, 3954315885u},
                                    {0.10833333333333334, 236936640u, 1283949660u},
                                    {0.2, 2708598864u, 419461417u},
                                    {0.3, 2996699131u, 1625701172u},
                                    {0.375, 3418673603u, 4175218408u},
                                    {0.38333333333333336, 3940589585u, 671254918u},
                                    {0.4, 2070175947u, 939282837u},
                                    {0.4083333333333333, 663189319u, 850291221u},
                                    {0.5, 1389017171u, 2012058541u},
                                    {0.6, 384537664u, 1788024681u},
                                    {0.8333333333333334, 1589741512u, 3332694532u},
                                    {0.8416666666666667, 4246391252u, 1957173002u},
                                    {1, 2805843780u, 2029958934u},
                                    {1.0083333333333333, 1528092397u, 1309108324u},
                                    {1.1, 1989420104u, 1006422366u},
                                    {1.1083333333333334, 933028934u, 1864597935u},
                                    {1.15, 663803089u, 1267383877u},
                                    {1.1583333333333334, 1773703014u, 3873907060u},
                                    {1.3, 3122057248u, 982004839u},
                                    {1.4, 3528958405u, 4125264133u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("\u0131\u017f\u00df\u4e2d\ud83d\ude42")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 469451566u, 3682019709u},
                                    {0.008333333333333333, 2891538689u, 3682019709u},
                                    {0.03333333333333333, 1804703359u, 3682019709u},
                                    {0.041666666666666664, 4171371099u, 3954315885u},
                                    {0.06666666666666667, 4006108u, 3954315885u},
                                    {0.075, 39461818u, 3954315885u},
                                    {0.1, 2441786522u, 3954315885u},
                                    {0.10833333333333334, 785439812u, 1283949660u},
                                    {0.2, 1380539011u, 419461417u},
                                    {0.3, 607736584u, 1625701172u},
                                    {0.375, 1978808635u, 4175218408u},
                                    {0.38333333333333336, 1170709295u, 671254918u},
                                    {0.4, 2376716878u, 939282837u},
                                    {0.4083333333333333, 2430490170u, 850291221u},
                                    {0.5, 3969023762u, 2012058541u},
                                    {0.6, 1027372090u, 2259891741u},
                                    {0.8333333333333334, 1147797672u, 3954315885u},
                                    {0.8416666666666667, 1147797672u, 3954315885u},
                                    {1, 1147797672u, 3954315885u},
                                    {1.0083333333333333, 1147797672u, 3954315885u},
                                    {1.1, 1147797672u, 3954315885u},
                                    {1.1083333333333334, 126248612u, 3954315885u},
                                    {1.15, 3250621225u, 3954315885u},
                                    {1.1583333333333334, 702129402u, 3954315885u},
                                    {1.3, 3122057248u, 3528958405u},
                                    {1.4, 3528958405u, 3528958405u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        auto input_label =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1571884634u, 3682019709u},
                                    {0.008333333333333333, 1571884634u, 3682019709u},
                                    {0.03333333333333333, 283440638u, 3682019709u},
                                    {0.041666666666666664, 283440638u, 3954315885u},
                                    {0.06666666666666667, 2949770108u, 3954315885u},
                                    {0.075, 2949770108u, 3954315885u},
                                    {0.1, 2949770108u, 3954315885u},
                                    {0.10833333333333334, 2134047386u, 1283949660u},
                                    {0.2, 3931561230u, 419461417u},
                                    {0.3, 330595942u, 1625701172u},
                                    {0.375, 989294904u, 4175218408u},
                                    {0.38333333333333336, 2511503198u, 671254918u},
                                    {0.4, 822681484u, 939282837u},
                                    {0.4083333333333333, 2737295256u, 850291221u},
                                    {0.5, 237359109u, 2012058541u},
                                    {0.6, 901529739u, 2259891741u},
                                    {0.8333333333333334, 199392143u, 3954315885u},
                                    {0.8416666666666667, 3120626999u, 3954315885u},
                                    {1, 3366454661u, 3954315885u},
                                    {1.0083333333333333, 3366454661u, 3954315885u},
                                    {1.1, 3366454661u, 3954315885u},
                                    {1.1083333333333334, 3528958405u, 3954315885u},
                                    {1.15, 241220003u, 3954315885u},
                                    {1.1583333333333334, 2073592756u, 3954315885u},
                                    {1.3, 3122057248u, 3528958405u},
                                    {1.4, 3528958405u, 3528958405u},
                                    {5, 3528958405u, 3528958405u}};
        PixelFx         fx(PixelFxKind::ComboCounter);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(
            R"json("\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ComboCounter, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
}
TEST_CASE("stylize.pixelfx.ui.exclamationPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 1738698135u, 1399661560u},
                                    {0.06666666666666667, 917120901u, 734704865u},
                                    {0.075, 917807375u, 242719841u},
                                    {0.1, 2164943780u, 2484759652u},
                                    {0.10833333333333334, 4171579303u, 3843888149u},
                                    {0.2, 2767283640u, 486758129u},
                                    {0.3, 2453891243u, 136485861u},
                                    {0.375, 824263561u, 2714640u},
                                    {0.38333333333333336, 1610202677u, 588873312u},
                                    {0.4, 718748722u, 1234065661u},
                                    {0.4083333333333333, 99566454u, 1232828309u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 3310677075u, 1611521073u},
                                    {1.3, 1088702877u, 1404432133u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1081661381u, 47753589u},
                                    {0.008333333333333333, 502310509u, 2496933297u},
                                    {0.03333333333333333, 1027655475u, 3660254925u},
                                    {0.041666666666666664, 4038851703u, 1196225647u},
                                    {0.06666666666666667, 2831524581u, 3409198492u},
                                    {0.075, 1339323543u, 403927355u},
                                    {0.1, 2838148221u, 2490637165u},
                                    {0.10833333333333334, 831426955u, 928550881u},
                                    {0.2, 1753711738u, 1718630785u},
                                    {0.3, 842569611u, 4245486144u},
                                    {0.375, 378208209u, 26505131u},
                                    {0.38333333333333336, 3897874169u, 155126757u},
                                    {0.4, 927399138u, 2417897643u},
                                    {0.4083333333333333, 3976866050u, 2063853625u},
                                    {0.5, 1354162075u, 555388453u},
                                    {0.6, 3288131183u, 1744703133u},
                                    {0.8333333333333334, 551577134u, 1298018497u},
                                    {0.8416666666666667, 1712346067u, 2489515173u},
                                    {1, 222880542u, 455190305u},
                                    {1.0083333333333333, 469036660u, 3404055277u},
                                    {1.1, 366188375u, 1267295381u},
                                    {1.1083333333333334, 2667041797u, 1833355033u},
                                    {1.15, 2280939169u, 1833355033u},
                                    {1.1583333333333334, 2820347560u, 3738460904u},
                                    {1.3, 1081661381u, 3142916991u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 2080836301u, 1169577351u},
                                    {0.06666666666666667, 1455768784u, 2756734027u},
                                    {0.075, 1672998242u, 3134784074u},
                                    {0.1, 483284673u, 2594648276u},
                                    {0.10833333333333334, 2198336826u, 1714152022u},
                                    {0.2, 1241454298u, 3161583045u},
                                    {0.3, 765186876u, 2775707172u},
                                    {0.375, 824263561u, 1806796711u},
                                    {0.38333333333333336, 1610202677u, 1368321689u},
                                    {0.4, 718748722u, 1799564915u},
                                    {0.4083333333333333, 99566454u, 91037387u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 2396340058u, 3285881018u},
                                    {1.3, 603379645u, 3511408996u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 2766754631u, 548954871u},
                                    {0.06666666666666667, 527218129u, 654122697u},
                                    {0.075, 1299184847u, 564487094u},
                                    {0.1, 361360120u, 394808484u},
                                    {0.10833333333333334, 361295189u, 916337339u},
                                    {0.2, 1981356875u, 334929471u},
                                    {0.3, 4179672379u, 2727028728u},
                                    {0.375, 824263561u, 195398247u},
                                    {0.38333333333333336, 1610202677u, 3366313081u},
                                    {0.4, 718748722u, 1234065661u},
                                    {0.4083333333333333, 99566454u, 1232828309u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 64430622u, 2228325931u},
                                    {1.3, 167205985u, 4332530u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2223769989u},
                                    {0.008333333333333333, 2253549221u, 1728128085u},
                                    {0.03333333333333333, 1115541017u, 727588325u},
                                    {0.041666666666666664, 2169855881u, 1706284678u},
                                    {0.06666666666666667, 4024940561u, 2144774442u},
                                    {0.075, 2159180469u, 20044356u},
                                    {0.1, 3504611721u, 276208358u},
                                    {0.10833333333333334, 4157240913u, 1250683500u},
                                    {0.2, 1759012301u, 561250880u},
                                    {0.3, 1909426913u, 2442763073u},
                                    {0.375, 2092465285u, 4790140u},
                                    {0.38333333333333336, 2821323057u, 58375932u},
                                    {0.4, 861701457u, 1511343821u},
                                    {0.4083333333333333, 3412700453u, 1665621597u},
                                    {0.5, 2677448449u, 3956854077u},
                                    {0.6, 679249313u, 1308823125u},
                                    {0.8333333333333334, 254356241u, 4262668237u},
                                    {0.8416666666666667, 4102956157u, 1097120541u},
                                    {1, 3508862537u, 39159805u},
                                    {1.0083333333333333, 3508862537u, 3555885637u},
                                    {1.1, 3900075645u, 3317746269u},
                                    {1.1083333333333334, 491608517u, 519434949u},
                                    {1.15, 1041001937u, 519434949u},
                                    {1.1583333333333334, 2199964053u, 2685666148u},
                                    {1.3, 2543882717u, 2227569661u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2138187197u},
                                    {0.008333333333333333, 1076754429u, 1527471885u},
                                    {0.03333333333333333, 3322498584u, 4265893973u},
                                    {0.041666666666666664, 2742845638u, 3868454372u},
                                    {0.06666666666666667, 1238121300u, 1535399273u},
                                    {0.075, 531769869u, 3043917855u},
                                    {0.1, 3022818731u, 1402226666u},
                                    {0.10833333333333334, 1145917158u, 1386210134u},
                                    {0.2, 2779727831u, 1247503730u},
                                    {0.3, 241830903u, 4125180749u},
                                    {0.375, 226244169u, 1645412482u},
                                    {0.38333333333333336, 342319980u, 1007433017u},
                                    {0.4, 2967100748u, 3035710473u},
                                    {0.4083333333333333, 776995974u, 1732099645u},
                                    {0.5, 1825593929u, 2175077261u},
                                    {0.6, 2409677914u, 527515657u},
                                    {0.8333333333333334, 659220488u, 1455143083u},
                                    {0.8416666666666667, 2941222232u, 4025882357u},
                                    {1, 823662069u, 4025882357u},
                                    {1.0083333333333333, 690043972u, 4025882357u},
                                    {1.1, 1287394132u, 1106054149u},
                                    {1.1083333333333334, 390011349u, 2088157301u},
                                    {1.15, 3646162745u, 2088157301u},
                                    {1.1583333333333334, 3176053427u, 1990616864u},
                                    {1.3, 939508989u, 1592042916u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        for (const auto& f : frames) {
            auto seek = fx.prepareSeek(f.time);
            REQUIRE(seek);
            REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
            fx.draw();
            REQUIRE(checksum(fx.color()) == f.color);
            REQUIRE(checksum(fx.light()) == f.light);
            REQUIRE(fx.actorVisibility() == PixelFxActorVisibility::Visible);
        }
    }
}
TEST_CASE("stylize.pixelfx.ui.exclamationPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 1738698135u, 1399661560u},
                                    {0.06666666666666667, 917120901u, 734704865u},
                                    {0.075, 917807375u, 242719841u},
                                    {0.1, 2164943780u, 2484759652u},
                                    {0.10833333333333334, 4171579303u, 3843888149u},
                                    {0.2, 2767283640u, 486758129u},
                                    {0.3, 2453891243u, 136485861u},
                                    {0.375, 824263561u, 2714640u},
                                    {0.38333333333333336, 1610202677u, 588873312u},
                                    {0.4, 718748722u, 1234065661u},
                                    {0.4083333333333333, 99566454u, 1232828309u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 3310677075u, 1611521073u},
                                    {1.3, 1088702877u, 1404432133u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
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
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 360547926u, 447119832u},
                                    {0.06666666666666667, 2337554276u, 3808559527u},
                                    {0.075, 814254534u, 4054393327u},
                                    {0.1, 53461377u, 1003592659u},
                                    {0.10833333333333334, 1107676931u, 218750040u},
                                    {0.2, 2249189010u, 109112105u},
                                    {0.3, 2453891243u, 3386449603u},
                                    {0.375, 824263561u, 726009451u},
                                    {0.38333333333333336, 1610202677u, 2122072583u},
                                    {0.4, 718748722u, 2503915459u},
                                    {0.4083333333333333, 99566454u, 1695864338u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 2977612036u, 3621045600u},
                                    {1.3, 1081661381u, 1424335380u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
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
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 1738698135u, 1399661560u},
                                    {0.06666666666666667, 917120901u, 734704865u},
                                    {0.075, 917807375u, 242719841u},
                                    {0.1, 2164943780u, 2484759652u},
                                    {0.10833333333333334, 4171579303u, 3843888149u},
                                    {0.2, 2767283640u, 486758129u},
                                    {0.3, 2453891243u, 136485861u},
                                    {0.375, 824263561u, 2714640u},
                                    {0.38333333333333336, 1610202677u, 588873312u},
                                    {0.4, 718748722u, 1234065661u},
                                    {0.4083333333333333, 99566454u, 1232828309u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 3310677075u, 1611521073u},
                                    {1.3, 1088702877u, 1404432133u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
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
        constexpr Frame frames[] = {{0, 1081661381u, 2466731237u},
                                    {0.008333333333333333, 3768059737u, 1712759657u},
                                    {0.03333333333333333, 471141575u, 3164162561u},
                                    {0.041666666666666664, 1738698135u, 1399661560u},
                                    {0.06666666666666667, 917120901u, 734704865u},
                                    {0.075, 917807375u, 242719841u},
                                    {0.1, 2164943780u, 2484759652u},
                                    {0.10833333333333334, 4171579303u, 3843888149u},
                                    {0.2, 2767283640u, 486758129u},
                                    {0.3, 2453891243u, 136485861u},
                                    {0.375, 824263561u, 2714640u},
                                    {0.38333333333333336, 1610202677u, 588873312u},
                                    {0.4, 718748722u, 1234065661u},
                                    {0.4083333333333333, 99566454u, 1232828309u},
                                    {0.5, 2803629914u, 3669825833u},
                                    {0.6, 438104686u, 4078842765u},
                                    {0.8333333333333334, 3656472715u, 13042685u},
                                    {0.8416666666666667, 184129791u, 3978357265u},
                                    {1, 186305051u, 3783814213u},
                                    {1.0083333333333333, 3357018544u, 786832129u},
                                    {1.1, 2983555811u, 1878255089u},
                                    {1.1083333333333334, 814713093u, 2654595073u},
                                    {1.15, 2831843441u, 2654595073u},
                                    {1.1583333333333334, 3310677075u, 1611521073u},
                                    {1.3, 1088702877u, 1404432133u},
                                    {1.4, 1081661381u, 1081661381u},
                                    {5, 1081661381u, 1081661381u}};
        PixelFx         fx(PixelFxKind::Exclamation);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Exclamation, value);
        REQUIRE(candidate);
        value.find("options")->set("actor", eve::Value());
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
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
TEST_CASE("stylize.pixelfx.ui.reverseSeekLayersPurityAndStopRestore") {
    for (auto kind : {PixelFxKind::DamageNumber, PixelFxKind::ComboCounter, PixelFxKind::Exclamation}) {
        PixelFx  fx(kind);
        uint32_t originalColor = 0, originalLight = 0;
        for (double t :
             {.13, 5., .04, .1, .10833333333333334, .38333333333333336, .5, .62, .84, 1.1, 1.16, .13, 0., .13}) {
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
            if (t == .13) {
                if (originalColor) {
                    REQUIRE(checksum(c) == originalColor);
                    REQUIRE(checksum(l) == originalLight);
                } else {
                    originalColor = checksum(c);
                    originalLight = checksum(l);
                }
            }
        }
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto c) { return c == 0; }));
        auto seek = fx.prepareSeek(.13);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        REQUIRE(checksum(fx.color()) == originalColor);
        REQUIRE(checksum(fx.light()) == originalLight);
    }
}
TEST_CASE("stylize.pixelfx.ui.partitionedClocksPreserveEmissionOrder") {
    for (auto kind : {PixelFxKind::DamageNumber, PixelFxKind::ComboCounter, PixelFxKind::Exclamation})
        for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
            PixelFx live(kind), reference(kind);
            double  elapsed = 0;
            for (double target : {.01, .034, .05, .075, .12, .13, .14, .5, .9, 1.5, 2.5}) {
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
TEST_CASE("stylize.pixelfx.ui.materialOwnershipRoundTripBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::DamageNumber, PixelFxKind::ComboCounter, PixelFxKind::Exclamation}) {
        PixelFx fx(kind);
        auto    s      = fx.settings();
        s.palette      = {0xff123456u};
        auto candidate = PixelFx::prepareConfiguration(kind, s);
        REQUIRE(candidate);
        s.palette.clear();
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().palette.size() == 8);
        for (auto c : fx.settings().palette) REQUIRE(c == 0xff123456u);
        auto seek = fx.prepareSeek(.13);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        const auto config = fx.configurationValue();
        const auto c = checksum(fx.color()), l = checksum(fx.light());
        auto       rt = PixelFx::prepareConfigurationValue(kind, config);
        REQUIRE(rt);
        PixelFx other(kind);
        REQUIRE(other.commitConfiguration(std::move(rt).takeValue()));
        seek = other.prepareSeek(.13);
        REQUIRE(seek);
        REQUIRE(other.commitSeek(std::move(seek).takeValue()));
        other.draw();
        REQUIRE(checksum(other.color()) == c);
        REQUIRE(checksum(other.light()) == l);
        for (size_t count : {size_t(0), size_t(65)}) {
            auto bad = fx.settings();
            bad.palette.assign(count, 0xff123456u);
            REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
        }
        auto bad    = fx.settings();
        bad.palette = {0x80123456u};
        REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
        bad = fx.settings();
        bad.palette.clear();
        REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
        auto unknown = config;
        unknown.find("options")->set("bogus", eve::Value(int64_t(1)));
        REQUIRE(!PixelFx::prepareConfigurationValue(kind, unknown));
        REQUIRE(fx.configurationValue() == config);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
        REQUIRE(fx.effectTime() == .13);
    }
}
TEST_CASE("stylize.pixelfx.ui.pinnedStaticStockBackBodyFrontComposition") {
    struct Frame {
        PixelFxKind kind;
        int         actor;
        double      time;
        uint32_t    color, light;
    };
    constexpr Frame frames[] = {
        {PixelFxKind::Exclamation, 1, 0, 1767026484u, 2466731237u},
        {PixelFxKind::Exclamation, 1, 0.008333333333333333, 579274000u, 1712759657u},
        {PixelFxKind::Exclamation, 1, 0.03333333333333333, 3085623250u, 3164162561u},
        {PixelFxKind::Exclamation, 1, 0.041666666666666664, 56516738u, 1399661560u},
        {PixelFxKind::Exclamation, 1, 0.05, 1194805478u, 2948990025u},
        {PixelFxKind::Exclamation, 1, 0.1, 3490908057u, 2484759652u},
        {PixelFxKind::Exclamation, 1, 0.2, 2778620037u, 486758129u},
        {PixelFxKind::Exclamation, 1, 0.3, 2406270062u, 136485861u},
        {PixelFxKind::Exclamation, 1, 0.5, 161340571u, 3669825833u},
        {PixelFxKind::Exclamation, 1, 0.6, 1600173815u, 4078842765u},
        {PixelFxKind::Exclamation, 1, 0.8333333333333334, 2522842830u, 13042685u},
        {PixelFxKind::Exclamation, 1, 1.0916666666666666, 537405300u, 1878255089u},
        {PixelFxKind::Exclamation, 1, 1.1, 4259112918u, 1878255089u},
        {PixelFxKind::Exclamation, 1, 1.1083333333333334, 23453172u, 2654595073u},
        {PixelFxKind::Exclamation, 1, 1.1583333333333334, 2253980006u, 1611521073u},
        {PixelFxKind::Exclamation, 1, 1.1666666666666667, 2465957048u, 4198024978u},
        {PixelFxKind::Exclamation, 1, 5, 1767026484u, 1081661381u},
        {PixelFxKind::Exclamation, 2, 0, 2353784175u, 2466731237u},
        {PixelFxKind::Exclamation, 2, 0.008333333333333333, 1696176435u, 1712759657u},
        {PixelFxKind::Exclamation, 2, 0.03333333333333333, 3247429433u, 3164162561u},
        {PixelFxKind::Exclamation, 2, 0.041666666666666664, 3636753961u, 1399661560u},
        {PixelFxKind::Exclamation, 2, 0.05, 337281941u, 2948990025u},
        {PixelFxKind::Exclamation, 2, 0.1, 1587097102u, 2484759652u},
        {PixelFxKind::Exclamation, 2, 0.2, 2047075618u, 486758129u},
        {PixelFxKind::Exclamation, 2, 0.3, 3637756637u, 136485861u},
        {PixelFxKind::Exclamation, 2, 0.5, 1723679428u, 3669825833u},
        {PixelFxKind::Exclamation, 2, 0.6, 741838616u, 4078842765u},
        {PixelFxKind::Exclamation, 2, 0.8333333333333334, 1073057277u, 13042685u},
        {PixelFxKind::Exclamation, 2, 1.0916666666666666, 3146596399u, 1878255089u},
        {PixelFxKind::Exclamation, 2, 1.1, 1956006469u, 1878255089u},
        {PixelFxKind::Exclamation, 2, 1.1083333333333334, 1268587695u, 2654595073u},
        {PixelFxKind::Exclamation, 2, 1.1583333333333334, 9549333u, 1611521073u},
        {PixelFxKind::Exclamation, 2, 1.1666666666666667, 1348393019u, 4198024978u},
        {PixelFxKind::Exclamation, 2, 5, 2353784175u, 1081661381u},
    };
    for (const auto& f : frames) {
        PixelFx fx(f.kind);
        auto    s      = fx.settings();
        s.actor.source = f.actor == 1 ? PixelFxActorSource::Hero : PixelFxActorSource::Dummy;
        auto candidate = PixelFx::prepareConfiguration(f.kind, s);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        const auto            info = fx.info();
        std::vector<uint32_t> c(size_t(info.width) * size_t(info.height)), l(c.size()), body(c.size());
        PixelFxCanvas         canvas{info.width, info.height, info.originX, info.originY, c, l};
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Back, {}));
        REQUIRE(fx.drawActor(body));
        for (size_t i = 0; i < body.size(); ++i)
            if (body[i]) c[i] = body[i];
        REQUIRE(fx.drawToCanvas(canvas, PixelFxLayer::Front, {}));
        REQUIRE(checksum(c) == f.color);
        REQUIRE(checksum(l) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.ui.sourceGlyphLayoutsUnicodeAndUnknownSpacing") {
    struct Expected {
        int                    x, width, height, glyphWidth;
        std::array<uint8_t, 7> rows;
    };
    {
        const auto word = ui::spell("abx!?ıſß中🙂", pixelfx::PixelFont::Mini);
        REQUIRE(word.width == 34);
        REQUIRE(word.height == 5);
        const std::vector<Expected> expected{
            {0, 3, 5, 3, {2, 5, 7, 5, 5, 0, 0}},  {4, 3, 5, 3, {3, 5, 3, 5, 3, 0, 0}},
            {8, 3, 5, 3, {0, 5, 2, 5, 0, 0, 0}},  {12, 1, 5, 1, {1, 1, 1, 0, 1, 0, 0}},
            {14, 3, 5, 3, {3, 4, 2, 0, 2, 0, 0}}, {18, 3, 5, 3, {7, 2, 2, 2, 7, 0, 0}},
            {22, 3, 5, 3, {6, 1, 2, 4, 3, 0, 0}}, {26, 2, 5, 0, {0, 0, 0, 0, 0, 0, 0}},
            {29, 2, 5, 0, {0, 0, 0, 0, 0, 0, 0}}, {32, 2, 5, 0, {0, 0, 0, 0, 0, 0, 0}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("IMW.19", pixelfx::PixelFont::Mini);
        REQUIRE(word.width == 21);
        REQUIRE(word.height == 5);
        const std::vector<Expected> expected{
            {0, 3, 5, 3, {7, 2, 2, 2, 7, 0, 0}},  {4, 3, 5, 3, {5, 7, 7, 5, 5, 0, 0}},
            {8, 3, 5, 3, {5, 5, 7, 7, 5, 0, 0}},  {12, 1, 5, 1, {0, 0, 0, 0, 1, 0, 0}},
            {14, 3, 5, 3, {2, 3, 2, 2, 7, 0, 0}}, {18, 3, 5, 3, {7, 5, 7, 4, 3, 0, 0}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("", pixelfx::PixelFont::Mini);
        REQUIRE(word.width == 0);
        REQUIRE(word.height == 5);
        const std::vector<Expected> expected{};
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("M A", pixelfx::PixelFont::Mini);
        REQUIRE(word.width == 10);
        REQUIRE(word.height == 5);
        const std::vector<Expected> expected{
            {0, 3, 5, 3, {5, 7, 7, 5, 5, 0, 0}},
            {4, 2, 5, 0, {0, 0, 0, 0, 0, 0, 0}},
            {7, 3, 5, 3, {2, 5, 7, 5, 5, 0, 0}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("abx!?ıſß中🙂", pixelfx::PixelFont::Big);
        REQUIRE(word.width == 47);
        REQUIRE(word.height == 7);
        const std::vector<Expected> expected{
            {0, 5, 7, 5, {14, 17, 17, 31, 17, 17, 17}}, {6, 5, 7, 5, {15, 17, 17, 15, 17, 17, 15}},
            {12, 5, 7, 5, {0, 0, 17, 10, 4, 10, 17}},   {18, 1, 7, 1, {1, 1, 1, 1, 1, 0, 1}},
            {20, 5, 7, 5, {14, 17, 16, 8, 4, 0, 4}},    {26, 3, 7, 3, {7, 2, 2, 2, 2, 2, 7}},
            {30, 5, 7, 5, {30, 1, 1, 14, 16, 16, 15}},  {36, 3, 7, 0, {0, 0, 0, 0, 0, 0, 0}},
            {40, 3, 7, 0, {0, 0, 0, 0, 0, 0, 0}},       {44, 3, 7, 0, {0, 0, 0, 0, 0, 0, 0}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("IMW.19", pixelfx::PixelFont::Big);
        REQUIRE(word.width == 27);
        REQUIRE(word.height == 7);
        const std::vector<Expected> expected{
            {0, 3, 7, 3, {7, 2, 2, 2, 2, 2, 7}},         {4, 5, 7, 5, {17, 27, 21, 21, 17, 17, 17}},
            {10, 5, 7, 5, {17, 17, 17, 21, 21, 21, 10}}, {16, 1, 7, 1, {0, 0, 0, 0, 0, 0, 1}},
            {18, 3, 7, 3, {2, 3, 2, 2, 2, 2, 7}},        {22, 5, 7, 5, {14, 17, 17, 30, 16, 8, 6}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("", pixelfx::PixelFont::Big);
        REQUIRE(word.width == 0);
        REQUIRE(word.height == 7);
        const std::vector<Expected> expected{};
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
    {
        const auto word = ui::spell("M A", pixelfx::PixelFont::Big);
        REQUIRE(word.width == 15);
        REQUIRE(word.height == 7);
        const std::vector<Expected> expected{
            {0, 5, 7, 5, {17, 27, 21, 21, 17, 17, 17}},
            {6, 3, 7, 0, {0, 0, 0, 0, 0, 0, 0}},
            {10, 5, 7, 5, {14, 17, 17, 31, 17, 17, 17}},
        };
        REQUIRE(word.letters.size() == expected.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            const auto& a = word.letters[i];
            const auto& e = expected[i];
            REQUIRE(a.x == e.x);
            REQUIRE(a.width == e.width);
            REQUIRE(a.height == e.height);
            REQUIRE(a.glyph.width == e.glyphWidth);
            REQUIRE(a.glyph.rows == e.rows);
        }
    }
}
TEST_CASE("stylize.pixelfx.ui.sourceWordBevelOutlineShadowShineOverlapAndFlash") {
    struct Case {
        pixelfx::PixelFont font;
        std::string_view   text;
        int                variant;
        uint32_t           color, light;
    };
    const Case cases[] = {
        {pixelfx::PixelFont::Mini, "abx!?ıſß中🙂", 0, 259804479u, 998546885u},
        {pixelfx::PixelFont::Mini, "abx!?ıſß中🙂", 1, 1656353878u, 998546885u},
        {pixelfx::PixelFont::Mini, "abx!?ıſß中🙂", 2, 419142364u, 998546885u},
        {pixelfx::PixelFont::Mini, "abx!?ıſß中🙂", 3, 1832529252u, 998546885u},
        {pixelfx::PixelFont::Mini, "abx!?ıſß中🙂", 4, 4022810638u, 998546885u},
        {pixelfx::PixelFont::Mini, "IMW.19", 0, 3880646218u, 998546885u},
        {pixelfx::PixelFont::Mini, "IMW.19", 1, 1761936639u, 998546885u},
        {pixelfx::PixelFont::Mini, "IMW.19", 2, 3763853352u, 998546885u},
        {pixelfx::PixelFont::Mini, "IMW.19", 3, 1495014618u, 998546885u},
        {pixelfx::PixelFont::Mini, "IMW.19", 4, 3656206375u, 998546885u},
        {pixelfx::PixelFont::Mini, "", 0, 998546885u, 998546885u},
        {pixelfx::PixelFont::Mini, "", 1, 998546885u, 998546885u},
        {pixelfx::PixelFont::Mini, "", 2, 998546885u, 998546885u},
        {pixelfx::PixelFont::Mini, "", 3, 998546885u, 998546885u},
        {pixelfx::PixelFont::Mini, "", 4, 998546885u, 998546885u},
        {pixelfx::PixelFont::Mini, "M A", 0, 1871518283u, 998546885u},
        {pixelfx::PixelFont::Mini, "M A", 1, 3271190032u, 998546885u},
        {pixelfx::PixelFont::Mini, "M A", 2, 3691801413u, 998546885u},
        {pixelfx::PixelFont::Mini, "M A", 3, 3167581504u, 998546885u},
        {pixelfx::PixelFont::Mini, "M A", 4, 3009626349u, 998546885u},
        {pixelfx::PixelFont::Big, "abx!?ıſß中🙂", 0, 407005309u, 998546885u},
        {pixelfx::PixelFont::Big, "abx!?ıſß中🙂", 1, 3652316153u, 998546885u},
        {pixelfx::PixelFont::Big, "abx!?ıſß中🙂", 2, 2052993985u, 998546885u},
        {pixelfx::PixelFont::Big, "abx!?ıſß中🙂", 3, 3581399766u, 998546885u},
        {pixelfx::PixelFont::Big, "abx!?ıſß中🙂", 4, 1696858657u, 998546885u},
        {pixelfx::PixelFont::Big, "IMW.19", 0, 1454516274u, 998546885u},
        {pixelfx::PixelFont::Big, "IMW.19", 1, 862453731u, 998546885u},
        {pixelfx::PixelFont::Big, "IMW.19", 2, 700713765u, 998546885u},
        {pixelfx::PixelFont::Big, "IMW.19", 3, 1223897890u, 998546885u},
        {pixelfx::PixelFont::Big, "IMW.19", 4, 1337320854u, 998546885u},
        {pixelfx::PixelFont::Big, "", 0, 998546885u, 998546885u},
        {pixelfx::PixelFont::Big, "", 1, 998546885u, 998546885u},
        {pixelfx::PixelFont::Big, "", 2, 998546885u, 998546885u},
        {pixelfx::PixelFont::Big, "", 3, 998546885u, 998546885u},
        {pixelfx::PixelFont::Big, "", 4, 998546885u, 998546885u},
        {pixelfx::PixelFont::Big, "M A", 0, 3401499874u, 998546885u},
        {pixelfx::PixelFont::Big, "M A", 1, 2429712633u, 998546885u},
        {pixelfx::PixelFont::Big, "M A", 2, 2742431502u, 998546885u},
        {pixelfx::PixelFont::Big, "M A", 3, 1759238508u, 998546885u},
        {pixelfx::PixelFont::Big, "M A", 4, 381670365u, 998546885u},
    };
    const PixelFxInfo             info{PixelFxKind::DamageNumber, "ui/word-fixture", 256, 128, 128, 96, 1, false};
    std::vector<uint32_t>         color(256 * 128), light(color.size());
    ui::WordScratch               scratch(4096, 128);
    const std::array<uint32_t, 4> colors{4281541137u, 4281550028u, 4286023935u, 4290506495u};
    const std::array<std::array<double, 7>, 5> transforms{{{2, 2, -40, -40, 1, 0, 0},
                                                           {1.3, 2.7, -35.25, -36.6, .6, .3, .5},
                                                           {3.4, .8, -45.1, -18.4, 1, .7, -.5},
                                                           {.65, 1.4, -24.8, -23.2, .35, 0, 1},
                                                           {2, 1.7, -40.3, -26.8, 1, 0, .5}}};
    for (const auto& c : cases) {
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        pixelfx::Paint painter{info, color, light, 1};
        const auto     word = ui::spell(c.text, c.font);
        const auto     tr   = transforms[size_t(c.variant)];
        ui::WordItems  items;
        for (size_t i = 0; i < word.letters.size(); ++i) {
            const auto& L = word.letters[i];
            if (!L.glyph.width) continue;
            items.add({L.glyph, L.height, tr[2] + L.x * tr[0] * (c.variant == 4 ? .7 : 1),
                       tr[3] + (c.variant == 4 ? (i % 2 ? 1.25 : -.5) : 0), tr[0], tr[1],
                       c.variant == 4 ? double(i % 3) / 2 : tr[4], tr[5], tr[6]});
        }
        ui::WordStyle style{c.variant == 4 ? std::span<const uint32_t>(colors).subspan(2, 1)
                                           : std::span<const uint32_t>(colors)};
        style.edge         = c.variant == 1 ? 0 : colors[3];
        style.under        = colors[0];
        style.outline      = c.variant == 4 ? 0 : colors[0];
        style.shadow       = c.variant == 4 ? 0 : pixelfx::scale(colors[0], .5);
        style.outlineWidth = c.variant == 3 ? 2 : 1;
        style.shine        = c.variant == 3 ? std::optional<double>(-10) : std::nullopt;
        style.shineY       = -30;
        style.glint        = 4287168358u;
        scratch.draw(painter, items.view(), style);
        REQUIRE(checksum(color) == c.color);
        REQUIRE(checksum(light) == c.light);
    }
}
TEST_CASE("stylize.pixelfx.ui.sourceWholePixelShakeBoundariesAndSeeds") {
    struct Case {
        uint32_t seed;
        double   time, x, y;
    };
    const Case cases[] = {
        {0u, 0, 0, 0},
        {0u, 0.02, 0, 0},
        {0u, 0.1, 5, -2},
        {0u, 0.11, 4, -2},
        {0u, 0.19, 2, 0},
        {0u, 0.3, 0, 0},
        {0u, 0.4, 0, 0},
        {0u, 1.1, 0, 0},
        {1u, 0, 0, 0},
        {1u, 0.02, 0, 0},
        {1u, 0.1, -4, 0},
        {1u, 0.11, -2, 1},
        {1u, 0.19, 3, 1},
        {1u, 0.3, 0, 0},
        {1u, 0.4, 0, 0},
        {1u, 1.1, 0, 0},
        {4294967295u, 0, 0, 0},
        {4294967295u, 0.02, 0, 0},
        {4294967295u, 0.1, 5, -1},
        {4294967295u, 0.11, 3, -1},
        {4294967295u, 0.19, -1, 1},
        {4294967295u, 0.3, 0, 0},
        {4294967295u, 0.4, 0, 0},
        {4294967295u, 1.1, 0, 0},
    };
    for (const auto& c : cases) {
        const auto point = ui::shake(c.time, 3, .1, .3, c.seed);
        REQUIRE(point.x == c.x);
        REQUIRE(point.y == c.y);
    }
}
TEST_CASE("stylize.pixelfx.ui.contentOwnershipBoundsAndMalformedFailures") {
    for (auto kind : {PixelFxKind::DamageNumber, PixelFxKind::ComboCounter}) {
        PixelFx           fx(kind);
        auto              settings = fx.settings();
        const std::string expected = "I1 ıſ🙂";
        if (kind == PixelFxKind::DamageNumber)
            settings.ui.text = expected;
        else
            settings.ui.label = expected;
        auto prepared = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(prepared);
        settings.ui.text.clear();
        settings.ui.label.clear();
        REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
        REQUIRE((kind == PixelFxKind::DamageNumber ? fx.settings().ui.text : fx.settings().ui.label) == expected);
        const auto  before = fx.configurationValue();
        std::string unicode64;
        for (int i = 0; i < 64; ++i) unicode64 += "🙂";
        for (const auto& text : std::vector<std::string>{"", std::string(64, 'M'), unicode64}) {
            auto s = fx.settings();
            if (kind == PixelFxKind::DamageNumber)
                s.ui.text = text;
            else
                s.ui.label = text;
            REQUIRE(PixelFx::prepareConfiguration(kind, s));
        }
        for (const auto& text :
             std::vector<std::string>{std::string(65, 'M'), unicode64 + "🙂", std::string("\xc0\xaf", 2)}) {
            auto s = fx.settings();
            if (kind == PixelFxKind::DamageNumber)
                s.ui.text = text;
            else
                s.ui.label = text;
            REQUIRE(!PixelFx::prepareConfiguration(kind, s));
            REQUIRE(fx.configurationValue() == before);
        }
        for (const char* json : {"null", "123", "[]"}) {
            auto input = eve::Value::fromJson(json);
            REQUIRE(input);
            auto value = before;
            value.find("options")->set(kind == PixelFxKind::DamageNumber ? "text" : "label", input.value());
            REQUIRE(!PixelFx::prepareConfigurationValue(kind, value));
            REQUIRE(fx.configurationValue() == before);
        }
    }
    PixelFx    combo(PixelFxKind::ComboCounter);
    const auto before = combo.configurationValue();
    for (double v :
         {-1000000., 1000000., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto s     = combo.settings();
        s.ui.count = v;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ComboCounter, s));
        REQUIRE(combo.configurationValue() == before);
    }
    for (double v : {-999999., -12.5, -.5, 0., .5, 5.49, 5.5, 999999.}) {
        auto s     = combo.settings();
        s.ui.count = v;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::ComboCounter, s));
    }
}
}  // namespace test_pixelfx_ui_unit
