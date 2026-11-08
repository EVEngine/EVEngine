// Pinned upstream melee geometry and raster fixtures; no runtime source interpreter required.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/MeleeSwing.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_melee_ring_whip_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.melee.uppercutPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 3254385357u, 2409387895u},
                                    {0.09166666666666666, 4096161760u, 4075207705u},
                                    {0.1, 913212311u, 2222566232u},
                                    {0.14166666666666666, 3002862485u, 1038141581u},
                                    {0.15833333333333333, 28063544u, 3910299148u},
                                    {0.16666666666666666, 852573252u, 3269491810u},
                                    {0.175, 1369156898u, 2153319534u},
                                    {0.18333333333333332, 3832875019u, 960751078u},
                                    {0.2, 3218580045u, 370493978u},
                                    {0.20833333333333334, 3704565183u, 3432799047u},
                                    {0.21666666666666667, 4030741908u, 3027462347u},
                                    {0.225, 1157870330u, 1117305018u},
                                    {0.25, 1978323374u, 142103600u},
                                    {0.3, 1211118557u, 567790809u},
                                    {0.4166666666666667, 20585576u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 2553189192u, 792346053u},
                                    {0.016666666666666666, 4193678212u, 792346053u},
                                    {0.025, 1330358798u, 2591057717u},
                                    {0.075, 3319524532u, 2377595152u},
                                    {0.08333333333333333, 1700148592u, 1919987994u},
                                    {0.09166666666666666, 1446748284u, 3325022955u},
                                    {0.1, 1296200413u, 1021565455u},
                                    {0.14166666666666666, 2941606428u, 3092060830u},
                                    {0.15833333333333333, 352365798u, 2742190735u},
                                    {0.16666666666666666, 2821253461u, 2674862380u},
                                    {0.175, 3065051922u, 1095521587u},
                                    {0.18333333333333332, 66028284u, 3134016659u},
                                    {0.2, 1950547545u, 2995459898u},
                                    {0.20833333333333334, 2059553414u, 253176596u},
                                    {0.21666666666666667, 3614491137u, 2359926142u},
                                    {0.225, 859892206u, 3288349551u},
                                    {0.25, 3596554414u, 3672770150u},
                                    {0.3, 690765762u, 1079079119u},
                                    {0.4166666666666667, 1422688574u, 2961556394u},
                                    {0.5, 2746008270u, 2019134050u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1446031968u, 792346053u},
                                    {0.016666666666666666, 406064420u, 792346053u},
                                    {0.025, 1224598363u, 1791006688u},
                                    {0.075, 843706113u, 3693235263u},
                                    {0.08333333333333333, 1681439697u, 2921219358u},
                                    {0.09166666666666666, 2367010145u, 3726613639u},
                                    {0.1, 3415629242u, 1363701092u},
                                    {0.14166666666666666, 392182384u, 1736938933u},
                                    {0.15833333333333333, 1225381580u, 1481511970u},
                                    {0.16666666666666666, 4274223843u, 2669764145u},
                                    {0.175, 3210624466u, 443824311u},
                                    {0.18333333333333332, 158422086u, 69851474u},
                                    {0.2, 476209536u, 214289986u},
                                    {0.20833333333333334, 353850520u, 1766601941u},
                                    {0.21666666666666667, 2028955888u, 3995867772u},
                                    {0.225, 3798176816u, 3217045054u},
                                    {0.25, 881331530u, 4006874124u},
                                    {0.3, 1680647816u, 4264940393u},
                                    {0.4166666666666667, 1414504623u, 3448249514u},
                                    {0.5, 2303158349u, 3146719251u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 3592390913u, 792346053u},
                                    {0.016666666666666666, 3473858478u, 792346053u},
                                    {0.025, 3118793343u, 1791006688u},
                                    {0.075, 1186515172u, 3693235263u},
                                    {0.08333333333333333, 692359027u, 3237448715u},
                                    {0.09166666666666666, 245282182u, 2932728177u},
                                    {0.1, 3051756897u, 1517674178u},
                                    {0.14166666666666666, 1192691250u, 2774126482u},
                                    {0.15833333333333333, 948345650u, 1680615114u},
                                    {0.16666666666666666, 3677436492u, 3660324454u},
                                    {0.175, 4083511594u, 853852081u},
                                    {0.18333333333333332, 2664111091u, 1453450516u},
                                    {0.2, 1389175978u, 1233862071u},
                                    {0.20833333333333334, 2125795403u, 2959533177u},
                                    {0.21666666666666667, 2065204948u, 2353815107u},
                                    {0.225, 2972951799u, 1725836483u},
                                    {0.25, 36318169u, 442542535u},
                                    {0.3, 240110451u, 621624363u},
                                    {0.4166666666666667, 2061678465u, 1691163095u},
                                    {0.5, 15107806u, 1128654462u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 3935705466u, 1068816963u},
                                    {0.075, 2573520178u, 912231996u},
                                    {0.08333333333333333, 3691414817u, 1334881251u},
                                    {0.09166666666666666, 1322775704u, 761466584u},
                                    {0.1, 2644509892u, 1638764491u},
                                    {0.14166666666666666, 705095690u, 1001360505u},
                                    {0.15833333333333333, 1866382873u, 1515365705u},
                                    {0.16666666666666666, 2828051794u, 151543758u},
                                    {0.175, 1328036974u, 1793518697u},
                                    {0.18333333333333332, 978627624u, 2277622667u},
                                    {0.2, 3511506924u, 284532826u},
                                    {0.20833333333333334, 3002069988u, 4068404297u},
                                    {0.21666666666666667, 3961241439u, 19159127u},
                                    {0.225, 337813301u, 3782214881u},
                                    {0.25, 1622561770u, 72209081u},
                                    {0.3, 947726119u, 3150779547u},
                                    {0.4166666666666667, 1174421348u, 1109224522u},
                                    {0.5, 2997741320u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 3006045132u, 1184173721u},
                                    {0.075, 1386834596u, 2025660576u},
                                    {0.08333333333333333, 2133237091u, 1465790334u},
                                    {0.09166666666666666, 3911333778u, 305086134u},
                                    {0.1, 3553372899u, 753955015u},
                                    {0.14166666666666666, 616229059u, 507987531u},
                                    {0.15833333333333333, 216625400u, 2123810436u},
                                    {0.16666666666666666, 1364977006u, 937690800u},
                                    {0.175, 99388849u, 1816805892u},
                                    {0.18333333333333332, 3864288708u, 612503969u},
                                    {0.2, 919700402u, 1647437887u},
                                    {0.20833333333333334, 2458074961u, 2711245641u},
                                    {0.21666666666666667, 3079082307u, 3444431101u},
                                    {0.225, 1804513041u, 286733373u},
                                    {0.25, 870226830u, 2248194629u},
                                    {0.3, 294440843u, 1682924816u},
                                    {0.4166666666666667, 2426659020u, 1446958279u},
                                    {0.5, 2017682170u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(
            R"json([4278190080, 4278205813, 4278221546, 4278237279, 4278253012, 4278268745, 4278284478, 4278300211, 4278315944, 4278331677, 4278347410, 4278363143, 4278378876, 4278394609, 4278410342, 4278426075, 4278441808, 4278457541, 4278473274, 4278489007, 4278504740, 4278520473, 4278536206, 4278551939, 4278567672, 4278583405, 4278599138, 4278614871, 4278630604, 4278646337, 4278662070, 4278677803, 4278693536, 4278709269, 4278725002, 4278740735, 4278756468, 4278772201, 4278787934, 4278803667, 4278819400, 4278835133, 4278850866, 4278866599, 4278882332, 4278898065, 4278913798, 4278929531, 4278945264, 4278960997, 4278976730, 4278992463, 4279008196, 4279023929, 4279039662, 4279055395, 4279071128, 4279086861, 4279102594, 4279118327, 4279134060, 4279149793, 4279165526, 4279181259])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 2707339743u, 2409387895u},
                                    {0.09166666666666666, 2195024331u, 4075207705u},
                                    {0.1, 1728225548u, 2222566232u},
                                    {0.14166666666666666, 2879215477u, 1038141581u},
                                    {0.15833333333333333, 3964647209u, 3910299148u},
                                    {0.16666666666666666, 2172289871u, 3269491810u},
                                    {0.175, 49917950u, 2153319534u},
                                    {0.18333333333333332, 2163090187u, 960751078u},
                                    {0.2, 3192753394u, 370493978u},
                                    {0.20833333333333334, 2918807429u, 3432799047u},
                                    {0.21666666666666667, 3125802577u, 3027462347u},
                                    {0.225, 2904694118u, 1117305018u},
                                    {0.25, 3917786462u, 142103600u},
                                    {0.3, 1979088244u, 567790809u},
                                    {0.4166666666666667, 305903113u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_outline = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_outline);
        value.find("options")->set("outline", input_outline.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 668224340u, 2409387895u},
                                    {0.09166666666666666, 2805107361u, 4075207705u},
                                    {0.1, 3588429295u, 2222566232u},
                                    {0.14166666666666666, 2776263220u, 1038141581u},
                                    {0.15833333333333333, 3676855800u, 3910299148u},
                                    {0.16666666666666666, 525932533u, 3269491810u},
                                    {0.175, 2984183391u, 2153319534u},
                                    {0.18333333333333332, 2299919563u, 960751078u},
                                    {0.2, 514092700u, 370493978u},
                                    {0.20833333333333334, 3840112399u, 3432799047u},
                                    {0.21666666666666667, 2637739453u, 3027462347u},
                                    {0.225, 4147719703u, 1117305018u},
                                    {0.25, 4286894267u, 142103600u},
                                    {0.3, 990734844u, 567790809u},
                                    {0.4166666666666667, 2281285801u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_outline = eve::Value::fromJson(R"json(1193046)json");
        REQUIRE(input_outline);
        value.find("options")->set("outline", input_outline.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 2693178356u, 2409387895u},
                                    {0.09166666666666666, 3055593489u, 4075207705u},
                                    {0.1, 4095954903u, 2222566232u},
                                    {0.14166666666666666, 4037911496u, 1038141581u},
                                    {0.15833333333333333, 3955112896u, 3910299148u},
                                    {0.16666666666666666, 42118793u, 3269491810u},
                                    {0.175, 2464241155u, 2153319534u},
                                    {0.18333333333333332, 3261143023u, 960751078u},
                                    {0.2, 3693502836u, 370493978u},
                                    {0.20833333333333334, 2721401207u, 3432799047u},
                                    {0.21666666666666667, 3152936633u, 3027462347u},
                                    {0.225, 2028633335u, 1117305018u},
                                    {0.25, 2197680307u, 142103600u},
                                    {0.3, 2974958348u, 567790809u},
                                    {0.4166666666666667, 97840329u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_outline = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input_outline);
        value.find("options")->set("outline", input_outline.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 1453692056u, 2409387895u},
                                    {0.09166666666666666, 209873901u, 4075207705u},
                                    {0.1, 4144779759u, 2222566232u},
                                    {0.14166666666666666, 2593383528u, 1038141581u},
                                    {0.15833333333333333, 681142000u, 3910299148u},
                                    {0.16666666666666666, 1289314673u, 3269491810u},
                                    {0.175, 855798291u, 2153319534u},
                                    {0.18333333333333332, 3540036923u, 960751078u},
                                    {0.2, 2778550880u, 370493978u},
                                    {0.20833333333333334, 1780105383u, 3432799047u},
                                    {0.21666666666666667, 569109225u, 3027462347u},
                                    {0.225, 2873811899u, 1117305018u},
                                    {0.25, 1756258503u, 142103600u},
                                    {0.3, 2234775480u, 567790809u},
                                    {0.4166666666666667, 284918701u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_outline = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_outline);
        value.find("options")->set("outline", input_outline.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 3354673169u, 2409387895u},
                                    {0.09166666666666666, 2012575000u, 4075207705u},
                                    {0.1, 1188374271u, 2222566232u},
                                    {0.14166666666666666, 3300265265u, 1038141581u},
                                    {0.15833333333333333, 87717276u, 3910299148u},
                                    {0.16666666666666666, 1308050360u, 3269491810u},
                                    {0.175, 402812854u, 2153319534u},
                                    {0.18333333333333332, 2240272803u, 960751078u},
                                    {0.2, 4000098453u, 370493978u},
                                    {0.20833333333333334, 3054927467u, 3432799047u},
                                    {0.21666666666666667, 2105750052u, 3027462347u},
                                    {0.225, 860091310u, 1117305018u},
                                    {0.25, 3053498686u, 142103600u},
                                    {0.3, 1762243873u, 567790809u},
                                    {0.4166666666666667, 4242536220u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_outline = eve::Value::fromJson(R"json(4278190080)json");
        REQUIRE(input_outline);
        value.find("options")->set("outline", input_outline.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
TEST_CASE("stylize.pixelfx.melee.uppercutPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 3254385357u, 2409387895u},
                                    {0.09166666666666666, 4096161760u, 4075207705u},
                                    {0.1, 913212311u, 2222566232u},
                                    {0.14166666666666666, 3002862485u, 1038141581u},
                                    {0.15833333333333333, 28063544u, 3910299148u},
                                    {0.16666666666666666, 852573252u, 3269491810u},
                                    {0.175, 1369156898u, 2153319534u},
                                    {0.18333333333333332, 3832875019u, 960751078u},
                                    {0.2, 3218580045u, 370493978u},
                                    {0.20833333333333334, 3704565183u, 3432799047u},
                                    {0.21666666666666667, 4030741908u, 3027462347u},
                                    {0.225, 1157870330u, 1117305018u},
                                    {0.25, 1978323374u, 142103600u},
                                    {0.3, 1211118557u, 567790809u},
                                    {0.4166666666666667, 20585576u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 2553189192u, 792346053u},
                                    {0.016666666666666666, 4193678212u, 792346053u},
                                    {0.025, 2909071740u, 1791006688u},
                                    {0.075, 3329035752u, 3693235263u},
                                    {0.08333333333333333, 3810667492u, 3619509634u},
                                    {0.09166666666666666, 2631305835u, 2605731756u},
                                    {0.1, 504628259u, 21032274u},
                                    {0.14166666666666666, 705888583u, 3849022886u},
                                    {0.15833333333333333, 2999979785u, 794845657u},
                                    {0.16666666666666666, 1115751848u, 1898948519u},
                                    {0.175, 1663622300u, 3377760587u},
                                    {0.18333333333333332, 2256460366u, 1824378665u},
                                    {0.2, 3983638773u, 1086028747u},
                                    {0.20833333333333334, 1394865229u, 2977216009u},
                                    {0.21666666666666667, 3150495295u, 3011601441u},
                                    {0.225, 2616192129u, 712056989u},
                                    {0.25, 4157889872u, 4032915046u},
                                    {0.3, 3819979615u, 3789520295u},
                                    {0.4166666666666667, 332182616u, 1964257207u},
                                    {0.5, 3300472202u, 77012045u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 3254385357u, 2409387895u},
                                    {0.09166666666666666, 4096161760u, 4075207705u},
                                    {0.1, 913212311u, 2222566232u},
                                    {0.14166666666666666, 3002862485u, 1038141581u},
                                    {0.15833333333333333, 28063544u, 3910299148u},
                                    {0.16666666666666666, 852573252u, 3269491810u},
                                    {0.175, 1369156898u, 2153319534u},
                                    {0.18333333333333332, 3832875019u, 960751078u},
                                    {0.2, 3218580045u, 370493978u},
                                    {0.20833333333333334, 3704565183u, 3432799047u},
                                    {0.21666666666666667, 4030741908u, 3027462347u},
                                    {0.225, 1157870330u, 1117305018u},
                                    {0.25, 1978323374u, 142103600u},
                                    {0.3, 1211118557u, 567790809u},
                                    {0.4166666666666667, 20585576u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
        constexpr Frame frames[] = {{0, 792346053u, 792346053u},
                                    {0.008333333333333333, 1645843171u, 792346053u},
                                    {0.016666666666666666, 226807409u, 792346053u},
                                    {0.025, 228578528u, 1791006688u},
                                    {0.075, 1987477203u, 3693235263u},
                                    {0.08333333333333333, 3254385357u, 2409387895u},
                                    {0.09166666666666666, 4096161760u, 4075207705u},
                                    {0.1, 913212311u, 2222566232u},
                                    {0.14166666666666666, 3002862485u, 1038141581u},
                                    {0.15833333333333333, 28063544u, 3910299148u},
                                    {0.16666666666666666, 852573252u, 3269491810u},
                                    {0.175, 1369156898u, 2153319534u},
                                    {0.18333333333333332, 3832875019u, 960751078u},
                                    {0.2, 3218580045u, 370493978u},
                                    {0.20833333333333334, 3704565183u, 3432799047u},
                                    {0.21666666666666667, 4030741908u, 3027462347u},
                                    {0.225, 1157870330u, 1117305018u},
                                    {0.25, 1978323374u, 142103600u},
                                    {0.3, 1211118557u, 567790809u},
                                    {0.4166666666666667, 20585576u, 3292568726u},
                                    {0.5, 3005884629u, 3159314493u},
                                    {0.8, 792346053u, 792346053u},
                                    {0.85, 792346053u, 792346053u},
                                    {1, 792346053u, 792346053u},
                                    {5, 792346053u, 792346053u}};
        PixelFx         fx(PixelFxKind::Uppercut);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::Uppercut, value);
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
TEST_CASE("stylize.pixelfx.melee.spinSlashPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3278130044u, 436553446u},
                                    {0.016666666666666666, 2905097655u, 3260505489u},
                                    {0.025, 3266407064u, 3423763488u},
                                    {0.075, 3809351893u, 3078053246u},
                                    {0.08333333333333333, 3612720675u, 1449256263u},
                                    {0.09166666666666666, 131226760u, 4099808698u},
                                    {0.1, 3055250075u, 3908297732u},
                                    {0.14166666666666666, 3346705755u, 111234418u},
                                    {0.15833333333333333, 3092726406u, 564577782u},
                                    {0.16666666666666666, 4070333101u, 2713377091u},
                                    {0.175, 2662921024u, 326344850u},
                                    {0.18333333333333332, 1062648353u, 855503958u},
                                    {0.2, 491299867u, 1795641625u},
                                    {0.20833333333333334, 821181422u, 303929205u},
                                    {0.21666666666666667, 555775930u, 1707587250u},
                                    {0.225, 1427895570u, 2432986750u},
                                    {0.25, 1079719053u, 2476093091u},
                                    {0.3, 3330640954u, 373900448u},
                                    {0.4166666666666667, 2666416008u, 386814314u},
                                    {0.5, 1784491434u, 1340221067u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 3873740753u, 3626727474u},
                                    {0.008333333333333333, 998922472u, 597852933u},
                                    {0.016666666666666666, 3765697516u, 1544424844u},
                                    {0.025, 699074704u, 1648172099u},
                                    {0.075, 3419424031u, 3781928027u},
                                    {0.08333333333333333, 2957658422u, 1381761541u},
                                    {0.09166666666666666, 1264671978u, 3541540352u},
                                    {0.1, 200658899u, 355840492u},
                                    {0.14166666666666666, 3890513695u, 355721062u},
                                    {0.15833333333333333, 1091128412u, 2421577549u},
                                    {0.16666666666666666, 2145759358u, 4243897614u},
                                    {0.175, 375073045u, 2015238066u},
                                    {0.18333333333333332, 551484996u, 846926059u},
                                    {0.2, 1357664785u, 161164634u},
                                    {0.20833333333333334, 4294524999u, 3987153666u},
                                    {0.21666666666666667, 3339603206u, 1449603432u},
                                    {0.225, 3995556090u, 1513556603u},
                                    {0.25, 1737904872u, 3439780113u},
                                    {0.3, 1789325021u, 1438381267u},
                                    {0.4166666666666667, 1824775038u, 2493586241u},
                                    {0.5, 1576439308u, 3515473376u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3442132698u, 363432551u},
                                    {0.016666666666666666, 1858285654u, 4248115680u},
                                    {0.025, 1926007320u, 2447067908u},
                                    {0.075, 3109239785u, 3641084744u},
                                    {0.08333333333333333, 3123097432u, 2100957555u},
                                    {0.09166666666666666, 781801673u, 2995659592u},
                                    {0.1, 2226610991u, 549553153u},
                                    {0.14166666666666666, 925915404u, 2045351012u},
                                    {0.15833333333333333, 2904969801u, 2915132991u},
                                    {0.16666666666666666, 3371828003u, 3493205376u},
                                    {0.175, 2515880253u, 916957353u},
                                    {0.18333333333333332, 3131731938u, 2079653681u},
                                    {0.2, 980042427u, 1650713396u},
                                    {0.20833333333333334, 2533061904u, 4227617354u},
                                    {0.21666666666666667, 1728859560u, 3599760184u},
                                    {0.225, 368163966u, 419797377u},
                                    {0.25, 274449177u, 3344219560u},
                                    {0.3, 1281245418u, 1508884683u},
                                    {0.4166666666666667, 676250597u, 1420943993u},
                                    {0.5, 2995478199u, 1228380988u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 305311811u, 1902986909u},
                                    {0.016666666666666666, 102389981u, 3950974060u},
                                    {0.025, 2624143222u, 2181829423u},
                                    {0.075, 2194000661u, 4244806716u},
                                    {0.08333333333333333, 2954336216u, 1557440127u},
                                    {0.09166666666666666, 3689854398u, 770405416u},
                                    {0.1, 3140240924u, 547261395u},
                                    {0.14166666666666666, 1321589915u, 913286244u},
                                    {0.15833333333333333, 3262293353u, 2218106878u},
                                    {0.16666666666666666, 3564982480u, 4283691265u},
                                    {0.175, 756800561u, 3106780046u},
                                    {0.18333333333333332, 1869980212u, 3514580235u},
                                    {0.2, 3119219583u, 2072141283u},
                                    {0.20833333333333334, 361419762u, 529085040u},
                                    {0.21666666666666667, 122296650u, 258055187u},
                                    {0.225, 3330522338u, 1048882386u},
                                    {0.25, 3050869948u, 2995340366u},
                                    {0.3, 2466816947u, 2024095686u},
                                    {0.4166666666666667, 441032132u, 3606182281u},
                                    {0.5, 2127761427u, 2753777953u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1737756945u, 131951685u},
                                    {0.008333333333333333, 3753286845u, 1558938232u},
                                    {0.016666666666666666, 3918982984u, 3020358236u},
                                    {0.025, 46740337u, 2745337884u},
                                    {0.075, 3953595605u, 934061397u},
                                    {0.08333333333333333, 2482747049u, 2324505617u},
                                    {0.09166666666666666, 1517946512u, 3775466732u},
                                    {0.1, 502916612u, 3129107015u},
                                    {0.14166666666666666, 874688373u, 2498543234u},
                                    {0.15833333333333333, 2135409349u, 1810879079u},
                                    {0.16666666666666666, 2827145621u, 4228259774u},
                                    {0.175, 661830169u, 2363464437u},
                                    {0.18333333333333332, 2198228484u, 301224308u},
                                    {0.2, 2235454908u, 11227347u},
                                    {0.20833333333333334, 3480653749u, 2020296382u},
                                    {0.21666666666666667, 1992066740u, 1682139612u},
                                    {0.225, 228623821u, 1736767877u},
                                    {0.25, 1491169188u, 2197338641u},
                                    {0.3, 2426100332u, 515100800u},
                                    {0.4166666666666667, 3154903128u, 3161547392u},
                                    {0.5, 79623637u, 3299754759u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 3556429297u, 2207099316u},
                                    {0.008333333333333333, 3722983984u, 3543504221u},
                                    {0.016666666666666666, 3290180291u, 625262095u},
                                    {0.025, 1910001852u, 374190877u},
                                    {0.075, 2270138162u, 2151990577u},
                                    {0.08333333333333333, 481784664u, 3062788255u},
                                    {0.09166666666666666, 1034356165u, 2655418780u},
                                    {0.1, 2248170327u, 375008004u},
                                    {0.14166666666666666, 3900379094u, 2817765028u},
                                    {0.15833333333333333, 3377610932u, 1391630367u},
                                    {0.16666666666666666, 1559621138u, 3485621651u},
                                    {0.175, 2491397401u, 3793011568u},
                                    {0.18333333333333332, 1381056374u, 3618593136u},
                                    {0.2, 2173909488u, 2689192629u},
                                    {0.20833333333333334, 3952155863u, 1355342733u},
                                    {0.21666666666666667, 2681594118u, 1674186899u},
                                    {0.225, 3508726392u, 4164921327u},
                                    {0.25, 2829129450u, 559713630u},
                                    {0.3, 2162046696u, 896960004u},
                                    {0.4166666666666667, 4037840076u, 277592776u},
                                    {0.5, 1609517729u, 3953545985u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(
            R"json([4278190080, 4278205813, 4278221546, 4278237279, 4278253012, 4278268745, 4278284478, 4278300211, 4278315944, 4278331677, 4278347410, 4278363143, 4278378876, 4278394609, 4278410342, 4278426075, 4278441808, 4278457541, 4278473274, 4278489007, 4278504740, 4278520473, 4278536206, 4278551939, 4278567672, 4278583405, 4278599138, 4278614871, 4278630604, 4278646337, 4278662070, 4278677803, 4278693536, 4278709269, 4278725002, 4278740735, 4278756468, 4278772201, 4278787934, 4278803667, 4278819400, 4278835133, 4278850866, 4278866599, 4278882332, 4278898065, 4278913798, 4278929531, 4278945264, 4278960997, 4278976730, 4278992463, 4279008196, 4279023929, 4279039662, 4279055395, 4279071128, 4279086861, 4279102594, 4279118327, 4279134060, 4279149793, 4279165526, 4279181259])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1875789457u, 4026195687u},
                                    {0.008333333333333333, 390865948u, 2167952789u},
                                    {0.016666666666666666, 1104768204u, 1470398444u},
                                    {0.025, 642866844u, 2599671116u},
                                    {0.075, 1939616826u, 3468311340u},
                                    {0.08333333333333333, 3552250170u, 3585968787u},
                                    {0.09166666666666666, 1044163141u, 3081263833u},
                                    {0.1, 2498584885u, 1510209746u},
                                    {0.14166666666666666, 3463871312u, 2565210368u},
                                    {0.15833333333333333, 1827572811u, 582260074u},
                                    {0.16666666666666666, 1648839260u, 2638246874u},
                                    {0.175, 2271413497u, 1843178709u},
                                    {0.18333333333333332, 3154066241u, 1085739622u},
                                    {0.2, 2019918996u, 2190861572u},
                                    {0.20833333333333334, 329312848u, 2259363437u},
                                    {0.21666666666666667, 764461410u, 556398712u},
                                    {0.225, 2390426210u, 2868816438u},
                                    {0.25, 2455925999u, 2825154362u},
                                    {0.3, 1245501012u, 3368986329u},
                                    {0.4166666666666667, 3135230522u, 1963144689u},
                                    {0.5, 1762299368u, 2844593924u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1875789457u, 3885140693u},
                                    {0.008333333333333333, 1979658835u, 420371813u},
                                    {0.016666666666666666, 1646095541u, 3190874369u},
                                    {0.025, 1795327970u, 1224164507u},
                                    {0.075, 2122317277u, 2666646602u},
                                    {0.08333333333333333, 42288709u, 4091666540u},
                                    {0.09166666666666666, 3695642636u, 3133214731u},
                                    {0.1, 1465082780u, 2932402309u},
                                    {0.14166666666666666, 3917211241u, 1991927216u},
                                    {0.15833333333333333, 1199280486u, 1110651076u},
                                    {0.16666666666666666, 1270977185u, 4115417492u},
                                    {0.175, 76643668u, 1452242679u},
                                    {0.18333333333333332, 1469597216u, 1638359969u},
                                    {0.2, 2325539966u, 1482859253u},
                                    {0.20833333333333334, 4208234770u, 932383293u},
                                    {0.21666666666666667, 1449003456u, 278314421u},
                                    {0.225, 1084870768u, 968409109u},
                                    {0.25, 1511791102u, 165098692u},
                                    {0.3, 258530741u, 334185275u},
                                    {0.4166666666666667, 3562910498u, 1199652892u},
                                    {0.5, 2478240659u, 611694048u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(0.5)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 3886216145u, 119581128u},
                                    {0.008333333333333333, 3886963597u, 2104071528u},
                                    {0.016666666666666666, 4051535964u, 2525067421u},
                                    {0.025, 250018987u, 887912847u},
                                    {0.075, 1196273537u, 1499473223u},
                                    {0.08333333333333333, 9840879u, 249726512u},
                                    {0.09166666666666666, 3161596792u, 1058525057u},
                                    {0.1, 1188731049u, 2860241874u},
                                    {0.14166666666666666, 3095295838u, 1860819662u},
                                    {0.15833333333333333, 3108423770u, 3110428741u},
                                    {0.16666666666666666, 966133055u, 1345329295u},
                                    {0.175, 2516754476u, 4243948808u},
                                    {0.18333333333333332, 3383599568u, 1155199106u},
                                    {0.2, 701099842u, 576671u},
                                    {0.20833333333333334, 3696531695u, 2651997472u},
                                    {0.21666666666666667, 666873817u, 2069995161u},
                                    {0.225, 2442794282u, 3702134092u},
                                    {0.25, 1598393575u, 1783967918u},
                                    {0.3, 1811115572u, 2912462204u},
                                    {0.4166666666666667, 128207153u, 2404012548u},
                                    {0.5, 881541028u, 417315265u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 2419551505u, 547273030u},
                                    {0.008333333333333333, 3759116730u, 2953742510u},
                                    {0.016666666666666666, 3415938792u, 1996646749u},
                                    {0.025, 2238064996u, 2198144243u},
                                    {0.075, 990112960u, 2568262766u},
                                    {0.08333333333333333, 3488821634u, 125164859u},
                                    {0.09166666666666666, 146880349u, 41319780u},
                                    {0.1, 3093336731u, 3893608957u},
                                    {0.14166666666666666, 1158391868u, 554511815u},
                                    {0.15833333333333333, 1229448815u, 3855959899u},
                                    {0.16666666666666666, 187432607u, 3601087173u},
                                    {0.175, 3607188374u, 501526745u},
                                    {0.18333333333333332, 2406005215u, 3868378561u},
                                    {0.2, 2041859795u, 3459424030u},
                                    {0.20833333333333334, 989547338u, 1347310114u},
                                    {0.21666666666666667, 1617169844u, 722238183u},
                                    {0.225, 3505575417u, 3207825459u},
                                    {0.25, 2992735446u, 3898197900u},
                                    {0.3, 3217477028u, 1110753813u},
                                    {0.4166666666666667, 4040048509u, 1015004838u},
                                    {0.5, 2072412099u, 584135049u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2255765532u},
                                    {0.008333333333333333, 1992047004u, 3503172148u},
                                    {0.016666666666666666, 4220825987u, 3723179611u},
                                    {0.025, 4189011741u, 1148365457u},
                                    {0.075, 2738424310u, 378614356u},
                                    {0.08333333333333333, 2069128510u, 733379858u},
                                    {0.09166666666666666, 1178472611u, 3193747763u},
                                    {0.1, 1624592158u, 3980029843u},
                                    {0.14166666666666666, 1863485484u, 1773402219u},
                                    {0.15833333333333333, 2392529197u, 2800894980u},
                                    {0.16666666666666666, 2370584740u, 3884966717u},
                                    {0.175, 633958212u, 2989443299u},
                                    {0.18333333333333332, 2257381571u, 732160461u},
                                    {0.2, 1972484966u, 3575520279u},
                                    {0.20833333333333334, 2546577572u, 2603289720u},
                                    {0.21666666666666667, 1165451675u, 1196359165u},
                                    {0.225, 1858261583u, 794601024u},
                                    {0.25, 4081442705u, 2185985740u},
                                    {0.3, 2819850070u, 1721513434u},
                                    {0.4166666666666667, 1086398060u, 2183041219u},
                                    {0.5, 173941324u, 1162882215u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(46.5)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 3553140676u, 2959112305u},
                                    {0.008333333333333333, 2311800261u, 229371593u},
                                    {0.016666666666666666, 2311800261u, 1870863178u},
                                    {0.025, 3449221239u, 3551842635u},
                                    {0.075, 478758048u, 2836429901u},
                                    {0.08333333333333333, 2458922231u, 124173853u},
                                    {0.09166666666666666, 549391566u, 1281869349u},
                                    {0.1, 2042572457u, 3071520249u},
                                    {0.14166666666666666, 806727911u, 3040755558u},
                                    {0.15833333333333333, 2998487091u, 3621533317u},
                                    {0.16666666666666666, 770313813u, 1117476621u},
                                    {0.175, 1851174714u, 172165615u},
                                    {0.18333333333333332, 559361886u, 3233677521u},
                                    {0.2, 3671275802u, 1346441907u},
                                    {0.20833333333333334, 640574251u, 3214119728u},
                                    {0.21666666666666667, 2141944882u, 4263844122u},
                                    {0.225, 4221540621u, 2852326071u},
                                    {0.25, 2044125081u, 3955442310u},
                                    {0.3, 3509854724u, 3807909868u},
                                    {0.4166666666666667, 3469061273u, 4238885762u},
                                    {0.5, 3394605976u, 1926675268u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(128)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 2311800261u, 2311800261u},
                                    {0.008333333333333333, 2311800261u, 2311800261u},
                                    {0.016666666666666666, 2311800261u, 2311800261u},
                                    {0.025, 2311800261u, 2311800261u},
                                    {0.075, 2709537496u, 2853134688u},
                                    {0.08333333333333333, 4141521655u, 1716905472u},
                                    {0.09166666666666666, 3086890336u, 1330591189u},
                                    {0.1, 2426011821u, 34271196u},
                                    {0.14166666666666666, 3272178318u, 89365667u},
                                    {0.15833333333333333, 4163651389u, 3039728733u},
                                    {0.16666666666666666, 1955848433u, 2322251865u},
                                    {0.175, 2045213712u, 2469009964u},
                                    {0.18333333333333332, 2202940892u, 2398646834u},
                                    {0.2, 2691992692u, 2727947124u},
                                    {0.20833333333333334, 1589602928u, 676520956u},
                                    {0.21666666666666667, 3257255392u, 2974879373u},
                                    {0.225, 797865643u, 3614127253u},
                                    {0.25, 3578253435u, 3839147062u},
                                    {0.3, 3813499160u, 2575126500u},
                                    {0.4166666666666667, 2199196932u, 2524878725u},
                                    {0.5, 750606197u, 358017331u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(256)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 2311800261u, 2311800261u},
                                    {0.008333333333333333, 2311800261u, 2311800261u},
                                    {0.016666666666666666, 2311800261u, 2311800261u},
                                    {0.025, 2311800261u, 2311800261u},
                                    {0.075, 2311800261u, 2311800261u},
                                    {0.08333333333333333, 2311800261u, 2311800261u},
                                    {0.09166666666666666, 2311800261u, 2311800261u},
                                    {0.1, 2311800261u, 2311800261u},
                                    {0.14166666666666666, 2311800261u, 2311800261u},
                                    {0.15833333333333333, 2311800261u, 2311800261u},
                                    {0.16666666666666666, 2311800261u, 2311800261u},
                                    {0.175, 2311800261u, 1757450285u},
                                    {0.18333333333333332, 2311800261u, 3127790281u},
                                    {0.2, 2311800261u, 1879231785u},
                                    {0.20833333333333334, 2311800261u, 1435640997u},
                                    {0.21666666666666667, 2311800261u, 2156370085u},
                                    {0.225, 2311800261u, 3385251209u},
                                    {0.25, 2311800261u, 2449884897u},
                                    {0.3, 2311800261u, 1420444685u},
                                    {0.4166666666666667, 2311800261u, 3273855977u},
                                    {0.5, 2311800261u, 1852496989u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_radius = eve::Value::fromJson(R"json(512)json");
        REQUIRE(input_radius);
        value.find("options")->set("radius", input_radius.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
TEST_CASE("stylize.pixelfx.melee.spinSlashPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3278130044u, 436553446u},
                                    {0.016666666666666666, 2905097655u, 3260505489u},
                                    {0.025, 3266407064u, 3423763488u},
                                    {0.075, 3809351893u, 3078053246u},
                                    {0.08333333333333333, 3612720675u, 1449256263u},
                                    {0.09166666666666666, 131226760u, 4099808698u},
                                    {0.1, 3055250075u, 3908297732u},
                                    {0.14166666666666666, 3346705755u, 111234418u},
                                    {0.15833333333333333, 3092726406u, 564577782u},
                                    {0.16666666666666666, 4070333101u, 2713377091u},
                                    {0.175, 2662921024u, 326344850u},
                                    {0.18333333333333332, 1062648353u, 855503958u},
                                    {0.2, 491299867u, 1795641625u},
                                    {0.20833333333333334, 821181422u, 303929205u},
                                    {0.21666666666666667, 555775930u, 1707587250u},
                                    {0.225, 1427895570u, 2432986750u},
                                    {0.25, 1079719053u, 2476093091u},
                                    {0.3, 3330640954u, 373900448u},
                                    {0.4166666666666667, 2666416008u, 386814314u},
                                    {0.5, 1784491434u, 1340221067u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3383743984u, 2713031120u},
                                    {0.016666666666666666, 3802437659u, 2625985257u},
                                    {0.025, 4208563549u, 4084651521u},
                                    {0.075, 1750842728u, 3563628372u},
                                    {0.08333333333333333, 2198483267u, 3795103888u},
                                    {0.09166666666666666, 1466821951u, 1278169091u},
                                    {0.1, 2318404701u, 3508970526u},
                                    {0.14166666666666666, 2253962355u, 2898418187u},
                                    {0.15833333333333333, 2342838624u, 1646895799u},
                                    {0.16666666666666666, 1030229362u, 13834006u},
                                    {0.175, 4029462870u, 3635990677u},
                                    {0.18333333333333332, 1949386193u, 3508661724u},
                                    {0.2, 1381318867u, 102141085u},
                                    {0.20833333333333334, 2488055072u, 1431961912u},
                                    {0.21666666666666667, 3400137271u, 2267996737u},
                                    {0.225, 221290962u, 4249680538u},
                                    {0.25, 2530409736u, 3035407227u},
                                    {0.3, 4151587815u, 2826631509u},
                                    {0.4166666666666667, 1734344689u, 2172248240u},
                                    {0.5, 2605936346u, 3605766182u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3278130044u, 436553446u},
                                    {0.016666666666666666, 2905097655u, 3260505489u},
                                    {0.025, 3266407064u, 3423763488u},
                                    {0.075, 3809351893u, 3078053246u},
                                    {0.08333333333333333, 3612720675u, 1449256263u},
                                    {0.09166666666666666, 131226760u, 4099808698u},
                                    {0.1, 3055250075u, 3908297732u},
                                    {0.14166666666666666, 3346705755u, 111234418u},
                                    {0.15833333333333333, 3092726406u, 564577782u},
                                    {0.16666666666666666, 4070333101u, 2713377091u},
                                    {0.175, 2662921024u, 326344850u},
                                    {0.18333333333333332, 1062648353u, 855503958u},
                                    {0.2, 491299867u, 1795641625u},
                                    {0.20833333333333334, 821181422u, 303929205u},
                                    {0.21666666666666667, 555775930u, 1707587250u},
                                    {0.225, 1427895570u, 2432986750u},
                                    {0.25, 1079719053u, 2476093091u},
                                    {0.3, 3330640954u, 373900448u},
                                    {0.4166666666666667, 2666416008u, 386814314u},
                                    {0.5, 1784491434u, 1340221067u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
        constexpr Frame frames[] = {{0, 1574901713u, 2698056993u},
                                    {0.008333333333333333, 3278130044u, 436553446u},
                                    {0.016666666666666666, 2905097655u, 3260505489u},
                                    {0.025, 3266407064u, 3423763488u},
                                    {0.075, 3809351893u, 3078053246u},
                                    {0.08333333333333333, 3612720675u, 1449256263u},
                                    {0.09166666666666666, 131226760u, 4099808698u},
                                    {0.1, 3055250075u, 3908297732u},
                                    {0.14166666666666666, 3346705755u, 111234418u},
                                    {0.15833333333333333, 3092726406u, 564577782u},
                                    {0.16666666666666666, 4070333101u, 2713377091u},
                                    {0.175, 2662921024u, 326344850u},
                                    {0.18333333333333332, 1062648353u, 855503958u},
                                    {0.2, 491299867u, 1795641625u},
                                    {0.20833333333333334, 821181422u, 303929205u},
                                    {0.21666666666666667, 555775930u, 1707587250u},
                                    {0.225, 1427895570u, 2432986750u},
                                    {0.25, 1079719053u, 2476093091u},
                                    {0.3, 3330640954u, 373900448u},
                                    {0.4166666666666667, 2666416008u, 386814314u},
                                    {0.5, 1784491434u, 1340221067u},
                                    {0.8, 2311800261u, 2311800261u},
                                    {0.85, 2311800261u, 2311800261u},
                                    {1, 2311800261u, 2311800261u},
                                    {5, 2311800261u, 2311800261u}};
        PixelFx         fx(PixelFxKind::SpinSlash);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::SpinSlash, value);
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
TEST_CASE("stylize.pixelfx.melee.whipCrackPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 4076342217u, 3856125171u},
                                    {0.225, 3044724622u, 934622352u},
                                    {0.25, 3663283792u, 3066869454u},
                                    {0.3, 3627338068u, 1455232055u},
                                    {0.4166666666666667, 3395410421u, 2131854441u},
                                    {0.5, 3808073710u, 2861129105u},
                                    {0.8, 635507059u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 1663513019u, 1947969564u},
                                    {0.008333333333333333, 1663513019u, 1947969564u},
                                    {0.016666666666666666, 1663513019u, 1947969564u},
                                    {0.025, 1663513019u, 3758694806u},
                                    {0.075, 816663574u, 2468684704u},
                                    {0.08333333333333333, 87749406u, 1761738936u},
                                    {0.09166666666666666, 367853436u, 2865162757u},
                                    {0.1, 2256376373u, 347573706u},
                                    {0.14166666666666666, 2498304793u, 2385312297u},
                                    {0.15833333333333333, 1248338882u, 1587075717u},
                                    {0.16666666666666666, 1505844121u, 904157804u},
                                    {0.175, 4106907634u, 3422538818u},
                                    {0.18333333333333332, 3282211540u, 170821028u},
                                    {0.2, 2006433980u, 3804725647u},
                                    {0.20833333333333334, 3450670285u, 515584604u},
                                    {0.21666666666666667, 54608333u, 497168033u},
                                    {0.225, 3914599142u, 3430254969u},
                                    {0.25, 77901220u, 3627949729u},
                                    {0.3, 208009704u, 3950535992u},
                                    {0.4166666666666667, 3596139771u, 3394588160u},
                                    {0.5, 1019010780u, 1662233764u},
                                    {0.8, 3001647705u, 3095223749u},
                                    {0.85, 1003705330u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 420452724u, 3258652970u},
                                    {0.225, 1463378972u, 3532198687u},
                                    {0.25, 1675663233u, 1781228674u},
                                    {0.3, 1106530173u, 2588978392u},
                                    {0.4166666666666667, 3883779999u, 4250477098u},
                                    {0.5, 1913797141u, 1586080019u},
                                    {0.8, 682341725u, 3095223749u},
                                    {0.85, 1617668822u, 3095223749u},
                                    {1, 735311341u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 1112254650u, 4176316891u},
                                    {0.225, 2149450235u, 3322645343u},
                                    {0.25, 3863840953u, 260367326u},
                                    {0.3, 231366167u, 2287476627u},
                                    {0.4166666666666667, 1481801928u, 2264153895u},
                                    {0.5, 561662969u, 859400102u},
                                    {0.8, 1720216738u, 3095223749u},
                                    {0.85, 2469616433u, 3095223749u},
                                    {1, 2149053030u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2609720285u, 2281597030u},
                                    {0.008333333333333333, 2609720285u, 2281597030u},
                                    {0.016666666666666666, 2609720285u, 2281597030u},
                                    {0.025, 2609720285u, 3015261067u},
                                    {0.075, 3865785773u, 2052309100u},
                                    {0.08333333333333333, 2178069997u, 3201318855u},
                                    {0.09166666666666666, 2527282173u, 4118193398u},
                                    {0.1, 2567470205u, 2192097804u},
                                    {0.14166666666666666, 3468614117u, 1830931262u},
                                    {0.15833333333333333, 1760065873u, 3947488195u},
                                    {0.16666666666666666, 3061443789u, 819341021u},
                                    {0.175, 3461972773u, 3983510262u},
                                    {0.18333333333333332, 2253819617u, 225662486u},
                                    {0.2, 1573715301u, 207090096u},
                                    {0.20833333333333334, 3726327625u, 2894355844u},
                                    {0.21666666666666667, 1339089041u, 480059949u},
                                    {0.225, 459808985u, 2146478003u},
                                    {0.25, 3614273466u, 2635410247u},
                                    {0.3, 547156643u, 4186036246u},
                                    {0.4166666666666667, 865293797u, 1015860943u},
                                    {0.5, 1429124565u, 4055082647u},
                                    {0.8, 743021791u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 485657295u, 1144044951u},
                                    {0.008333333333333333, 485657295u, 1144044951u},
                                    {0.016666666666666666, 485657295u, 1144044951u},
                                    {0.025, 485657295u, 3359810482u},
                                    {0.075, 1346618496u, 3478230612u},
                                    {0.08333333333333333, 2992119672u, 1378684782u},
                                    {0.09166666666666666, 4185890646u, 1018042172u},
                                    {0.1, 1371389077u, 3974760450u},
                                    {0.14166666666666666, 2225351365u, 1197220864u},
                                    {0.15833333333333333, 3710244605u, 287036262u},
                                    {0.16666666666666666, 4059655928u, 1939819404u},
                                    {0.175, 4072552149u, 1505259930u},
                                    {0.18333333333333332, 950867846u, 1877627953u},
                                    {0.2, 1076109954u, 220150177u},
                                    {0.20833333333333334, 2369332525u, 440835010u},
                                    {0.21666666666666667, 3555671313u, 3072819257u},
                                    {0.225, 229185718u, 3978279837u},
                                    {0.25, 4125077005u, 2677966185u},
                                    {0.3, 1186396718u, 2994375261u},
                                    {0.4166666666666667, 3971874563u, 2999206124u},
                                    {0.5, 235038506u, 1421581000u},
                                    {0.8, 676496519u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_sparks = eve::Value::fromJson(
            R"json([4278190080, 4278205813, 4278221546, 4278237279, 4278253012, 4278268745, 4278284478, 4278300211, 4278315944, 4278331677, 4278347410, 4278363143, 4278378876, 4278394609, 4278410342, 4278426075, 4278441808, 4278457541, 4278473274, 4278489007, 4278504740, 4278520473, 4278536206, 4278551939, 4278567672, 4278583405, 4278599138, 4278614871, 4278630604, 4278646337, 4278662070, 4278677803, 4278693536, 4278709269, 4278725002, 4278740735, 4278756468, 4278772201, 4278787934, 4278803667, 4278819400, 4278835133, 4278850866, 4278866599, 4278882332, 4278898065, 4278913798, 4278929531, 4278945264, 4278960997, 4278976730, 4278992463, 4279008196, 4279023929, 4279039662, 4279055395, 4279071128, 4279086861, 4279102594, 4279118327, 4279134060, 4279149793, 4279165526, 4279181259])json");
        REQUIRE(input_sparks);
        value.find("options")->set("sparks", input_sparks.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2563889221u, 2889591046u},
                                    {0.008333333333333333, 2563889221u, 2889591046u},
                                    {0.016666666666666666, 2563889221u, 2889591046u},
                                    {0.025, 2563889221u, 1860639367u},
                                    {0.075, 2563889221u, 4029520597u},
                                    {0.08333333333333333, 2563889221u, 599924114u},
                                    {0.09166666666666666, 2563889221u, 2219221946u},
                                    {0.1, 2563889221u, 568284876u},
                                    {0.14166666666666666, 2563889221u, 451252344u},
                                    {0.15833333333333333, 2563889221u, 3666566472u},
                                    {0.16666666666666666, 2563889221u, 1562805727u},
                                    {0.175, 2036222177u, 4222007995u},
                                    {0.18333333333333332, 2036222177u, 438174389u},
                                    {0.2, 2036222177u, 1390900663u},
                                    {0.20833333333333334, 2036222177u, 2580400607u},
                                    {0.21666666666666667, 1735410285u, 2938746195u},
                                    {0.225, 2333498438u, 3561605088u},
                                    {0.25, 195870512u, 448689662u},
                                    {0.3, 1947160520u, 994618519u},
                                    {0.4166666666666667, 1453510485u, 1981020169u},
                                    {0.5, 3023714302u, 728533745u},
                                    {0.8, 3661245395u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2563889221u, 4164834671u},
                                    {0.008333333333333333, 2563889221u, 4164834671u},
                                    {0.016666666666666666, 2563889221u, 4164834671u},
                                    {0.025, 2563889221u, 1128957631u},
                                    {0.075, 2563889221u, 1823290330u},
                                    {0.08333333333333333, 2563889221u, 3898077645u},
                                    {0.09166666666666666, 2563889221u, 2565482870u},
                                    {0.1, 2563889221u, 4176602739u},
                                    {0.14166666666666666, 2563889221u, 174852085u},
                                    {0.15833333333333333, 4146623705u, 54217140u},
                                    {0.16666666666666666, 4146623705u, 2833220776u},
                                    {0.175, 1018027886u, 2507873229u},
                                    {0.18333333333333332, 1018027886u, 982654038u},
                                    {0.2, 1018027886u, 3256747333u},
                                    {0.20833333333333334, 1018027886u, 1589220098u},
                                    {0.21666666666666667, 1391286834u, 1737109672u},
                                    {0.225, 2769429413u, 1733204968u},
                                    {0.25, 1012273854u, 3929276335u},
                                    {0.3, 3144997316u, 3674042251u},
                                    {0.4166666666666667, 507629193u, 1068357196u},
                                    {0.5, 935297480u, 367505814u},
                                    {0.8, 416530835u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(1.5)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 4150998489u, 3732716796u},
                                    {0.008333333333333333, 4150998489u, 3732716796u},
                                    {0.016666666666666666, 4150998489u, 3732716796u},
                                    {0.025, 4150998489u, 3644735533u},
                                    {0.075, 3333806399u, 603953936u},
                                    {0.08333333333333333, 776609450u, 603569827u},
                                    {0.09166666666666666, 103086256u, 1986651355u},
                                    {0.1, 3721898346u, 2350965016u},
                                    {0.14166666666666666, 1761009708u, 580718469u},
                                    {0.15833333333333333, 902232111u, 2719948621u},
                                    {0.16666666666666666, 857587970u, 880697082u},
                                    {0.175, 2767245721u, 3202807220u},
                                    {0.18333333333333332, 2934680305u, 2905843537u},
                                    {0.2, 1044700529u, 2499282537u},
                                    {0.20833333333333334, 1044700529u, 4045633246u},
                                    {0.21666666666666667, 2876776799u, 2494776563u},
                                    {0.225, 2880026996u, 3519824016u},
                                    {0.25, 3466028405u, 577075918u},
                                    {0.3, 1615549360u, 22813751u},
                                    {0.4166666666666667, 1983945205u, 462328937u},
                                    {0.5, 3299597294u, 4005301649u},
                                    {0.8, 1277069683u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 3417222686u, 3387479063u},
                                    {0.008333333333333333, 3417222686u, 3387479063u},
                                    {0.016666666666666666, 3417222686u, 3387479063u},
                                    {0.025, 3417222686u, 3255777682u},
                                    {0.075, 1912322301u, 960664314u},
                                    {0.08333333333333333, 1640073273u, 2985717645u},
                                    {0.09166666666666666, 3028595818u, 760433842u},
                                    {0.1, 3374882007u, 3656181530u},
                                    {0.14166666666666666, 2253687594u, 2666466564u},
                                    {0.15833333333333333, 2477254274u, 1625407662u},
                                    {0.16666666666666666, 394083623u, 650192415u},
                                    {0.175, 3489868635u, 496578u},
                                    {0.18333333333333332, 349118651u, 4244648882u},
                                    {0.2, 2191933320u, 575052616u},
                                    {0.20833333333333334, 3894565387u, 1296049929u},
                                    {0.21666666666666667, 1404750082u, 2139559864u},
                                    {0.225, 99166337u, 1440941560u},
                                    {0.25, 3971907093u, 536134927u},
                                    {0.3, 2554814772u, 1596081995u},
                                    {0.4166666666666667, 4199556009u, 404902268u},
                                    {0.5, 2801539960u, 536931622u},
                                    {0.8, 975132979u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(76.5)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 3851684496u, 1783000559u},
                                    {0.008333333333333333, 3851684496u, 1783000559u},
                                    {0.016666666666666666, 3851684496u, 1783000559u},
                                    {0.025, 3756351824u, 3236993035u},
                                    {0.075, 3725792835u, 2970955151u},
                                    {0.08333333333333333, 2854356598u, 2610871086u},
                                    {0.09166666666666666, 143903659u, 3795461384u},
                                    {0.1, 3215912377u, 1866138849u},
                                    {0.14166666666666666, 2290996137u, 572573820u},
                                    {0.15833333333333333, 1561024410u, 19178921u},
                                    {0.16666666666666666, 2857705703u, 155007527u},
                                    {0.175, 3075910596u, 1658846303u},
                                    {0.18333333333333332, 1640927327u, 762488867u},
                                    {0.2, 3058833938u, 383526738u},
                                    {0.20833333333333334, 982615096u, 3201521301u},
                                    {0.21666666666666667, 4062234794u, 3442922239u},
                                    {0.225, 840936777u, 474853032u},
                                    {0.25, 3824022779u, 2580809054u},
                                    {0.3, 3348175918u, 2329656187u},
                                    {0.4166666666666667, 2799218417u, 3989441655u},
                                    {0.5, 3455924510u, 2562460278u},
                                    {0.8, 1384386547u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(128)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 4092754234u, 3401960486u},
                                    {0.008333333333333333, 4092754234u, 3401960486u},
                                    {0.016666666666666666, 4092754234u, 3401960486u},
                                    {0.025, 2077236182u, 1912932039u},
                                    {0.075, 1378513474u, 1988822884u},
                                    {0.08333333333333333, 378276837u, 2932581463u},
                                    {0.09166666666666666, 3659377295u, 263197011u},
                                    {0.1, 2027337732u, 2780570965u},
                                    {0.14166666666666666, 2680569684u, 2366452332u},
                                    {0.15833333333333333, 4123111859u, 3095223749u},
                                    {0.16666666666666666, 3193820374u, 3095223749u},
                                    {0.175, 3799325240u, 3095223749u},
                                    {0.18333333333333332, 2422225668u, 3095223749u},
                                    {0.2, 1760229933u, 3095223749u},
                                    {0.20833333333333334, 1760229933u, 3095223749u},
                                    {0.21666666666666667, 3101254133u, 3095223749u},
                                    {0.225, 1313707381u, 3095223749u},
                                    {0.25, 1588487733u, 3095223749u},
                                    {0.3, 3322945613u, 3095223749u},
                                    {0.4166666666666667, 3095223749u, 3095223749u},
                                    {0.5, 3095223749u, 3095223749u},
                                    {0.8, 3095223749u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(256)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 3612590816u, 3095223749u},
                                    {0.008333333333333333, 3612590816u, 3095223749u},
                                    {0.016666666666666666, 3612590816u, 3095223749u},
                                    {0.025, 195651913u, 3095223749u},
                                    {0.075, 980101360u, 3095223749u},
                                    {0.08333333333333333, 1723205968u, 3095223749u},
                                    {0.09166666666666666, 3496789816u, 3095223749u},
                                    {0.1, 3546007712u, 3095223749u},
                                    {0.14166666666666666, 516416522u, 3095223749u},
                                    {0.15833333333333333, 4287696288u, 3095223749u},
                                    {0.16666666666666666, 3132595312u, 3095223749u},
                                    {0.175, 3132595312u, 3095223749u},
                                    {0.18333333333333332, 3132595312u, 3095223749u},
                                    {0.2, 3132595312u, 3095223749u},
                                    {0.20833333333333334, 3132595312u, 3095223749u},
                                    {0.21666666666666667, 803966416u, 3095223749u},
                                    {0.225, 803966416u, 3095223749u},
                                    {0.25, 803966416u, 3095223749u},
                                    {0.3, 2716668448u, 3095223749u},
                                    {0.4166666666666667, 3095223749u, 3095223749u},
                                    {0.5, 3095223749u, 3095223749u},
                                    {0.8, 3095223749u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        REQUIRE(fx.isSimulated());
        auto value        = fx.configurationValue();
        auto input_length = eve::Value::fromJson(R"json(512)json");
        REQUIRE(input_length);
        value.find("options")->set("length", input_length.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
TEST_CASE("stylize.pixelfx.melee.whipCrackPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 4076342217u, 3856125171u},
                                    {0.225, 3044724622u, 934622352u},
                                    {0.25, 3663283792u, 3066869454u},
                                    {0.3, 3627338068u, 1455232055u},
                                    {0.4166666666666667, 3395410421u, 2131854441u},
                                    {0.5, 3808073710u, 2861129105u},
                                    {0.8, 635507059u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 2171643979u, 2037911389u},
                                    {0.225, 2125791698u, 90833087u},
                                    {0.25, 4272327447u, 3207682953u},
                                    {0.3, 4289959251u, 2245365508u},
                                    {0.4166666666666667, 1408123828u, 1668951020u},
                                    {0.5, 3433818175u, 3550902351u},
                                    {0.8, 3884307543u, 3095223749u},
                                    {0.85, 4068984585u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 4076342217u, 3856125171u},
                                    {0.225, 3044724622u, 934622352u},
                                    {0.25, 3663283792u, 3066869454u},
                                    {0.3, 3627338068u, 1455232055u},
                                    {0.4166666666666667, 3395410421u, 2131854441u},
                                    {0.5, 3808073710u, 2861129105u},
                                    {0.8, 635507059u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
        constexpr Frame frames[] = {{0, 2206436460u, 192730150u},
                                    {0.008333333333333333, 2206436460u, 192730150u},
                                    {0.016666666666666666, 2206436460u, 192730150u},
                                    {0.025, 2206436460u, 3374451587u},
                                    {0.075, 593914748u, 2572495059u},
                                    {0.08333333333333333, 3361822878u, 868188770u},
                                    {0.09166666666666666, 953679603u, 2798832412u},
                                    {0.1, 2915612137u, 1048088858u},
                                    {0.14166666666666666, 574836641u, 2350594213u},
                                    {0.15833333333333333, 831985366u, 3433412492u},
                                    {0.16666666666666666, 926843311u, 2916207797u},
                                    {0.175, 4164103562u, 1465435984u},
                                    {0.18333333333333332, 1848985315u, 3247584658u},
                                    {0.2, 1855711107u, 4074981086u},
                                    {0.20833333333333334, 186432769u, 215010505u},
                                    {0.21666666666666667, 4076342217u, 3856125171u},
                                    {0.225, 3044724622u, 934622352u},
                                    {0.25, 3663283792u, 3066869454u},
                                    {0.3, 3627338068u, 1455232055u},
                                    {0.4166666666666667, 3395410421u, 2131854441u},
                                    {0.5, 3808073710u, 2861129105u},
                                    {0.8, 635507059u, 3095223749u},
                                    {0.85, 3095223749u, 3095223749u},
                                    {1, 3095223749u, 3095223749u},
                                    {5, 3095223749u, 3095223749u}};
        PixelFx         fx(PixelFxKind::WhipCrack);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::WhipCrack, value);
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
TEST_CASE("stylize.pixelfx.melee.thirdReverseSeekLayersPurityAndStopRestore") {
    for (auto kind : {PixelFxKind::Uppercut, PixelFxKind::SpinSlash, PixelFxKind::WhipCrack}) {
        PixelFx  fx(kind);
        uint32_t originalColor = 0, originalLight = 0;
        for (double t :
             {.13, 5., .02, .075, .08333333333333333, .16, .175, .21, .21666666666666667, .25, .5, .13, 0., .13}) {
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
TEST_CASE("stylize.pixelfx.melee.thirdPartitionedClocksPreserveEmissionOrder") {
    for (auto kind : {PixelFxKind::Uppercut, PixelFxKind::SpinSlash, PixelFxKind::WhipCrack})
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
TEST_CASE("stylize.pixelfx.melee.thirdMaterialOwnershipRoundTripBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::Uppercut, PixelFxKind::SpinSlash, PixelFxKind::WhipCrack}) {
        PixelFx fx(kind);
        auto    s      = fx.settings();
        s.melee.sparks = {0xff123456u};
        auto candidate = PixelFx::prepareConfiguration(kind, s);
        REQUIRE(candidate);
        s.melee.sparks.clear();
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().melee.sparks.size() == 4);
        for (auto c : fx.settings().melee.sparks) REQUIRE(c == 0xff123456u);
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
            bad.melee.sparks.assign(count, 0xff123456u);
            REQUIRE(!PixelFx::prepareConfiguration(kind, bad));
        }
        auto bad         = fx.settings();
        bad.melee.sparks = {0x80123456u};
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
TEST_CASE("stylize.pixelfx.melee.thirdPinnedStaticStockBackBodyFrontComposition") {
    struct Frame {
        PixelFxKind kind;
        int         actor;
        double      time;
        uint32_t    color, light;
    };
    constexpr Frame frames[] = {
        {PixelFxKind::Uppercut, 1, 0, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 1, 0.008333333333333333, 3601074170u, 792346053u},
        {PixelFxKind::Uppercut, 1, 0.016666666666666666, 2834021084u, 792346053u},
        {PixelFxKind::Uppercut, 1, 0.025, 2511746309u, 1791006688u},
        {PixelFxKind::Uppercut, 1, 0.075, 3845116030u, 3693235263u},
        {PixelFxKind::Uppercut, 1, 0.08333333333333333, 2987309008u, 2409387895u},
        {PixelFxKind::Uppercut, 1, 0.09166666666666666, 3800282129u, 4075207705u},
        {PixelFxKind::Uppercut, 1, 0.1, 3507207878u, 2222566232u},
        {PixelFxKind::Uppercut, 1, 0.15, 2606562832u, 320897961u},
        {PixelFxKind::Uppercut, 1, 0.2, 3574633388u, 370493978u},
        {PixelFxKind::Uppercut, 1, 0.3, 1046153448u, 567790809u},
        {PixelFxKind::Uppercut, 1, 0.5, 1264214324u, 3159314493u},
        {PixelFxKind::Uppercut, 1, 0.8416666666666667, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 1, 0.85, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 1, 0.8583333333333333, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 1, 1, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 1, 5, 2919011476u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0, 3562271343u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0.008333333333333333, 2028269273u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0.016666666666666666, 1098154583u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0.025, 3277490477u, 1791006688u},
        {PixelFxKind::Uppercut, 2, 0.075, 3375663573u, 3693235263u},
        {PixelFxKind::Uppercut, 2, 0.08333333333333333, 2721568827u, 2409387895u},
        {PixelFxKind::Uppercut, 2, 0.09166666666666666, 2736507958u, 4075207705u},
        {PixelFxKind::Uppercut, 2, 0.1, 3107190803u, 2222566232u},
        {PixelFxKind::Uppercut, 2, 0.15, 757494133u, 320897961u},
        {PixelFxKind::Uppercut, 2, 0.2, 1430410240u, 370493978u},
        {PixelFxKind::Uppercut, 2, 0.3, 3554543861u, 567790809u},
        {PixelFxKind::Uppercut, 2, 0.5, 3589370071u, 3159314493u},
        {PixelFxKind::Uppercut, 2, 0.8416666666666667, 3562271343u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0.85, 3562271343u, 792346053u},
        {PixelFxKind::Uppercut, 2, 0.8583333333333333, 3562271343u, 792346053u},
        {PixelFxKind::Uppercut, 2, 1, 3562271343u, 792346053u},
        {PixelFxKind::Uppercut, 2, 5, 3562271343u, 792346053u},
        {PixelFxKind::SpinSlash, 1, 0, 2729587988u, 2698056993u},
        {PixelFxKind::SpinSlash, 1, 0.008333333333333333, 834647553u, 436553446u},
        {PixelFxKind::SpinSlash, 1, 0.058333333333333334, 4201879041u, 3245859135u},
        {PixelFxKind::SpinSlash, 1, 0.1, 1627519522u, 3908297732u},
        {PixelFxKind::SpinSlash, 1, 0.14166666666666666, 4280355774u, 111234418u},
        {PixelFxKind::SpinSlash, 1, 0.15833333333333333, 1577404064u, 564577782u},
        {PixelFxKind::SpinSlash, 1, 0.16666666666666666, 1308397276u, 2713377091u},
        {PixelFxKind::SpinSlash, 1, 0.175, 3688317084u, 326344850u},
        {PixelFxKind::SpinSlash, 1, 0.18333333333333332, 1793971846u, 855503958u},
        {PixelFxKind::SpinSlash, 1, 0.2, 2270887822u, 1795641625u},
        {PixelFxKind::SpinSlash, 1, 0.3, 3015946408u, 373900448u},
        {PixelFxKind::SpinSlash, 1, 0.5, 1909954427u, 1340221067u},
        {PixelFxKind::SpinSlash, 1, 0.7916666666666666, 2051892052u, 2311800261u},
        {PixelFxKind::SpinSlash, 1, 0.8, 2051892052u, 2311800261u},
        {PixelFxKind::SpinSlash, 1, 0.8083333333333333, 2051892052u, 2311800261u},
        {PixelFxKind::SpinSlash, 1, 1, 2051892052u, 2311800261u},
        {PixelFxKind::SpinSlash, 1, 5, 2051892052u, 2311800261u},
        {PixelFxKind::SpinSlash, 2, 0, 743888743u, 2698056993u},
        {PixelFxKind::SpinSlash, 2, 0.008333333333333333, 2130165886u, 436553446u},
        {PixelFxKind::SpinSlash, 2, 0.058333333333333334, 486263201u, 3245859135u},
        {PixelFxKind::SpinSlash, 2, 0.1, 456919944u, 3908297732u},
        {PixelFxKind::SpinSlash, 2, 0.14166666666666666, 886202627u, 111234418u},
        {PixelFxKind::SpinSlash, 2, 0.15833333333333333, 1393011000u, 564577782u},
        {PixelFxKind::SpinSlash, 2, 0.16666666666666666, 1510508665u, 2713377091u},
        {PixelFxKind::SpinSlash, 2, 0.175, 2228513853u, 326344850u},
        {PixelFxKind::SpinSlash, 2, 0.18333333333333332, 2383361763u, 855503958u},
        {PixelFxKind::SpinSlash, 2, 0.2, 2455363525u, 1795641625u},
        {PixelFxKind::SpinSlash, 2, 0.3, 2743773612u, 373900448u},
        {PixelFxKind::SpinSlash, 2, 0.5, 1930361021u, 1340221067u},
        {PixelFxKind::SpinSlash, 2, 0.7916666666666666, 2902832367u, 2311800261u},
        {PixelFxKind::SpinSlash, 2, 0.8, 2902832367u, 2311800261u},
        {PixelFxKind::SpinSlash, 2, 0.8083333333333333, 2902832367u, 2311800261u},
        {PixelFxKind::SpinSlash, 2, 1, 2902832367u, 2311800261u},
        {PixelFxKind::SpinSlash, 2, 5, 2902832367u, 2311800261u},
        {PixelFxKind::WhipCrack, 1, 0, 1416118150u, 192730150u},
        {PixelFxKind::WhipCrack, 1, 0.008333333333333333, 1416118150u, 192730150u},
        {PixelFxKind::WhipCrack, 1, 0.016666666666666666, 1416118150u, 192730150u},
        {PixelFxKind::WhipCrack, 1, 0.025, 1416118150u, 3374451587u},
        {PixelFxKind::WhipCrack, 1, 0.15833333333333333, 889509035u, 3433412492u},
        {PixelFxKind::WhipCrack, 1, 0.16666666666666666, 1338872282u, 2916207797u},
        {PixelFxKind::WhipCrack, 1, 0.2, 2051749050u, 4074981086u},
        {PixelFxKind::WhipCrack, 1, 0.20833333333333334, 96415336u, 215010505u},
        {PixelFxKind::WhipCrack, 1, 0.21666666666666667, 1369860612u, 3856125171u},
        {PixelFxKind::WhipCrack, 1, 0.225, 2064391887u, 934622352u},
        {PixelFxKind::WhipCrack, 1, 0.25, 2719601945u, 3066869454u},
        {PixelFxKind::WhipCrack, 1, 0.3, 3502109485u, 1455232055u},
        {PixelFxKind::WhipCrack, 1, 0.4166666666666667, 3263146052u, 2131854441u},
        {PixelFxKind::WhipCrack, 1, 0.8416666666666667, 211714740u, 3095223749u},
        {PixelFxKind::WhipCrack, 1, 0.85, 211714740u, 3095223749u},
        {PixelFxKind::WhipCrack, 1, 0.8583333333333333, 211714740u, 3095223749u},
        {PixelFxKind::WhipCrack, 1, 5, 211714740u, 3095223749u},
        {PixelFxKind::WhipCrack, 2, 0, 3715458001u, 192730150u},
        {PixelFxKind::WhipCrack, 2, 0.008333333333333333, 3715458001u, 192730150u},
        {PixelFxKind::WhipCrack, 2, 0.016666666666666666, 3715458001u, 192730150u},
        {PixelFxKind::WhipCrack, 2, 0.025, 3715458001u, 3374451587u},
        {PixelFxKind::WhipCrack, 2, 0.15833333333333333, 3614477024u, 3433412492u},
        {PixelFxKind::WhipCrack, 2, 0.16666666666666666, 3778354369u, 2916207797u},
        {PixelFxKind::WhipCrack, 2, 0.2, 3976282429u, 4074981086u},
        {PixelFxKind::WhipCrack, 2, 0.20833333333333334, 281435607u, 215010505u},
        {PixelFxKind::WhipCrack, 2, 0.21666666666666667, 1864179623u, 3856125171u},
        {PixelFxKind::WhipCrack, 2, 0.225, 599859872u, 934622352u},
        {PixelFxKind::WhipCrack, 2, 0.25, 3833633342u, 3066869454u},
        {PixelFxKind::WhipCrack, 2, 0.3, 2649960618u, 1455232055u},
        {PixelFxKind::WhipCrack, 2, 0.4166666666666667, 1173148583u, 2131854441u},
        {PixelFxKind::WhipCrack, 2, 0.8416666666666667, 2179678319u, 3095223749u},
        {PixelFxKind::WhipCrack, 2, 0.85, 2179678319u, 3095223749u},
        {PixelFxKind::WhipCrack, 2, 0.8583333333333333, 2179678319u, 3095223749u},
        {PixelFxKind::WhipCrack, 2, 5, 2179678319u, 3095223749u},
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
TEST_CASE("stylize.pixelfx.melee.thirdGeometryBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::SpinSlash, PixelFxKind::WhipCrack}) {
        PixelFx    fx(kind);
        const auto before = fx.configurationValue();
        for (double invalid :
             {-.1, 512.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
            auto s = fx.settings();
            if (kind == PixelFxKind::SpinSlash)
                s.melee.radius = invalid;
            else
                s.melee.length = invalid;
            REQUIRE(!PixelFx::prepareConfiguration(kind, s));
            REQUIRE(fx.configurationValue() == before);
        }
        for (double v : {1., 1.5, 12., 128., 256., 512.}) {
            auto s = fx.settings();
            if (kind == PixelFxKind::SpinSlash)
                s.melee.radius = v;
            else
                s.melee.length = v;
            REQUIRE(PixelFx::prepareConfiguration(kind, s));
        }
    }
    PixelFx whip(PixelFxKind::WhipCrack);
    for (double v : {0., .5, .999}) {
        auto s         = whip.settings();
        s.melee.length = v;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::WhipCrack, s));
    }
    PixelFx spin(PixelFxKind::SpinSlash);
    for (double v : {0., .5}) {
        auto s         = spin.settings();
        s.melee.radius = v;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::SpinSlash, s));
    }
}
}  // namespace test_pixelfx_melee_ring_whip_unit
