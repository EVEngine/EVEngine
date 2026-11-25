// Pinned upstream melee geometry and raster fixtures; no runtime source interpreter required.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/UiHud.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_ui_impact_unit {
using namespace eve::stylize;
namespace ui = eve::stylize::pixelfx::ui;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.ui.impact.critNumberPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 4139797698u, 1458625824u},
                                    {0.075, 891258106u, 797064650u},
                                    {0.08333333333333333, 3459814269u, 975084383u},
                                    {0.09166666666666666, 2250826793u, 2703418059u},
                                    {0.13333333333333333, 4046530373u, 2453407289u},
                                    {0.14166666666666666, 2942854910u, 3449849861u},
                                    {0.15, 4014249681u, 256939460u},
                                    {0.19166666666666668, 992681137u, 93247266u},
                                    {0.2, 2413455979u, 2003352685u},
                                    {0.20833333333333334, 727928519u, 1479504775u},
                                    {0.23333333333333334, 1532718956u, 3822549187u},
                                    {0.24166666666666667, 3636642541u, 1496184299u},
                                    {0.49166666666666664, 3397333056u, 3044792356u},
                                    {0.5, 1779480787u, 813450592u},
                                    {0.5083333333333333, 1239458223u, 1882542250u},
                                    {0.6166666666666667, 3632919495u, 3307695694u},
                                    {0.625, 1420210413u, 909573270u},
                                    {0.6333333333333333, 4162023291u, 1052475569u},
                                    {0.75, 1515482590u, 1534141959u},
                                    {0.925, 2171837660u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3351575808u, 2985606341u},
                                    {0.008333333333333333, 1001492760u, 174061645u},
                                    {0.03333333333333333, 24296815u, 1831888641u},
                                    {0.041666666666666664, 2511075579u, 23077936u},
                                    {0.06666666666666667, 1753044115u, 3294944551u},
                                    {0.075, 5764026u, 670225127u},
                                    {0.08333333333333333, 3909255346u, 3338490268u},
                                    {0.09166666666666666, 1316493354u, 1204113569u},
                                    {0.13333333333333333, 2578326628u, 3964854590u},
                                    {0.14166666666666666, 4157769681u, 676498352u},
                                    {0.15, 3139759683u, 3601458798u},
                                    {0.19166666666666668, 2544533636u, 3461475363u},
                                    {0.2, 2965929348u, 1650100725u},
                                    {0.20833333333333334, 2376533728u, 1098365962u},
                                    {0.23333333333333334, 1104352663u, 3785113103u},
                                    {0.24166666666666667, 1052332923u, 3568511693u},
                                    {0.49166666666666664, 1629798678u, 2650416380u},
                                    {0.5, 3531881415u, 1976706421u},
                                    {0.5083333333333333, 448099898u, 4273873162u},
                                    {0.6166666666666667, 1391977962u, 22164737u},
                                    {0.625, 1334658856u, 2106988712u},
                                    {0.6333333333333333, 1934840573u, 2179481401u},
                                    {0.75, 3068196801u, 1889794443u},
                                    {0.925, 259870538u, 3141836962u},
                                    {0.9833333333333333, 3830341558u, 2262973601u},
                                    {1.05, 1724605543u, 2356586986u},
                                    {1.0666666666666667, 3576785485u, 1571172318u},
                                    {1.1083333333333334, 787257147u, 2883313492u},
                                    {1.3, 1420759096u, 175851732u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_text = eve::Value::fromJson(R"json("999!")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto input_label = eve::Value::fromJson(R"json("HIT")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 1669523096u, 2985606341u},
                                    {0.008333333333333333, 3352893444u, 1609797517u},
                                    {0.03333333333333333, 868296180u, 1923052468u},
                                    {0.041666666666666664, 1777572379u, 2992190011u},
                                    {0.06666666666666667, 3006713972u, 65939694u},
                                    {0.075, 3224592366u, 3341732418u},
                                    {0.08333333333333333, 2910305937u, 3362308252u},
                                    {0.09166666666666666, 833806671u, 2591689779u},
                                    {0.13333333333333333, 873632050u, 2313716346u},
                                    {0.14166666666666666, 186390524u, 641188763u},
                                    {0.15, 3957863424u, 635850274u},
                                    {0.19166666666666668, 104626365u, 3328258466u},
                                    {0.2, 1376134729u, 3510227147u},
                                    {0.20833333333333334, 1237500803u, 2816853591u},
                                    {0.23333333333333334, 1598276978u, 2374540995u},
                                    {0.24166666666666667, 962942669u, 1811307938u},
                                    {0.49166666666666664, 4218614816u, 1245249248u},
                                    {0.5, 1951330769u, 194090424u},
                                    {0.5083333333333333, 2695460443u, 1092889212u},
                                    {0.6166666666666667, 3860047732u, 3777774445u},
                                    {0.625, 1291849117u, 3448332006u},
                                    {0.6333333333333333, 1440542808u, 2307818026u},
                                    {0.75, 4258370800u, 52373597u},
                                    {0.925, 1582601909u, 1315878591u},
                                    {0.9833333333333333, 467918302u, 1304785734u},
                                    {1.05, 2267749162u, 639003391u},
                                    {1.0666666666666667, 2963622360u, 4240096228u},
                                    {1.1083333333333334, 1560096636u, 905498451u},
                                    {1.3, 1760487034u, 1566950156u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3895937196u, 2985606341u},
                                    {0.008333333333333333, 1142261007u, 1609797517u},
                                    {0.03333333333333333, 252257415u, 3433799907u},
                                    {0.041666666666666664, 3438062328u, 2271746005u},
                                    {0.06666666666666667, 2283426805u, 3025681745u},
                                    {0.075, 1738311647u, 3323097261u},
                                    {0.08333333333333333, 1331471911u, 2323120414u},
                                    {0.09166666666666666, 980296028u, 4250801461u},
                                    {0.13333333333333333, 2353429236u, 1236885294u},
                                    {0.14166666666666666, 3376567354u, 3796059937u},
                                    {0.15, 2583012504u, 104866916u},
                                    {0.19166666666666668, 3792718911u, 1075130894u},
                                    {0.2, 1994717623u, 2176122969u},
                                    {0.20833333333333334, 327013918u, 1644397149u},
                                    {0.23333333333333334, 2987308975u, 2038800589u},
                                    {0.24166666666666667, 1411266664u, 2528285242u},
                                    {0.49166666666666664, 882332003u, 1471716913u},
                                    {0.5, 3228552830u, 3471276598u},
                                    {0.5083333333333333, 1126789682u, 3663636014u},
                                    {0.6166666666666667, 3800290882u, 3348222552u},
                                    {0.625, 2828093935u, 3405107884u},
                                    {0.6333333333333333, 652929084u, 581214498u},
                                    {0.75, 1979845757u, 473467095u},
                                    {0.925, 2392300667u, 4130540061u},
                                    {0.9833333333333333, 2267198961u, 3742897682u},
                                    {1.05, 2777923937u, 2050460254u},
                                    {1.0666666666666667, 197711480u, 2243408091u},
                                    {1.1083333333333334, 3267459051u, 4046934324u},
                                    {1.3, 3075224457u, 588027623u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3617811881u, 2985606341u},
                                    {0.008333333333333333, 388600477u, 1436398901u},
                                    {0.03333333333333333, 502429273u, 2729586985u},
                                    {0.041666666666666664, 1452177097u, 3289896710u},
                                    {0.06666666666666667, 2196214461u, 878922744u},
                                    {0.075, 1658503425u, 1522255952u},
                                    {0.08333333333333333, 538742205u, 3957419820u},
                                    {0.09166666666666666, 93096529u, 343978764u},
                                    {0.13333333333333333, 1540276617u, 4235973393u},
                                    {0.14166666666666666, 602335773u, 2056937627u},
                                    {0.15, 1018732309u, 1168727885u},
                                    {0.19166666666666668, 1313537261u, 1392184850u},
                                    {0.2, 3620623865u, 998721237u},
                                    {0.20833333333333334, 4052889041u, 3940460234u},
                                    {0.23333333333333334, 1998232861u, 2019254865u},
                                    {0.24166666666666667, 738197577u, 908106171u},
                                    {0.49166666666666664, 2213906529u, 1506547881u},
                                    {0.5, 1561563825u, 744975272u},
                                    {0.5083333333333333, 751767645u, 3881768149u},
                                    {0.6166666666666667, 636894493u, 3888809226u},
                                    {0.625, 2382083125u, 1668948296u},
                                    {0.6333333333333333, 144166781u, 1123703377u},
                                    {0.75, 2636324849u, 1134873380u},
                                    {0.925, 2491112789u, 926865502u},
                                    {0.9833333333333333, 3352116537u, 1829939224u},
                                    {1.05, 1773356645u, 1095968659u},
                                    {1.0666666666666667, 1973376441u, 2062137433u},
                                    {1.1083333333333334, 3172532717u, 991600682u},
                                    {1.3, 1115732525u, 1756379436u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 1659616463u, 2985606341u},
                                    {0.008333333333333333, 2620236590u, 1738251477u},
                                    {0.03333333333333333, 428360028u, 2320108953u},
                                    {0.041666666666666664, 3076529804u, 4127293721u},
                                    {0.06666666666666667, 529794232u, 4133763093u},
                                    {0.075, 2322886861u, 1262683297u},
                                    {0.08333333333333333, 2548782519u, 3714075865u},
                                    {0.09166666666666666, 348331330u, 2107452158u},
                                    {0.13333333333333333, 2351990303u, 3871641779u},
                                    {0.14166666666666666, 293408374u, 3582350315u},
                                    {0.15, 29702653u, 495949957u},
                                    {0.19166666666666668, 2265313062u, 513480990u},
                                    {0.2, 2744869371u, 1043182625u},
                                    {0.20833333333333334, 3077356532u, 819816238u},
                                    {0.23333333333333334, 4170656435u, 2604109850u},
                                    {0.24166666666666667, 1608052053u, 2686895320u},
                                    {0.49166666666666664, 2688855857u, 1799199333u},
                                    {0.5, 3286615952u, 1994795052u},
                                    {0.5083333333333333, 1869991203u, 2927374507u},
                                    {0.6166666666666667, 1629929898u, 4243392622u},
                                    {0.625, 4210237134u, 2770717069u},
                                    {0.6333333333333333, 4209929615u, 2553449259u},
                                    {0.75, 275263322u, 3475628425u},
                                    {0.925, 4156718353u, 1091799489u},
                                    {0.9833333333333333, 1943926287u, 711803426u},
                                    {1.05, 1336368836u, 495374440u},
                                    {1.0666666666666667, 2657710292u, 3608156517u},
                                    {1.1083333333333334, 1217526737u, 2937426861u},
                                    {1.3, 763433207u, 1662520869u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 393249123u, 2985606341u},
                                    {0.008333333333333333, 3827500141u, 1609797517u},
                                    {0.03333333333333333, 3931332595u, 3858272491u},
                                    {0.041666666666666664, 3164362977u, 1881924444u},
                                    {0.06666666666666667, 3816009377u, 1458625824u},
                                    {0.075, 3989874674u, 797064650u},
                                    {0.08333333333333333, 1729872635u, 975084383u},
                                    {0.09166666666666666, 2223330772u, 2703418059u},
                                    {0.13333333333333333, 3853990446u, 2453407289u},
                                    {0.14166666666666666, 873198504u, 3449849861u},
                                    {0.15, 661537647u, 256939460u},
                                    {0.19166666666666668, 1693811213u, 93247266u},
                                    {0.2, 3011215747u, 2003352685u},
                                    {0.20833333333333334, 3961464644u, 1479504775u},
                                    {0.23333333333333334, 3699915431u, 3822549187u},
                                    {0.24166666666666667, 1213498881u, 1496184299u},
                                    {0.49166666666666664, 2862275875u, 930977948u},
                                    {0.5, 1041532407u, 3786289236u},
                                    {0.5083333333333333, 1310575039u, 516630449u},
                                    {0.6166666666666667, 2584122534u, 908280752u},
                                    {0.625, 4028031291u, 1474352000u},
                                    {0.6333333333333333, 2241706829u, 3936097941u},
                                    {0.75, 4251923842u, 545401171u},
                                    {0.925, 3751055424u, 1541669617u},
                                    {0.9833333333333333, 3017175192u, 2529960977u},
                                    {1.05, 1999594946u, 3332750914u},
                                    {1.0666666666666667, 4113293190u, 1454222248u},
                                    {1.1083333333333334, 2345267749u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 393249123u, 2985606341u},
                                    {0.008333333333333333, 3827500141u, 1609797517u},
                                    {0.03333333333333333, 3931332595u, 3858272491u},
                                    {0.041666666666666664, 3164362977u, 1881924444u},
                                    {0.06666666666666667, 3816009377u, 1458625824u},
                                    {0.075, 3989874674u, 797064650u},
                                    {0.08333333333333333, 1729872635u, 975084383u},
                                    {0.09166666666666666, 2223330772u, 2703418059u},
                                    {0.13333333333333333, 3853990446u, 2453407289u},
                                    {0.14166666666666666, 873198504u, 3449849861u},
                                    {0.15, 661537647u, 256939460u},
                                    {0.19166666666666668, 1693811213u, 93247266u},
                                    {0.2, 3011215747u, 2003352685u},
                                    {0.20833333333333334, 3961464644u, 1479504775u},
                                    {0.23333333333333334, 3699915431u, 3822549187u},
                                    {0.24166666666666667, 1213498881u, 1496184299u},
                                    {0.49166666666666664, 2862275875u, 3730476776u},
                                    {0.5, 1041532407u, 3786289236u},
                                    {0.5083333333333333, 1310575039u, 516630449u},
                                    {0.6166666666666667, 2410783538u, 659011422u},
                                    {0.625, 545699269u, 2005629110u},
                                    {0.6333333333333333, 1570962619u, 2113820293u},
                                    {0.75, 138111898u, 4181962565u},
                                    {0.925, 3751055424u, 1541669617u},
                                    {0.9833333333333333, 3017175192u, 2529960977u},
                                    {1.05, 1999594946u, 3332750914u},
                                    {1.0666666666666667, 4113293190u, 1454222248u},
                                    {1.1083333333333334, 2345267749u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json(" ")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 210787359u, 2985606341u},
                                    {0.008333333333333333, 3590926226u, 1609797517u},
                                    {0.03333333333333333, 2132441435u, 3858272491u},
                                    {0.041666666666666664, 3082031972u, 1881924444u},
                                    {0.06666666666666667, 4166823570u, 1458625824u},
                                    {0.075, 2729334564u, 797064650u},
                                    {0.08333333333333333, 1660985209u, 975084383u},
                                    {0.09166666666666666, 2648932941u, 2703418059u},
                                    {0.13333333333333333, 3809917601u, 2453407289u},
                                    {0.14166666666666666, 2768058041u, 3449849861u},
                                    {0.15, 3139339979u, 256939460u},
                                    {0.19166666666666668, 289280220u, 93247266u},
                                    {0.2, 3271949921u, 2003352685u},
                                    {0.20833333333333334, 3653666870u, 1479504775u},
                                    {0.23333333333333334, 1624077663u, 3822549187u},
                                    {0.24166666666666667, 3032092189u, 1496184299u},
                                    {0.49166666666666664, 3995369586u, 3044792356u},
                                    {0.5, 3914386461u, 813450592u},
                                    {0.5083333333333333, 4199035325u, 1882542250u},
                                    {0.6166666666666667, 422311952u, 3307695694u},
                                    {0.625, 3476283455u, 909573270u},
                                    {0.6333333333333333, 2459973525u, 1052475569u},
                                    {0.75, 3714468225u, 1534141959u},
                                    {0.925, 3295706944u, 1541669617u},
                                    {0.9833333333333333, 628927159u, 2529960977u},
                                    {1.05, 2537573078u, 3332750914u},
                                    {1.0666666666666667, 1987362292u, 1454222248u},
                                    {1.1083333333333334, 2203688723u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("abc")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 1872292234u, 2985606341u},
                                    {0.008333333333333333, 3167773364u, 1609797517u},
                                    {0.03333333333333333, 2262876385u, 3858272491u},
                                    {0.041666666666666664, 784354370u, 1881924444u},
                                    {0.06666666666666667, 1105790620u, 1458625824u},
                                    {0.075, 1420697994u, 797064650u},
                                    {0.08333333333333333, 3147050845u, 975084383u},
                                    {0.09166666666666666, 140040878u, 2703418059u},
                                    {0.13333333333333333, 142560280u, 2453407289u},
                                    {0.14166666666666666, 725714612u, 3449849861u},
                                    {0.15, 4128364761u, 256939460u},
                                    {0.19166666666666668, 924951603u, 93247266u},
                                    {0.2, 1690536477u, 2003352685u},
                                    {0.20833333333333334, 1730318853u, 1479504775u},
                                    {0.23333333333333334, 3205113739u, 3822549187u},
                                    {0.24166666666666667, 1868235468u, 1496184299u},
                                    {0.49166666666666664, 3121827990u, 2820283199u},
                                    {0.5, 4258835580u, 3122235687u},
                                    {0.5083333333333333, 1629564227u, 3331822131u},
                                    {0.6166666666666667, 2074245896u, 430423760u},
                                    {0.625, 1881221236u, 3234688074u},
                                    {0.6333333333333333, 890735814u, 2534280984u},
                                    {0.75, 441354785u, 1045031505u},
                                    {0.925, 260839988u, 1541669617u},
                                    {0.9833333333333333, 3515734319u, 2529960977u},
                                    {1.05, 3613103478u, 3332750914u},
                                    {1.0666666666666667, 1234665895u, 1454222248u},
                                    {1.1083333333333334, 3588296716u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("I1.MW!?")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 2651507086u, 2985606341u},
                                    {0.008333333333333333, 3077160032u, 1609797517u},
                                    {0.03333333333333333, 2660000433u, 3858272491u},
                                    {0.041666666666666664, 1279204907u, 1881924444u},
                                    {0.06666666666666667, 3945826274u, 1458625824u},
                                    {0.075, 3109875665u, 797064650u},
                                    {0.08333333333333333, 898098512u, 975084383u},
                                    {0.09166666666666666, 1337458796u, 2703418059u},
                                    {0.13333333333333333, 2854320834u, 2453407289u},
                                    {0.14166666666666666, 2511784943u, 3449849861u},
                                    {0.15, 3294710445u, 256939460u},
                                    {0.19166666666666668, 2316507985u, 93247266u},
                                    {0.2, 2451752951u, 2003352685u},
                                    {0.20833333333333334, 3474851225u, 1479504775u},
                                    {0.23333333333333334, 2954100342u, 3822549187u},
                                    {0.24166666666666667, 788240963u, 1496184299u},
                                    {0.49166666666666664, 506991415u, 3096987495u},
                                    {0.5, 4075096892u, 1403976331u},
                                    {0.5083333333333333, 4019843612u, 882172052u},
                                    {0.6166666666666667, 1311736919u, 444030758u},
                                    {0.625, 2871475030u, 2076054952u},
                                    {0.6333333333333333, 3677171456u, 1125587347u},
                                    {0.75, 3348905778u, 519171820u},
                                    {0.925, 3654912342u, 1541669617u},
                                    {0.9833333333333333, 4061670389u, 2529960977u},
                                    {1.05, 2354720272u, 3332750914u},
                                    {1.0666666666666667, 3954907866u, 1454222248u},
                                    {1.1083333333333334, 3254991225u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(R"json("\u0131\u017f\u00df\u4e2d\ud83d\ude42")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 2297385983u, 2985606341u},
                                    {0.008333333333333333, 175680793u, 1609797517u},
                                    {0.03333333333333333, 3111541156u, 3858272491u},
                                    {0.041666666666666664, 1202605429u, 1881924444u},
                                    {0.06666666666666667, 3929466690u, 1458625824u},
                                    {0.075, 3588800305u, 797064650u},
                                    {0.08333333333333333, 3729523337u, 975084383u},
                                    {0.09166666666666666, 2718876919u, 2703418059u},
                                    {0.13333333333333333, 3115006598u, 2453407289u},
                                    {0.14166666666666666, 717067431u, 3449849861u},
                                    {0.15, 3482734640u, 256939460u},
                                    {0.19166666666666668, 3112723583u, 93247266u},
                                    {0.2, 994997332u, 2003352685u},
                                    {0.20833333333333334, 3671336726u, 1479504775u},
                                    {0.23333333333333334, 4110997115u, 3822549187u},
                                    {0.24166666666666667, 775350938u, 1496184299u},
                                    {0.49166666666666664, 4242037265u, 930977948u},
                                    {0.5, 2714901888u, 3786289236u},
                                    {0.5083333333333333, 202679755u, 516630449u},
                                    {0.6166666666666667, 2500153272u, 2979921452u},
                                    {0.625, 3175918982u, 4013969202u},
                                    {0.6333333333333333, 2458820747u, 1184634711u},
                                    {0.75, 966956587u, 1109270965u},
                                    {0.925, 1651722619u, 1541669617u},
                                    {0.9833333333333333, 2536596036u, 2529960977u},
                                    {1.05, 3050841776u, 3332750914u},
                                    {1.0666666666666667, 759045900u, 1454222248u},
                                    {1.1083333333333334, 2249223554u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        auto input_text =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 393249123u, 2985606341u},
                                    {0.008333333333333333, 3827500141u, 1609797517u},
                                    {0.03333333333333333, 3931332595u, 3858272491u},
                                    {0.041666666666666664, 3164362977u, 1881924444u},
                                    {0.06666666666666667, 3816009377u, 1458625824u},
                                    {0.075, 3989874674u, 797064650u},
                                    {0.08333333333333333, 1729872635u, 975084383u},
                                    {0.09166666666666666, 2223330772u, 2703418059u},
                                    {0.13333333333333333, 3853990446u, 2453407289u},
                                    {0.14166666666666666, 873198504u, 3449849861u},
                                    {0.15, 661537647u, 256939460u},
                                    {0.19166666666666668, 1693811213u, 93247266u},
                                    {0.2, 3011215747u, 2003352685u},
                                    {0.20833333333333334, 3961464644u, 1479504775u},
                                    {0.23333333333333334, 3699915431u, 3822549187u},
                                    {0.24166666666666667, 1213498881u, 1496184299u},
                                    {0.49166666666666664, 2862275875u, 697839382u},
                                    {0.5, 1041532407u, 3786289236u},
                                    {0.5083333333333333, 1310575039u, 516630449u},
                                    {0.6166666666666667, 4022393011u, 2979921452u},
                                    {0.625, 1460239639u, 4013969202u},
                                    {0.6333333333333333, 2483919309u, 1184634711u},
                                    {0.75, 968289654u, 1109270965u},
                                    {0.925, 3751055424u, 1541669617u},
                                    {0.9833333333333333, 3017175192u, 2529960977u},
                                    {1.05, 1999594946u, 3332750914u},
                                    {1.0666666666666667, 4113293190u, 1454222248u},
                                    {1.1083333333333334, 2345267749u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_text = eve::Value::fromJson(
            R"json("\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 1736100315u, 1458625824u},
                                    {0.075, 853447706u, 797064650u},
                                    {0.08333333333333333, 3223057532u, 975084383u},
                                    {0.09166666666666666, 2935371414u, 2703418059u},
                                    {0.13333333333333333, 1626164359u, 2453407289u},
                                    {0.14166666666666666, 2281894959u, 3449849861u},
                                    {0.15, 2997250922u, 256939460u},
                                    {0.19166666666666668, 1478332236u, 93247266u},
                                    {0.2, 3620528183u, 2003352685u},
                                    {0.20833333333333334, 1126309712u, 1479504775u},
                                    {0.23333333333333334, 2731103548u, 3822549187u},
                                    {0.24166666666666667, 2402678999u, 1496184299u},
                                    {0.49166666666666664, 4198369870u, 3044792356u},
                                    {0.5, 2330426257u, 813450592u},
                                    {0.5083333333333333, 2488943021u, 1882542250u},
                                    {0.6166666666666667, 1736808204u, 3307695694u},
                                    {0.625, 1312442275u, 909573270u},
                                    {0.6333333333333333, 1474878449u, 1052475569u},
                                    {0.75, 3535850142u, 1534141959u},
                                    {0.925, 4174015381u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 3514051497u, 1458625824u},
                                    {0.075, 3441103332u, 797064650u},
                                    {0.08333333333333333, 1342648185u, 975084383u},
                                    {0.09166666666666666, 3810622912u, 2703418059u},
                                    {0.13333333333333333, 745022665u, 2453407289u},
                                    {0.14166666666666666, 2910421725u, 3449849861u},
                                    {0.15, 3793288758u, 256939460u},
                                    {0.19166666666666668, 3494584161u, 93247266u},
                                    {0.2, 1488530851u, 2003352685u},
                                    {0.20833333333333334, 2822065507u, 1479504775u},
                                    {0.23333333333333334, 3210429543u, 3822549187u},
                                    {0.24166666666666667, 213616027u, 1496184299u},
                                    {0.49166666666666664, 2783215735u, 3044792356u},
                                    {0.5, 2146124792u, 813450592u},
                                    {0.5083333333333333, 847622072u, 1882542250u},
                                    {0.6166666666666667, 3473283091u, 3307695694u},
                                    {0.625, 1048106919u, 909573270u},
                                    {0.6333333333333333, 2771533325u, 1052475569u},
                                    {0.75, 3112623160u, 1534141959u},
                                    {0.925, 3720606392u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json(" ")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 3227335237u, 1458625824u},
                                    {0.075, 2079635435u, 797064650u},
                                    {0.08333333333333333, 2540260127u, 975084383u},
                                    {0.09166666666666666, 4010482759u, 2703418059u},
                                    {0.13333333333333333, 2723439109u, 2453407289u},
                                    {0.14166666666666666, 2476399312u, 3449849861u},
                                    {0.15, 1806189214u, 256939460u},
                                    {0.19166666666666668, 3464268495u, 93247266u},
                                    {0.2, 3941848093u, 2003352685u},
                                    {0.20833333333333334, 2244968177u, 1479504775u},
                                    {0.23333333333333334, 14364567u, 3822549187u},
                                    {0.24166666666666667, 1234795151u, 1496184299u},
                                    {0.49166666666666664, 3826513158u, 3044792356u},
                                    {0.5, 2716755005u, 813450592u},
                                    {0.5083333333333333, 1427354298u, 1882542250u},
                                    {0.6166666666666667, 1303397062u, 3307695694u},
                                    {0.625, 1540988774u, 909573270u},
                                    {0.6333333333333333, 821726088u, 1052475569u},
                                    {0.75, 3049038794u, 1534141959u},
                                    {0.925, 983763756u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("hit")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 3819302979u, 1458625824u},
                                    {0.075, 2626653330u, 797064650u},
                                    {0.08333333333333333, 2551315840u, 975084383u},
                                    {0.09166666666666666, 3006087096u, 2703418059u},
                                    {0.13333333333333333, 2576830040u, 2453407289u},
                                    {0.14166666666666666, 2673526558u, 3449849861u},
                                    {0.15, 135918641u, 256939460u},
                                    {0.19166666666666668, 3678208639u, 93247266u},
                                    {0.2, 3350815251u, 2003352685u},
                                    {0.20833333333333334, 3391775564u, 1479504775u},
                                    {0.23333333333333334, 3695451718u, 3822549187u},
                                    {0.24166666666666667, 220214898u, 1496184299u},
                                    {0.49166666666666664, 766376444u, 3044792356u},
                                    {0.5, 1982932261u, 813450592u},
                                    {0.5083333333333333, 444361317u, 1882542250u},
                                    {0.6166666666666667, 3498879696u, 3307695694u},
                                    {0.625, 2538435980u, 909573270u},
                                    {0.6333333333333333, 2545825990u, 1052475569u},
                                    {0.75, 3627611416u, 1534141959u},
                                    {0.925, 2866213865u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(R"json("\u0131\u017f\u00df\u4e2d\ud83d\ude42")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 2718746878u, 1458625824u},
                                    {0.075, 4037372291u, 797064650u},
                                    {0.08333333333333333, 1600539484u, 975084383u},
                                    {0.09166666666666666, 141500690u, 2703418059u},
                                    {0.13333333333333333, 1998438373u, 2453407289u},
                                    {0.14166666666666666, 777526235u, 3449849861u},
                                    {0.15, 1501430393u, 256939460u},
                                    {0.19166666666666668, 3461788190u, 93247266u},
                                    {0.2, 421311027u, 2003352685u},
                                    {0.20833333333333334, 487206778u, 1479504775u},
                                    {0.23333333333333334, 1344711067u, 3822549187u},
                                    {0.24166666666666667, 592071357u, 1496184299u},
                                    {0.49166666666666664, 4192157601u, 3044792356u},
                                    {0.5, 3962360495u, 813450592u},
                                    {0.5083333333333333, 3711236760u, 1882542250u},
                                    {0.6166666666666667, 3808822667u, 3307695694u},
                                    {0.625, 817063314u, 909573270u},
                                    {0.6333333333333333, 1820134297u, 1052475569u},
                                    {0.75, 2782293717u, 1534141959u},
                                    {0.925, 4152952701u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        auto input_label =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 3108929342u, 2985606341u},
                                    {0.008333333333333333, 2153987820u, 1609797517u},
                                    {0.03333333333333333, 3334046528u, 3858272491u},
                                    {0.041666666666666664, 3781503483u, 1881924444u},
                                    {0.06666666666666667, 1874626445u, 1458625824u},
                                    {0.075, 917223389u, 797064650u},
                                    {0.08333333333333333, 4181842724u, 975084383u},
                                    {0.09166666666666666, 192891102u, 2703418059u},
                                    {0.13333333333333333, 3258918153u, 2453407289u},
                                    {0.14166666666666666, 281770247u, 3449849861u},
                                    {0.15, 3090162649u, 256939460u},
                                    {0.19166666666666668, 276586554u, 93247266u},
                                    {0.2, 1654154943u, 2003352685u},
                                    {0.20833333333333334, 2555510190u, 1479504775u},
                                    {0.23333333333333334, 112819863u, 3822549187u},
                                    {0.24166666666666667, 150203337u, 1496184299u},
                                    {0.49166666666666664, 2177850337u, 3044792356u},
                                    {0.5, 775170415u, 813450592u},
                                    {0.5083333333333333, 4057464664u, 1882542250u},
                                    {0.6166666666666667, 3400840679u, 3307695694u},
                                    {0.625, 2773712262u, 909573270u},
                                    {0.6333333333333333, 2079628493u, 1052475569u},
                                    {0.75, 2051813111u, 1534141959u},
                                    {0.925, 4165784185u, 1541669617u},
                                    {0.9833333333333333, 2318857976u, 2529960977u},
                                    {1.05, 2422530221u, 3332750914u},
                                    {1.0666666666666667, 1223283799u, 1454222248u},
                                    {1.1083333333333334, 2478793514u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_label = eve::Value::fromJson(
            R"json("\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42\ud83d\ude42")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
        constexpr Frame frames[] = {{0, 2297385983u, 2985606341u},
                                    {0.008333333333333333, 175680793u, 1609797517u},
                                    {0.03333333333333333, 3111541156u, 3858272491u},
                                    {0.041666666666666664, 1202605429u, 1881924444u},
                                    {0.06666666666666667, 534485104u, 1458625824u},
                                    {0.075, 3586612028u, 797064650u},
                                    {0.08333333333333333, 3383702122u, 975084383u},
                                    {0.09166666666666666, 3936773050u, 2703418059u},
                                    {0.13333333333333333, 1917299049u, 2453407289u},
                                    {0.14166666666666666, 952702775u, 3449849861u},
                                    {0.15, 570433911u, 256939460u},
                                    {0.19166666666666668, 3669851981u, 93247266u},
                                    {0.2, 1289198321u, 2003352685u},
                                    {0.20833333333333334, 1162295042u, 1479504775u},
                                    {0.23333333333333334, 4266962950u, 3822549187u},
                                    {0.24166666666666667, 2274447682u, 1496184299u},
                                    {0.49166666666666664, 3532081798u, 930977948u},
                                    {0.5, 2007520077u, 3786289236u},
                                    {0.5083333333333333, 119398778u, 516630449u},
                                    {0.6166666666666667, 1864563535u, 2979921452u},
                                    {0.625, 4010779258u, 4013969202u},
                                    {0.6333333333333333, 3815369729u, 1184634711u},
                                    {0.75, 4096524371u, 1109270965u},
                                    {0.925, 2468100818u, 1541669617u},
                                    {0.9833333333333333, 2536596036u, 2529960977u},
                                    {1.05, 3050841776u, 3332750914u},
                                    {1.0666666666666667, 759045900u, 1454222248u},
                                    {1.1083333333333334, 2249223554u, 632675931u},
                                    {1.3, 615204066u, 2003450227u},
                                    {1.5, 2202501573u, 2202501573u},
                                    {2, 2202501573u, 2202501573u},
                                    {5, 2202501573u, 2202501573u}};
        PixelFx         fx(PixelFxKind::CritNumber);
        REQUIRE(fx.isSimulated());
        auto value = fx.configurationValue();
        auto input_text =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_text);
        value.find("options")->set("text", input_text.value());
        auto input_label =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input_label);
        value.find("options")->set("label", input_label.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value);
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
TEST_CASE("stylize.pixelfx.ui.impact.healthBarHitPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2363315194u, 2142084051u},
                                    {0.041666666666666664, 1097969190u, 2142084051u},
                                    {0.06666666666666667, 3608351394u, 2142084051u},
                                    {0.075, 2780643210u, 2142084051u},
                                    {0.08333333333333333, 3608351394u, 2142084051u},
                                    {0.09166666666666666, 3608351394u, 2142084051u},
                                    {0.13333333333333333, 905573326u, 3075172533u},
                                    {0.14166666666666666, 905573326u, 3075172533u},
                                    {0.15, 905573326u, 3075172533u},
                                    {0.19166666666666668, 905573326u, 3075172533u},
                                    {0.2, 905573326u, 3075172533u},
                                    {0.20833333333333334, 1956954013u, 1385560358u},
                                    {0.23333333333333334, 529160322u, 1699704219u},
                                    {0.24166666666666667, 1861629888u, 95971379u},
                                    {0.49166666666666664, 3290064499u, 4085406797u},
                                    {0.5, 2868705735u, 4085406797u},
                                    {0.5083333333333333, 1454836519u, 4085406797u},
                                    {0.6166666666666667, 2351412690u, 4085406797u},
                                    {0.625, 2365128700u, 520622393u},
                                    {0.6333333333333333, 3538758174u, 1382302191u},
                                    {0.75, 3063325420u, 2970070001u},
                                    {0.925, 568317854u, 3919442993u},
                                    {0.9833333333333333, 773113072u, 2445314296u},
                                    {1.05, 975821509u, 3208521824u},
                                    {1.0666666666666667, 1464135663u, 2302473071u},
                                    {1.1083333333333334, 4251104059u, 1048486457u},
                                    {1.3, 1414284292u, 4085406797u},
                                    {1.5, 1414284292u, 4085406797u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2993204586u, 1745438619u},
                                    {0.041666666666666664, 424025494u, 1745438619u},
                                    {0.06666666666666667, 1406412002u, 1745438619u},
                                    {0.075, 4049852810u, 1745438619u},
                                    {0.08333333333333333, 1406412002u, 1745438619u},
                                    {0.09166666666666666, 1406412002u, 1745438619u},
                                    {0.13333333333333333, 2018958720u, 1024217421u},
                                    {0.14166666666666666, 2018958720u, 1024217421u},
                                    {0.15, 2018958720u, 1024217421u},
                                    {0.19166666666666668, 2018958720u, 1024217421u},
                                    {0.2, 2018958720u, 1024217421u},
                                    {0.20833333333333334, 2110977001u, 94343501u},
                                    {0.23333333333333334, 723948090u, 3137817386u},
                                    {0.24166666666666667, 2784865350u, 436029513u},
                                    {0.49166666666666664, 1925535372u, 3476639753u},
                                    {0.5, 1601023163u, 3476639753u},
                                    {0.5083333333333333, 561996230u, 3476639753u},
                                    {0.6166666666666667, 484029254u, 3476639753u},
                                    {0.625, 3888351600u, 643334669u},
                                    {0.6333333333333333, 578110702u, 3443003497u},
                                    {0.75, 4054548124u, 1834938895u},
                                    {0.925, 219566282u, 2605388551u},
                                    {0.9833333333333333, 1729016777u, 2504296844u},
                                    {1.05, 1945739462u, 2457049223u},
                                    {1.0666666666666667, 2703397728u, 3554950561u},
                                    {1.1083333333333334, 2099483401u, 714127092u},
                                    {1.3, 2165009805u, 3476639753u},
                                    {1.5, 2165009805u, 3476639753u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_from = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0.125)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2363315194u, 2142084051u},
                                    {0.041666666666666664, 1097969190u, 2142084051u},
                                    {0.06666666666666667, 3608351394u, 2142084051u},
                                    {0.075, 2780643210u, 2142084051u},
                                    {0.08333333333333333, 3608351394u, 2142084051u},
                                    {0.09166666666666666, 3608351394u, 2142084051u},
                                    {0.13333333333333333, 905573326u, 3075172533u},
                                    {0.14166666666666666, 905573326u, 3075172533u},
                                    {0.15, 905573326u, 3075172533u},
                                    {0.19166666666666668, 905573326u, 3075172533u},
                                    {0.2, 905573326u, 3075172533u},
                                    {0.20833333333333334, 589623362u, 3459467430u},
                                    {0.23333333333333334, 3281849318u, 3267087060u},
                                    {0.24166666666666667, 3556785632u, 3434223974u},
                                    {0.49166666666666664, 3529382556u, 4085406797u},
                                    {0.5, 3888928048u, 4085406797u},
                                    {0.5083333333333333, 3504521147u, 4085406797u},
                                    {0.6166666666666667, 2830937244u, 4085406797u},
                                    {0.625, 3966900508u, 1001214225u},
                                    {0.6333333333333333, 4036993116u, 2272012221u},
                                    {0.75, 1653784522u, 2603855587u},
                                    {0.925, 2105290543u, 1441284692u},
                                    {0.9833333333333333, 2864144923u, 3623324203u},
                                    {1.05, 2378782220u, 2520205836u},
                                    {1.0666666666666667, 2545419364u, 3904003664u},
                                    {1.1083333333333334, 1816250140u, 1387322608u},
                                    {1.3, 1414284292u, 4085406797u},
                                    {1.5, 1414284292u, 4085406797u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2363315194u, 2142084051u},
                                    {0.041666666666666664, 1097969190u, 2142084051u},
                                    {0.06666666666666667, 3608351394u, 2142084051u},
                                    {0.075, 2780643210u, 2142084051u},
                                    {0.08333333333333333, 3608351394u, 2142084051u},
                                    {0.09166666666666666, 3608351394u, 2142084051u},
                                    {0.13333333333333333, 905573326u, 3075172533u},
                                    {0.14166666666666666, 905573326u, 3075172533u},
                                    {0.15, 905573326u, 3075172533u},
                                    {0.19166666666666668, 905573326u, 3075172533u},
                                    {0.2, 905573326u, 3075172533u},
                                    {0.20833333333333334, 2331955890u, 1800178752u},
                                    {0.23333333333333334, 684914178u, 70663769u},
                                    {0.24166666666666667, 3425154146u, 76441545u},
                                    {0.49166666666666664, 2917222408u, 4085406797u},
                                    {0.5, 3762904117u, 4085406797u},
                                    {0.5083333333333333, 3755738609u, 4085406797u},
                                    {0.6166666666666667, 3138998982u, 4085406797u},
                                    {0.625, 384267264u, 36849691u},
                                    {0.6333333333333333, 3687261798u, 3499345954u},
                                    {0.75, 1603606944u, 2813501310u},
                                    {0.925, 3751658163u, 1217717397u},
                                    {0.9833333333333333, 432536213u, 2613194474u},
                                    {1.05, 1558760261u, 437012397u},
                                    {1.0666666666666667, 3980954011u, 2730571959u},
                                    {1.1083333333333334, 3255803017u, 790067672u},
                                    {1.3, 1414284292u, 4085406797u},
                                    {1.5, 1414284292u, 4085406797u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 692893284u, 3951704678u},
                                    {0.041666666666666664, 358243728u, 3951704678u},
                                    {0.06666666666666667, 3652495008u, 3951704678u},
                                    {0.075, 2317778576u, 3951704678u},
                                    {0.08333333333333333, 3652495008u, 3951704678u},
                                    {0.09166666666666666, 3652495008u, 3951704678u},
                                    {0.13333333333333333, 3311655840u, 4250960104u},
                                    {0.14166666666666666, 3311655840u, 4250960104u},
                                    {0.15, 3311655840u, 4250960104u},
                                    {0.19166666666666668, 3311655840u, 4250960104u},
                                    {0.2, 3311655840u, 4250960104u},
                                    {0.20833333333333334, 222960387u, 2801794718u},
                                    {0.23333333333333334, 3568582645u, 227784836u},
                                    {0.24166666666666667, 1779203839u, 133293060u},
                                    {0.49166666666666664, 1518102589u, 2272840728u},
                                    {0.5, 2813220415u, 2272840728u},
                                    {0.5083333333333333, 3646703717u, 2272840728u},
                                    {0.6166666666666667, 351538781u, 2272840728u},
                                    {0.625, 3154567690u, 4144504078u},
                                    {0.6333333333333333, 2003088864u, 2175470512u},
                                    {0.75, 2798593888u, 3575754213u},
                                    {0.925, 3815135617u, 2619301947u},
                                    {0.9833333333333333, 4060751057u, 600262041u},
                                    {1.05, 2937852478u, 2332334188u},
                                    {1.0666666666666667, 2603808220u, 2369352551u},
                                    {1.1083333333333334, 4056966276u, 555434343u},
                                    {1.3, 817817926u, 2272840728u},
                                    {1.5, 817817926u, 2272840728u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 4114923256u, 2893337227u},
                                    {0.041666666666666664, 1040509836u, 2893337227u},
                                    {0.06666666666666667, 3879456248u, 2893337227u},
                                    {0.075, 2277107376u, 2893337227u},
                                    {0.08333333333333333, 3879456248u, 2893337227u},
                                    {0.09166666666666666, 3879456248u, 2893337227u},
                                    {0.13333333333333333, 814527825u, 2273652607u},
                                    {0.14166666666666666, 814527825u, 2273652607u},
                                    {0.15, 814527825u, 2273652607u},
                                    {0.19166666666666668, 814527825u, 2273652607u},
                                    {0.2, 814527825u, 2273652607u},
                                    {0.20833333333333334, 800316169u, 3463742969u},
                                    {0.23333333333333334, 2868061710u, 1523141743u},
                                    {0.24166666666666667, 2607831856u, 3559950126u},
                                    {0.49166666666666664, 2926830540u, 449109863u},
                                    {0.5, 3889327318u, 449109863u},
                                    {0.5083333333333333, 2543822389u, 449109863u},
                                    {0.6166666666666667, 3098063172u, 449109863u},
                                    {0.625, 4263078657u, 1778079717u},
                                    {0.6333333333333333, 1494895831u, 2103278962u},
                                    {0.75, 2576942277u, 102012092u},
                                    {0.925, 1356210162u, 4211921719u},
                                    {0.9833333333333333, 954516579u, 240221968u},
                                    {1.05, 1674085608u, 881012181u},
                                    {1.0666666666666667, 1297543708u, 2287808789u},
                                    {1.1083333333333334, 1154411928u, 1523163183u},
                                    {1.3, 1143791343u, 449109863u},
                                    {1.5, 1143791343u, 449109863u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 1466272079u, 427022623u},
                                    {0.041666666666666664, 2442834259u, 427022623u},
                                    {0.06666666666666667, 3906264319u, 427022623u},
                                    {0.075, 4086089727u, 427022623u},
                                    {0.08333333333333333, 3906264319u, 427022623u},
                                    {0.09166666666666666, 3906264319u, 427022623u},
                                    {0.13333333333333333, 4008357155u, 1372721529u},
                                    {0.14166666666666666, 4008357155u, 1372721529u},
                                    {0.15, 4008357155u, 1372721529u},
                                    {0.19166666666666668, 4008357155u, 1372721529u},
                                    {0.2, 4008357155u, 1372721529u},
                                    {0.20833333333333334, 2210272743u, 2428241401u},
                                    {0.23333333333333334, 409241668u, 3099361570u},
                                    {0.24166666666666667, 3688256458u, 1019634704u},
                                    {0.49166666666666664, 1038321367u, 1372721529u},
                                    {0.5, 878150459u, 1372721529u},
                                    {0.5083333333333333, 53888134u, 1372721529u},
                                    {0.6166666666666667, 3179731885u, 1372721529u},
                                    {0.625, 1265283049u, 2745001668u},
                                    {0.6333333333333333, 2775125084u, 1976976078u},
                                    {0.75, 774880990u, 550738933u},
                                    {0.925, 1712919008u, 580114866u},
                                    {0.9833333333333333, 2302187065u, 880035604u},
                                    {1.05, 920049756u, 450497558u},
                                    {1.0666666666666667, 1398591752u, 538631891u},
                                    {1.1083333333333334, 2297077109u, 1411050460u},
                                    {1.3, 1849973788u, 1372721529u},
                                    {1.5, 1849973788u, 1372721529u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2605457126u, 1917053903u},
                                    {0.041666666666666664, 2485823466u, 1917053903u},
                                    {0.06666666666666667, 18114830u, 1917053903u},
                                    {0.075, 2018294078u, 1917053903u},
                                    {0.08333333333333333, 18114830u, 1917053903u},
                                    {0.09166666666666666, 18114830u, 1917053903u},
                                    {0.13333333333333333, 1843356130u, 3811331917u},
                                    {0.14166666666666666, 1843356130u, 3811331917u},
                                    {0.15, 1843356130u, 3811331917u},
                                    {0.19166666666666668, 1843356130u, 3811331917u},
                                    {0.2, 1843356130u, 3811331917u},
                                    {0.20833333333333334, 4122900283u, 466254266u},
                                    {0.23333333333333334, 2536283985u, 4121602194u},
                                    {0.24166666666666667, 3320188548u, 2643320653u},
                                    {0.49166666666666664, 3101379443u, 1372721529u},
                                    {0.5, 3859822258u, 1372721529u},
                                    {0.5083333333333333, 1934789926u, 1372721529u},
                                    {0.6166666666666667, 3566214507u, 1372721529u},
                                    {0.625, 1674601618u, 2870763228u},
                                    {0.6333333333333333, 3480826962u, 4120044186u},
                                    {0.75, 3932741428u, 3365128169u},
                                    {0.925, 2268239864u, 1019249442u},
                                    {0.9833333333333333, 2575757197u, 4231103690u},
                                    {1.05, 699534453u, 2281326255u},
                                    {1.0666666666666667, 345410853u, 1936558624u},
                                    {1.1083333333333334, 1727316943u, 3930635517u},
                                    {1.3, 1849973788u, 1372721529u},
                                    {1.5, 1849973788u, 1372721529u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2605457126u, 1917053903u},
                                    {0.041666666666666664, 2485823466u, 1917053903u},
                                    {0.06666666666666667, 18114830u, 1917053903u},
                                    {0.075, 2018294078u, 1917053903u},
                                    {0.08333333333333333, 18114830u, 1917053903u},
                                    {0.09166666666666666, 18114830u, 1917053903u},
                                    {0.13333333333333333, 1843356130u, 3811331917u},
                                    {0.14166666666666666, 1843356130u, 3811331917u},
                                    {0.15, 1843356130u, 3811331917u},
                                    {0.19166666666666668, 1843356130u, 3811331917u},
                                    {0.2, 1843356130u, 3811331917u},
                                    {0.20833333333333334, 118464788u, 131489795u},
                                    {0.23333333333333334, 1785661656u, 2368233008u},
                                    {0.24166666666666667, 1154038960u, 497484533u},
                                    {0.49166666666666664, 1351025841u, 3811331917u},
                                    {0.5, 1967813494u, 3811331917u},
                                    {0.5083333333333333, 1123995659u, 3811331917u},
                                    {0.6166666666666667, 2457451170u, 3811331917u},
                                    {0.625, 2014617588u, 2475106268u},
                                    {0.6333333333333333, 3307440492u, 2266315846u},
                                    {0.75, 42748709u, 486303672u},
                                    {0.925, 2519467878u, 3730885275u},
                                    {0.9833333333333333, 3916664690u, 3100418259u},
                                    {1.05, 2486506944u, 1236392980u},
                                    {1.0666666666666667, 4113507978u, 456977312u},
                                    {1.1083333333333334, 2356231922u, 2121889126u},
                                    {1.3, 2121281617u, 3811331917u},
                                    {1.5, 2121281617u, 3811331917u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 346454623u, 198593403u},
                                    {0.041666666666666664, 712975987u, 198593403u},
                                    {0.06666666666666667, 1820348607u, 198593403u},
                                    {0.075, 3280998047u, 198593403u},
                                    {0.08333333333333333, 1820348607u, 198593403u},
                                    {0.09166666666666666, 1820348607u, 198593403u},
                                    {0.13333333333333333, 1454066771u, 2526472577u},
                                    {0.14166666666666666, 1454066771u, 2526472577u},
                                    {0.15, 1454066771u, 2526472577u},
                                    {0.19166666666666668, 1454066771u, 2526472577u},
                                    {0.2, 1454066771u, 2526472577u},
                                    {0.20833333333333334, 236343377u, 931706797u},
                                    {0.23333333333333334, 465014664u, 3734761315u},
                                    {0.24166666666666667, 623621725u, 3772841040u},
                                    {0.49166666666666664, 868619338u, 2526472577u},
                                    {0.5, 2975619397u, 2526472577u},
                                    {0.5083333333333333, 2909355353u, 2526472577u},
                                    {0.6166666666666667, 1561145545u, 2526472577u},
                                    {0.625, 2043809179u, 3480968292u},
                                    {0.6333333333333333, 1924895481u, 203220950u},
                                    {0.75, 450064584u, 1151983382u},
                                    {0.925, 829709894u, 2465809911u},
                                    {0.9833333333333333, 940798171u, 3773767101u},
                                    {1.05, 3189237094u, 3587410980u},
                                    {1.0666666666666667, 3800200298u, 2259800347u},
                                    {1.1083333333333334, 331080133u, 891223264u},
                                    {1.3, 3996081620u, 2526472577u},
                                    {1.5, 3996081620u, 2526472577u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.5)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0.5)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 1466272079u, 427022623u},
                                    {0.041666666666666664, 2442834259u, 427022623u},
                                    {0.06666666666666667, 3906264319u, 427022623u},
                                    {0.075, 4086089727u, 427022623u},
                                    {0.08333333333333333, 3906264319u, 427022623u},
                                    {0.09166666666666666, 3906264319u, 427022623u},
                                    {0.13333333333333333, 4008357155u, 1372721529u},
                                    {0.14166666666666666, 4008357155u, 1372721529u},
                                    {0.15, 4008357155u, 1372721529u},
                                    {0.19166666666666668, 4008357155u, 1372721529u},
                                    {0.2, 4008357155u, 1372721529u},
                                    {0.20833333333333334, 2210272743u, 2428241401u},
                                    {0.23333333333333334, 409241668u, 3099361570u},
                                    {0.24166666666666667, 3688256458u, 1019634704u},
                                    {0.49166666666666664, 1038321367u, 1372721529u},
                                    {0.5, 878150459u, 1372721529u},
                                    {0.5083333333333333, 53888134u, 1372721529u},
                                    {0.6166666666666667, 3179731885u, 1372721529u},
                                    {0.625, 1265283049u, 2745001668u},
                                    {0.6333333333333333, 2775125084u, 1976976078u},
                                    {0.75, 774880990u, 550738933u},
                                    {0.925, 1712919008u, 580114866u},
                                    {0.9833333333333333, 2302187065u, 880035604u},
                                    {1.05, 920049756u, 450497558u},
                                    {1.0666666666666667, 1398591752u, 538631891u},
                                    {1.1083333333333334, 2297077109u, 1411050460u},
                                    {1.3, 1849973788u, 1372721529u},
                                    {1.5, 1849973788u, 1372721529u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2772275090u, 2982456911u},
                                    {0.041666666666666664, 959325734u, 2982456911u},
                                    {0.06666666666666667, 3745150146u, 2982456911u},
                                    {0.075, 2912238986u, 2982456911u},
                                    {0.08333333333333333, 3745150146u, 2982456911u},
                                    {0.09166666666666666, 3745150146u, 2982456911u},
                                    {0.13333333333333333, 583323278u, 2619751965u},
                                    {0.14166666666666666, 583323278u, 2619751965u},
                                    {0.15, 583323278u, 2619751965u},
                                    {0.19166666666666668, 583323278u, 2619751965u},
                                    {0.2, 583323278u, 2619751965u},
                                    {0.20833333333333334, 3108554980u, 792520882u},
                                    {0.23333333333333334, 3937004261u, 4155395409u},
                                    {0.24166666666666667, 943575232u, 1186768215u},
                                    {0.49166666666666664, 89416819u, 2619751965u},
                                    {0.5, 917144954u, 2619751965u},
                                    {0.5083333333333333, 3240681674u, 2619751965u},
                                    {0.6166666666666667, 463157804u, 2619751965u},
                                    {0.625, 1891363072u, 655238126u},
                                    {0.6333333333333333, 2002682855u, 2700121591u},
                                    {0.75, 2442051162u, 1082147465u},
                                    {0.925, 1706295487u, 3180630804u},
                                    {0.9833333333333333, 670386341u, 208556988u},
                                    {1.05, 539621875u, 3925690845u},
                                    {1.0666666666666667, 684621608u, 361333305u},
                                    {1.1083333333333334, 3890800020u, 1247833648u},
                                    {1.3, 3937556985u, 2619751965u},
                                    {1.5, 3937556985u, 2619751965u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.2)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0.8)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2363315194u, 2142084051u},
                                    {0.041666666666666664, 1097969190u, 2142084051u},
                                    {0.06666666666666667, 3608351394u, 2142084051u},
                                    {0.075, 2780643210u, 2142084051u},
                                    {0.08333333333333333, 3608351394u, 2142084051u},
                                    {0.09166666666666666, 3608351394u, 2142084051u},
                                    {0.13333333333333333, 905573326u, 3075172533u},
                                    {0.14166666666666666, 905573326u, 3075172533u},
                                    {0.15, 905573326u, 3075172533u},
                                    {0.19166666666666668, 905573326u, 3075172533u},
                                    {0.2, 905573326u, 3075172533u},
                                    {0.20833333333333334, 2093069519u, 3643319127u},
                                    {0.23333333333333334, 2814914706u, 3121016682u},
                                    {0.24166666666666667, 733855966u, 1799690009u},
                                    {0.49166666666666664, 3913809982u, 2619751965u},
                                    {0.5, 1468773418u, 2619751965u},
                                    {0.5083333333333333, 2058365564u, 2619751965u},
                                    {0.6166666666666667, 1626685800u, 2619751965u},
                                    {0.625, 1650481309u, 1399681197u},
                                    {0.6333333333333333, 3163614813u, 3341573434u},
                                    {0.75, 1172377647u, 1246217848u},
                                    {0.925, 189259187u, 2865935026u},
                                    {0.9833333333333333, 1377350854u, 426835819u},
                                    {1.05, 4135022295u, 308732313u},
                                    {1.0666666666666667, 2177194499u, 2803434981u},
                                    {1.1083333333333334, 1053367797u, 2915927365u},
                                    {1.3, 3937556985u, 2619751965u},
                                    {1.5, 3937556985u, 2619751965u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.8)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0.2)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 3603167199u, 2317083529u},
                                    {0.041666666666666664, 686576787u, 2317083529u},
                                    {0.06666666666666667, 3086889111u, 2317083529u},
                                    {0.075, 3450996671u, 2317083529u},
                                    {0.08333333333333333, 3086889111u, 2317083529u},
                                    {0.09166666666666666, 3086889111u, 2317083529u},
                                    {0.13333333333333333, 29203451u, 1101321013u},
                                    {0.14166666666666666, 29203451u, 1101321013u},
                                    {0.15, 29203451u, 1101321013u},
                                    {0.19166666666666668, 29203451u, 1101321013u},
                                    {0.2, 29203451u, 1101321013u},
                                    {0.20833333333333334, 1266295629u, 2091370983u},
                                    {0.23333333333333334, 2605629993u, 798018276u},
                                    {0.24166666666666667, 1644791249u, 123655102u},
                                    {0.49166666666666664, 385364094u, 1372721529u},
                                    {0.5, 1971514752u, 1372721529u},
                                    {0.5083333333333333, 3817019469u, 1372721529u},
                                    {0.6166666666666667, 2252264274u, 1372721529u},
                                    {0.625, 1066516544u, 193029934u},
                                    {0.6333333333333333, 3612605691u, 3372000527u},
                                    {0.75, 3389517280u, 2351291017u},
                                    {0.925, 2029946153u, 2998609797u},
                                    {0.9833333333333333, 2162222664u, 2557106514u},
                                    {1.05, 2634850391u, 374766056u},
                                    {1.0666666666666667, 2410403641u, 2781862324u},
                                    {1.1083333333333334, 2297077109u, 3585840629u},
                                    {1.3, 1849973788u, 1372721529u},
                                    {1.5, 1849973788u, 1372721529u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.01)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 2605457126u, 1917053903u},
                                    {0.041666666666666664, 2485823466u, 1917053903u},
                                    {0.06666666666666667, 18114830u, 1917053903u},
                                    {0.075, 2018294078u, 1917053903u},
                                    {0.08333333333333333, 18114830u, 1917053903u},
                                    {0.09166666666666666, 18114830u, 1917053903u},
                                    {0.13333333333333333, 1843356130u, 3811331917u},
                                    {0.14166666666666666, 1843356130u, 3811331917u},
                                    {0.15, 1843356130u, 3811331917u},
                                    {0.19166666666666668, 1843356130u, 3811331917u},
                                    {0.2, 1843356130u, 3811331917u},
                                    {0.20833333333333334, 4122900283u, 2269169589u},
                                    {0.23333333333333334, 2536283985u, 4121602194u},
                                    {0.24166666666666667, 3320188548u, 2643320653u},
                                    {0.49166666666666664, 3101379443u, 1372721529u},
                                    {0.5, 3859822258u, 1372721529u},
                                    {0.5083333333333333, 1934789926u, 1372721529u},
                                    {0.6166666666666667, 3566214507u, 1372721529u},
                                    {0.625, 1674601618u, 2870763228u},
                                    {0.6333333333333333, 3480826962u, 140378550u},
                                    {0.75, 3932741428u, 3365128169u},
                                    {0.925, 2268239864u, 1019249442u},
                                    {0.9833333333333333, 2575757197u, 4231103690u},
                                    {1.05, 699534453u, 2281326255u},
                                    {1.0666666666666667, 345410853u, 1936558624u},
                                    {1.1083333333333334, 1727316943u, 3930635517u},
                                    {1.3, 1849973788u, 1372721529u},
                                    {1.5, 1849973788u, 1372721529u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.999999)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(1e-06)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
        constexpr Frame frames[] = {{0, 727522757u, 727522757u},
                                    {0.008333333333333333, 2426765741u, 727522757u},
                                    {0.03333333333333333, 3813383946u, 1740989623u},
                                    {0.041666666666666664, 566997470u, 1740989623u},
                                    {0.06666666666666667, 1883800442u, 1740989623u},
                                    {0.075, 4280969250u, 1740989623u},
                                    {0.08333333333333333, 1883800442u, 1740989623u},
                                    {0.09166666666666666, 1883800442u, 1740989623u},
                                    {0.13333333333333333, 2942638102u, 1767436929u},
                                    {0.14166666666666666, 2942638102u, 1767436929u},
                                    {0.15, 2942638102u, 1767436929u},
                                    {0.19166666666666668, 2942638102u, 1767436929u},
                                    {0.2, 2942638102u, 1767436929u},
                                    {0.20833333333333334, 3927506162u, 3655917730u},
                                    {0.23333333333333334, 2943858183u, 3437710647u},
                                    {0.24166666666666667, 2846607495u, 3546867497u},
                                    {0.49166666666666664, 249306330u, 1504561125u},
                                    {0.5, 3217655512u, 1504561125u},
                                    {0.5083333333333333, 2401965735u, 1504561125u},
                                    {0.6166666666666667, 1549319140u, 1504561125u},
                                    {0.625, 3448200232u, 2423720370u},
                                    {0.6333333333333333, 3160249427u, 302218410u},
                                    {0.75, 3566045990u, 706458836u},
                                    {0.925, 2350685164u, 1578175297u},
                                    {0.9833333333333333, 3168583826u, 3896106240u},
                                    {1.05, 3507875533u, 4167892586u},
                                    {1.0666666666666667, 1115541014u, 3556055615u},
                                    {1.1083333333333334, 3149842227u, 1043722370u},
                                    {1.3, 3975005276u, 1504561125u},
                                    {1.5, 3975005276u, 1504561125u},
                                    {2, 727522757u, 727522757u},
                                    {5, 727522757u, 727522757u}};
        PixelFx         fx(PixelFxKind::HealthBarHit);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_from = eve::Value::fromJson(R"json(0.51)json");
        REQUIRE(input_from);
        value.find("options")->set("from", input_from.value());
        auto input_to = eve::Value::fromJson(R"json(0.49)json");
        REQUIRE(input_to);
        value.find("options")->set("to", input_to.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value);
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
TEST_CASE("stylize.pixelfx.ui.impact.heartLostPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3582112072u, 374740157u},
                                    {0.008333333333333333, 3582112072u, 357683101u},
                                    {0.03333333333333333, 132851662u, 3736758317u},
                                    {0.041666666666666664, 132851662u, 2863606509u},
                                    {0.06666666666666667, 132851662u, 4268540451u},
                                    {0.075, 132851662u, 4268540451u},
                                    {0.08333333333333333, 132851662u, 2424010763u},
                                    {0.09166666666666666, 132851662u, 1185531207u},
                                    {0.13333333333333333, 2096216278u, 4266194229u},
                                    {0.14166666666666666, 3411062209u, 516160775u},
                                    {0.15, 4013201703u, 2851151044u},
                                    {0.19166666666666668, 487758399u, 2480193585u},
                                    {0.2, 2863105827u, 2547758612u},
                                    {0.20833333333333334, 509843347u, 3484202164u},
                                    {0.23333333333333334, 2377584164u, 321884360u},
                                    {0.24166666666666667, 696824457u, 1528485490u},
                                    {0.49166666666666664, 4145457342u, 374740157u},
                                    {0.5, 468288767u, 1809208234u},
                                    {0.5083333333333333, 3780792493u, 1455374645u},
                                    {0.6166666666666667, 2868772653u, 3161442310u},
                                    {0.625, 4196304186u, 715750545u},
                                    {0.6333333333333333, 3804833361u, 3061560014u},
                                    {0.75, 953759966u, 3394091668u},
                                    {0.925, 4138350103u, 673467047u},
                                    {0.9833333333333333, 1781869609u, 472892155u},
                                    {1.05, 2692442137u, 2496979476u},
                                    {1.0666666666666667, 3659457169u, 3466813229u},
                                    {1.1083333333333334, 3636262053u, 3282944979u},
                                    {1.3, 2070927215u, 766209477u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
        constexpr Frame frames[] = {{0, 390127830u, 1766569789u},
                                    {0.008333333333333333, 390127830u, 3671418989u},
                                    {0.03333333333333333, 3327919707u, 1460233547u},
                                    {0.041666666666666664, 3327919707u, 305996631u},
                                    {0.06666666666666667, 3327919707u, 2167633249u},
                                    {0.075, 3327919707u, 405983399u},
                                    {0.08333333333333333, 3327919707u, 1145638561u},
                                    {0.09166666666666666, 3327919707u, 3704745635u},
                                    {0.13333333333333333, 1779848651u, 100417453u},
                                    {0.14166666666666666, 2977795276u, 2430102981u},
                                    {0.15, 2852863481u, 2474248746u},
                                    {0.19166666666666668, 2409703072u, 1698640634u},
                                    {0.2, 2522842877u, 189005146u},
                                    {0.20833333333333334, 675975668u, 310149950u},
                                    {0.23333333333333334, 3549635074u, 1525190876u},
                                    {0.24166666666666667, 3745117354u, 648013310u},
                                    {0.49166666666666664, 3761920965u, 1766569789u},
                                    {0.5, 3224133945u, 2684399595u},
                                    {0.5083333333333333, 2394491015u, 3993984294u},
                                    {0.6166666666666667, 2081790657u, 200875592u},
                                    {0.625, 4191650373u, 96676229u},
                                    {0.6333333333333333, 1890445998u, 994029428u},
                                    {0.75, 3495225712u, 4070310118u},
                                    {0.925, 3646491937u, 1319356350u},
                                    {0.9833333333333333, 2066085768u, 4245571775u},
                                    {1.05, 1732056992u, 3153626512u},
                                    {1.0666666666666667, 3394018333u, 3855271270u},
                                    {1.1083333333333334, 152603220u, 2399176931u},
                                    {1.3, 2070927215u, 766209477u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
        constexpr Frame frames[] = {{0, 3582112072u, 374740157u},
                                    {0.008333333333333333, 3582112072u, 357683101u},
                                    {0.03333333333333333, 132851662u, 3736758317u},
                                    {0.041666666666666664, 132851662u, 2863606509u},
                                    {0.06666666666666667, 132851662u, 4268540451u},
                                    {0.075, 132851662u, 4268540451u},
                                    {0.08333333333333333, 132851662u, 2424010763u},
                                    {0.09166666666666666, 132851662u, 1185531207u},
                                    {0.13333333333333333, 2096216278u, 4266194229u},
                                    {0.14166666666666666, 3758203533u, 3645269555u},
                                    {0.15, 2314904315u, 3623079360u},
                                    {0.19166666666666668, 2468356693u, 1370406383u},
                                    {0.2, 3447482044u, 1395101036u},
                                    {0.20833333333333334, 1060468804u, 3162037914u},
                                    {0.23333333333333334, 1058458172u, 1958247376u},
                                    {0.24166666666666667, 1228254897u, 2850981716u},
                                    {0.49166666666666664, 4145457342u, 374740157u},
                                    {0.5, 4061954875u, 2174769296u},
                                    {0.5083333333333333, 2925134632u, 3503752187u},
                                    {0.6166666666666667, 1019486846u, 912147538u},
                                    {0.625, 167745007u, 1478030372u},
                                    {0.6333333333333333, 2815451119u, 546169387u},
                                    {0.75, 3648620099u, 3053133919u},
                                    {0.925, 4208996828u, 2674477342u},
                                    {0.9833333333333333, 3985226864u, 2420142462u},
                                    {1.05, 1894259097u, 1218014194u},
                                    {1.0666666666666667, 3740212805u, 1692681362u},
                                    {1.1083333333333334, 1762448587u, 1680651363u},
                                    {1.3, 2070927215u, 766209477u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
        constexpr Frame frames[] = {{0, 3582112072u, 374740157u},
                                    {0.008333333333333333, 3582112072u, 357683101u},
                                    {0.03333333333333333, 132851662u, 3736758317u},
                                    {0.041666666666666664, 132851662u, 2863606509u},
                                    {0.06666666666666667, 132851662u, 4268540451u},
                                    {0.075, 132851662u, 4268540451u},
                                    {0.08333333333333333, 132851662u, 2424010763u},
                                    {0.09166666666666666, 132851662u, 1185531207u},
                                    {0.13333333333333333, 2096216278u, 4266194229u},
                                    {0.14166666666666666, 3973733227u, 2351068397u},
                                    {0.15, 1107212401u, 3587086921u},
                                    {0.19166666666666668, 3624316918u, 2177272323u},
                                    {0.2, 2378993642u, 2515248836u},
                                    {0.20833333333333334, 2246660898u, 1650727787u},
                                    {0.23333333333333334, 2629810931u, 1662745611u},
                                    {0.24166666666666667, 4059842152u, 3384006740u},
                                    {0.49166666666666664, 4145457342u, 374740157u},
                                    {0.5, 1854821004u, 1936876085u},
                                    {0.5083333333333333, 3660269709u, 2030277627u},
                                    {0.6166666666666667, 4162259357u, 2792886446u},
                                    {0.625, 1977430277u, 2352566270u},
                                    {0.6333333333333333, 3153775569u, 3474160956u},
                                    {0.75, 900655908u, 8132603u},
                                    {0.925, 1300121771u, 3690451928u},
                                    {0.9833333333333333, 3252572754u, 1284851434u},
                                    {1.05, 3637156694u, 3007277924u},
                                    {1.0666666666666667, 3183683487u, 1292511199u},
                                    {1.1083333333333334, 129375665u, 1275345112u},
                                    {1.3, 3711172997u, 3657432249u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
        constexpr Frame frames[] = {{0, 2792011061u, 3630817925u},
                                    {0.008333333333333333, 2792011061u, 2634809109u},
                                    {0.03333333333333333, 2792011061u, 1324886499u},
                                    {0.041666666666666664, 2792011061u, 3406355001u},
                                    {0.06666666666666667, 2792011061u, 3424125329u},
                                    {0.075, 2792011061u, 3424125329u},
                                    {0.08333333333333333, 2792011061u, 3968073039u},
                                    {0.09166666666666666, 2792011061u, 1824104103u},
                                    {0.13333333333333333, 2792011061u, 776475205u},
                                    {0.14166666666666666, 2944531317u, 3896921463u},
                                    {0.15, 2829906497u, 2612102608u},
                                    {0.19166666666666668, 171380041u, 224738197u},
                                    {0.2, 865740033u, 890618750u},
                                    {0.20833333333333334, 4220126837u, 2001519490u},
                                    {0.23333333333333334, 2268246461u, 921409208u},
                                    {0.24166666666666667, 2543097937u, 2430066181u},
                                    {0.49166666666666664, 2792011061u, 3630817925u},
                                    {0.5, 2194695039u, 1110707287u},
                                    {0.5083333333333333, 3083969476u, 2011726265u},
                                    {0.6166666666666667, 489172844u, 2728479045u},
                                    {0.625, 1128486176u, 862428645u},
                                    {0.6333333333333333, 3054172500u, 3786577402u},
                                    {0.75, 2523578091u, 1035487684u},
                                    {0.925, 1360087363u, 2183354284u},
                                    {0.9833333333333333, 2444498622u, 407377680u},
                                    {1.05, 2372500077u, 2693822489u},
                                    {1.0666666666666667, 387897133u, 2044267094u},
                                    {1.1083333333333334, 44750453u, 3471244283u},
                                    {1.3, 2070927215u, 766209477u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
        constexpr Frame frames[] = {{0, 2809150944u, 594804805u},
                                    {0.008333333333333333, 2809150944u, 1351621189u},
                                    {0.03333333333333333, 833877973u, 98459599u},
                                    {0.041666666666666664, 833877973u, 1389398943u},
                                    {0.06666666666666667, 833877973u, 1002725477u},
                                    {0.075, 833877973u, 1002725477u},
                                    {0.08333333333333333, 833877973u, 779741225u},
                                    {0.09166666666666666, 833877973u, 392262495u},
                                    {0.13333333333333333, 3048289805u, 3882302231u},
                                    {0.14166666666666666, 2162105513u, 3070709550u},
                                    {0.15, 4237442146u, 1148137783u},
                                    {0.19166666666666668, 1547603542u, 1761438520u},
                                    {0.2, 2906977930u, 1683376400u},
                                    {0.20833333333333334, 1012162558u, 342636473u},
                                    {0.23333333333333334, 3711271696u, 833311445u},
                                    {0.24166666666666667, 1559353219u, 311352251u},
                                    {0.49166666666666664, 3399161558u, 594804805u},
                                    {0.5, 72305851u, 1421366290u},
                                    {0.5083333333333333, 857736737u, 3892452037u},
                                    {0.6166666666666667, 4173643949u, 1930767873u},
                                    {0.625, 748791430u, 820951774u},
                                    {0.6333333333333333, 3240146117u, 3615540099u},
                                    {0.75, 3072657908u, 626897525u},
                                    {0.925, 3250107054u, 10692488u},
                                    {0.9833333333333333, 2225302584u, 1776543059u},
                                    {1.05, 3431311297u, 1023167568u},
                                    {1.0666666666666667, 94214967u, 1617233415u},
                                    {1.1083333333333334, 3826628873u, 2025713712u},
                                    {1.3, 2070927215u, 766209477u},
                                    {1.5, 766209477u, 766209477u},
                                    {2, 766209477u, 766209477u},
                                    {5, 766209477u, 766209477u}};
        PixelFx         fx(PixelFxKind::HeartLost);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeartLost, value);
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
TEST_CASE("stylize.pixelfx.ui.impact.reverseSeekLayersPurityAndStopRestore") {
    for (auto kind : {PixelFxKind::CritNumber, PixelFxKind::HealthBarHit, PixelFxKind::HeartLost}) {
        PixelFx  fx(kind);
        uint32_t originalColor = 0, originalLight = 0;
        for (double t : {.13, 5., .14, .2, .20833333333333334, .5, .62, .8, .95, 1.06, 1.11, 1.7, .13, 0., .13}) {
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
TEST_CASE("stylize.pixelfx.ui.impact.partitionedClocksPreserveEmissionOrder") {
    for (auto kind : {PixelFxKind::CritNumber, PixelFxKind::HealthBarHit, PixelFxKind::HeartLost})
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
TEST_CASE("stylize.pixelfx.ui.impact.materialOwnershipRoundTripBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::CritNumber, PixelFxKind::HealthBarHit, PixelFxKind::HeartLost}) {
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
TEST_CASE("stylize.pixelfx.ui.sourceRoundedPanelFractionalEdgesShineAndAlpha") {
    struct Case {
        double                x, y, w, h, r, a;
        std::optional<double> shine;
        double                px, py;
        bool                  flip;
        double                angle;
        uint32_t              color, light;
    };
    const Case cases[] = {
        {-24, -8, 48, 16, 3, 1, std::nullopt, 0, 0, false, 0, 1564597345u, 998546885u},
        {-24, -8, 48, 16, 3, 1, std::nullopt, 7, -3, true, 0.2, 545608526u, 998546885u},
        {-24.25, -8.6, 48.5, 16.8, 5, 0.6, -4, 0, 0, false, 0, 2873977186u, 998546885u},
        {-24.25, -8.6, 48.5, 16.8, 5, 0.6, -4, 7, -3, true, 0.2, 2881701135u, 998546885u},
        {-8, -6, 16, 12, 0, 1, 0, 0, 0, false, 0, 2188229342u, 998546885u},
        {-8, -6, 16, 12, 0, 1, 0, 7, -3, true, 0.2, 2354610895u, 998546885u},
        {-5, -2, 1.5, 1.5, 1, 0.8, std::nullopt, 0, 0, false, 0, 998546885u, 998546885u},
        {-5, -2, 1.5, 1.5, 1, 0.8, std::nullopt, 7, -3, true, 0.2, 998546885u, 998546885u},
        {-2, -2, 2, 2, 1, 1, std::nullopt, 0, 0, false, 0, 70252125u, 998546885u},
        {-2, -2, 2, 2, 1, 1, std::nullopt, 7, -3, true, 0.2, 90094685u, 998546885u},
        {-3.25, -1.75, 6.5, 3.5, 5, 0.5, 1, 0, 0, false, 0, 480805757u, 998546885u},
        {-3.25, -1.75, 6.5, 3.5, 5, 0.5, 1, 7, -3, true, 0.2, 2328674641u, 998546885u},
        {-48, -9, 130, 18, 5, 0.35, 75, 0, 0, false, 0, 346634798u, 998546885u},
        {-48, -9, 130, 18, 5, 0.35, 75, 7, -3, true, 0.2, 868051057u, 998546885u},
        {-10, -16, 20, 32, 2, 0, 8, 0, 0, false, 0, 998546885u, 998546885u},
        {-10, -16, 20, 32, 2, 0, 8, 7, -3, true, 0.2, 998546885u, 998546885u},
    };
    const ui::PanelColors colors{4281541137u, 4291874047u, 4292725947u, 4289370504u, 4286015061u};
    const PixelFxInfo     info{PixelFxKind::HealthBarHit, "ui/panel-fixture", 256, 128, 128, 64, 1, false};
    std::vector<uint32_t> color(256 * 128), light(color.size());
    for (const auto& c : cases) {
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        pixelfx::Paint painter{info, color, light, 1};
        painter.place(c.px, c.py, c.flip, c.angle);
        ui::panel(painter, c.x, c.y, c.w, c.h, colors, c.r, c.a, c.shine);
        REQUIRE(checksum(color) == c.color);
        REQUIRE(checksum(light) == c.light);
    }
}
TEST_CASE("stylize.pixelfx.ui.sourceHudHeartPaletteFlashRawAlphaScaleAndPlacement") {
    struct Case {
        std::array<uint32_t, 8> ramp;
        double                  x, y;
        bool                    flip;
        double                  angle;
        int                     variant;
        uint32_t                color, light;
    };
    const Case cases[] = {
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         0,
         0,
         false,
         0,
         0,
         2505865441u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         0,
         0,
         false,
         0,
         1,
         913530869u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         0,
         0,
         false,
         0,
         2,
         4284831199u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         0,
         0,
         false,
         0,
         3,
         40007798u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         0,
         3419490117u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         1,
         2502412989u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         2,
         934335318u,
         998546885u},
        {{4281541137u, 4286015061u, 4289370504u, 4292725947u, 4284892415u, 4288252159u, 4291874047u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         3,
         1749185109u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         0,
         0,
         false,
         0,
         0,
         3097518373u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         0,
         0,
         false,
         0,
         1,
         3097518373u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         0,
         0,
         false,
         0,
         2,
         4052239185u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         0,
         0,
         false,
         0,
         3,
         1217684177u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         0,
         44794037u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         1,
         44794037u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         2,
         4231889309u,
         998546885u},
        {{4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         3,
         3917933473u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         0,
         0,
         false,
         0,
         0,
         466626107u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         0,
         0,
         false,
         0,
         1,
         3681804393u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         0,
         0,
         false,
         0,
         2,
         131965413u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         0,
         0,
         false,
         0,
         3,
         2624988261u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         0,
         4165402039u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         1,
         749659289u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         2,
         1214211340u,
         998546885u},
        {{0u, 2148606515u, 66051u, 4280431428u, 4281549909u, 4282668390u, 4283786871u, 4294967295u},
         7,
         -3,
         true,
         0.2,
         3,
         2671960082u,
         998546885u},
    };
    const PixelFxInfo     info{PixelFxKind::HealthBarHit, "ui/heart-fixture", 256, 128, 128, 64, 1, false};
    std::vector<uint32_t> color(256 * 128), light(color.size());
    for (const auto& c : cases) {
        std::fill(color.begin(), color.end(), 0);
        std::fill(light.begin(), light.end(), 0);
        pixelfx::Paint painter{info, color, light, 1};
        painter.place(c.x, c.y, c.flip, c.angle);
        auto                   pal = ui::heartPalette(c.ramp);
        pixelfx::SpriteOptions opts;
        if (c.variant >= 2) {
            opts.scale = 1.7;
            opts.alpha = .6;
            opts.flip  = true;
            opts.flipY = c.variant == 3;
        }
        painter.sprite(ui::HEART, pal, -6.5, -6, opts);
        if (c.variant == 1) {
            pal['k'] = 0;
            pal['w'] = pixelfx::WHITE;
            pal['h'] = pixelfx::WHITE;
            pal['o'] = pixelfx::WHITE;
            pal['m'] = pixelfx::WHITE;
            pal['d'] = c.ramp[6];
            painter.sprite(ui::HEART, pal, -6.5, -6);
        }
        REQUIRE(checksum(color) == c.color);
        REQUIRE(checksum(light) == c.light);
    }
}
TEST_CASE("stylize.pixelfx.ui.impact.critOwnedTextLabelAndMalformedBounds") {
    PixelFx           fx(PixelFxKind::CritNumber);
    auto              settings = fx.settings();
    const std::string expected = "I1 ıſ🙂";
    settings.ui.text           = expected;
    settings.ui.label          = expected;
    auto prepared              = PixelFx::prepareConfiguration(PixelFxKind::CritNumber, settings);
    REQUIRE(prepared);
    settings.ui.text.clear();
    settings.ui.label.clear();
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    REQUIRE(fx.settings().ui.text == expected);
    REQUIRE(fx.settings().ui.label == expected);
    const auto  before = fx.configurationValue();
    std::string unicode64;
    for (int i = 0; i < 64; ++i) unicode64 += "🙂";
    for (bool label : {false, true}) {
        for (const auto& text : std::vector<std::string>{"", std::string(64, 'M'), unicode64}) {
            auto s                           = fx.settings();
            (label ? s.ui.label : s.ui.text) = text;
            REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CritNumber, s));
        }
        for (const auto& text :
             std::vector<std::string>{std::string(65, 'M'), unicode64 + "🙂", std::string("\xc0\xaf", 2)}) {
            auto s                           = fx.settings();
            (label ? s.ui.label : s.ui.text) = text;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CritNumber, s));
            REQUIRE(fx.configurationValue() == before);
        }
        for (const char* json : {"null", "123", "[]"}) {
            auto input = eve::Value::fromJson(json);
            REQUIRE(input);
            auto value = before;
            value.find("options")->set(label ? "label" : "text", input.value());
            REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::CritNumber, value));
            REQUIRE(fx.configurationValue() == before);
        }
    }
}
TEST_CASE("stylize.pixelfx.ui.impact.healthFractionBoundsAndRequestedToOwnership") {
    PixelFx    fx(PixelFxKind::HealthBarHit);
    const auto before = fx.configurationValue();
    for (bool to : {false, true})
        for (double v :
             {-.001, 1.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            auto s                     = fx.settings();
            (to ? s.ui.to : s.ui.from) = v;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HealthBarHit, s));
            REQUIRE(fx.configurationValue() == before);
        }
    auto s         = fx.settings();
    s.ui.from      = .2;
    s.ui.to        = .8;
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::HealthBarHit, s);
    REQUIRE(candidate);
    s.ui.from = 1;
    s.ui.to   = 0;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().ui.from == .2);
    REQUIRE(fx.settings().ui.to == .8);
    PixelFx equal(PixelFxKind::HealthBarHit);
    s         = equal.settings();
    s.ui.from = .2;
    s.ui.to   = .2;
    candidate = PixelFx::prepareConfiguration(PixelFxKind::HealthBarHit, s);
    REQUIRE(candidate);
    REQUIRE(equal.commitConfiguration(std::move(candidate).takeValue()));
    for (double t : {0., .21, .7, 1.1, 2.1}) {
        auto a = fx.prepareSeek(t), b = equal.prepareSeek(t);
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(fx.commitSeek(std::move(a).takeValue()));
        REQUIRE(equal.commitSeek(std::move(b).takeValue()));
        fx.draw();
        equal.draw();
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), equal.color().begin()));
        REQUIRE(std::equal(fx.light().begin(), fx.light().end(), equal.light().begin()));
    }
    const auto active = fx.configurationValue();
    for (const char* key : {"from", "to"})
        for (const char* json : {"null", "true", "[]", "\".5\""}) {
            auto input = eve::Value::fromJson(json);
            REQUIRE(input);
            auto value = active;
            value.find("options")->set(key, input.value());
            REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HealthBarHit, value));
            REQUIRE(fx.configurationValue() == active);
        }
}
}  // namespace test_pixelfx_ui_impact_unit
