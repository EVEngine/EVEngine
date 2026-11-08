// Pinned upstream melee geometry and raster fixtures; no runtime source interpreter required.
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/MeleeSwing.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_melee_heavy_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.melee.scytheReapPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 3365443235u, 2702031552u},
                                    {0.08333333333333333, 322609031u, 3060510409u},
                                    {0.14166666666666666, 250861627u, 3218790604u},
                                    {0.15, 978091292u, 2077822993u},
                                    {0.15833333333333333, 3760797274u, 1153442839u},
                                    {0.16666666666666666, 1190435416u, 3833375344u},
                                    {0.2, 105287780u, 2805171327u},
                                    {0.20833333333333334, 4102033816u, 298182915u},
                                    {0.325, 1487120267u, 1064244214u},
                                    {0.3333333333333333, 2511723678u, 2923224728u},
                                    {0.3416666666666667, 3111822150u, 2120256266u},
                                    {0.35, 681936778u, 1294055201u},
                                    {0.4083333333333333, 3215248230u, 4106824983u},
                                    {0.4166666666666667, 2557191001u, 3740277928u},
                                    {0.425, 3285720769u, 856624517u},
                                    {0.55, 4233397209u, 2295594255u},
                                    {0.5583333333333333, 1885113209u, 1912261786u},
                                    {0.5666666666666667, 952795156u, 999140840u},
                                    {0.8333333333333334, 484960249u, 557039795u},
                                    {1.1083333333333334, 493849066u, 985325863u},
                                    {1.1166666666666667, 2563976848u, 569910389u},
                                    {1.125, 4167729614u, 1872010325u},
                                    {1.5, 368315603u, 4062361212u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 1221011937u, 90830385u},
                                    {0.075, 1867610521u, 277495363u},
                                    {0.08333333333333333, 2587625141u, 938902780u},
                                    {0.14166666666666666, 2561497164u, 1059835918u},
                                    {0.15, 4060364287u, 726692111u},
                                    {0.15833333333333333, 1741734258u, 1431928668u},
                                    {0.16666666666666666, 1508227766u, 4257220217u},
                                    {0.2, 3020019396u, 1110893532u},
                                    {0.20833333333333334, 2076236707u, 964615945u},
                                    {0.325, 1041705912u, 3779347858u},
                                    {0.3333333333333333, 323207221u, 1271033632u},
                                    {0.3416666666666667, 3119087365u, 1693399827u},
                                    {0.35, 2003695244u, 4104759100u},
                                    {0.4083333333333333, 1030680513u, 2208325008u},
                                    {0.4166666666666667, 599137154u, 2903747340u},
                                    {0.425, 3602580559u, 1365888391u},
                                    {0.55, 1114919054u, 2271180115u},
                                    {0.5583333333333333, 149119069u, 166297119u},
                                    {0.5666666666666667, 3378442052u, 3998895318u},
                                    {0.8333333333333334, 2896572234u, 3904870926u},
                                    {1.1083333333333334, 2576933744u, 3983352594u},
                                    {1.1166666666666667, 2042955434u, 1492215504u},
                                    {1.125, 867738672u, 2436120482u},
                                    {1.5, 4140842758u, 250454403u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 126587031u, 4125595547u},
                                    {0.08333333333333333, 1616326264u, 3260687457u},
                                    {0.14166666666666666, 3296792040u, 3869573167u},
                                    {0.15, 279769906u, 1497692554u},
                                    {0.15833333333333333, 619665246u, 1749337101u},
                                    {0.16666666666666666, 2329960300u, 2865378078u},
                                    {0.2, 2255953466u, 4142522101u},
                                    {0.20833333333333334, 2267301432u, 3797884217u},
                                    {0.325, 1304599248u, 3474011196u},
                                    {0.3333333333333333, 2204228931u, 3603012235u},
                                    {0.3416666666666667, 818583392u, 26650384u},
                                    {0.35, 4162696676u, 934990566u},
                                    {0.4083333333333333, 751044784u, 276994339u},
                                    {0.4166666666666667, 1519571963u, 788033102u},
                                    {0.425, 3001193308u, 3832278770u},
                                    {0.55, 2311190888u, 3848492275u},
                                    {0.5583333333333333, 698561731u, 1456449463u},
                                    {0.5666666666666667, 466748913u, 139122189u},
                                    {0.8333333333333334, 2557402551u, 1757324143u},
                                    {1.1083333333333334, 466766627u, 3755380979u},
                                    {1.1166666666666667, 1043817747u, 3105924932u},
                                    {1.125, 2040295761u, 4057421638u},
                                    {1.5, 1228037745u, 3921794789u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 85516222u, 2039759766u},
                                    {0.08333333333333333, 2049808201u, 771835282u},
                                    {0.14166666666666666, 3022408787u, 3755002161u},
                                    {0.15, 269099872u, 2182188495u},
                                    {0.15833333333333333, 3962861304u, 2323127180u},
                                    {0.16666666666666666, 496591730u, 63906647u},
                                    {0.2, 4292579362u, 1665892840u},
                                    {0.20833333333333334, 3905225422u, 3820132471u},
                                    {0.325, 3840730050u, 3839820420u},
                                    {0.3333333333333333, 765179373u, 3203917585u},
                                    {0.3416666666666667, 3839913552u, 439127491u},
                                    {0.35, 2391232709u, 3391537158u},
                                    {0.4083333333333333, 3071328952u, 1712257248u},
                                    {0.4166666666666667, 2701346177u, 662276411u},
                                    {0.425, 1195162682u, 693212330u},
                                    {0.55, 2200858404u, 960786622u},
                                    {0.5583333333333333, 134097455u, 280536930u},
                                    {0.5666666666666667, 1071079927u, 1042648133u},
                                    {0.8333333333333334, 1137384013u, 1236152688u},
                                    {1.1083333333333334, 2080654396u, 1875873638u},
                                    {1.1166666666666667, 4087296348u, 2806763803u},
                                    {1.125, 3104597167u, 1970662112u},
                                    {1.5, 2636979325u, 1460951444u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 1692144657u, 1281944344u},
                                    {0.075, 3686464099u, 2509003330u},
                                    {0.08333333333333333, 3485334816u, 2554459513u},
                                    {0.14166666666666666, 1155388476u, 3266777326u},
                                    {0.15, 4096768629u, 3169730819u},
                                    {0.15833333333333333, 2895840956u, 2870651887u},
                                    {0.16666666666666666, 3876242859u, 3214820807u},
                                    {0.2, 2884598878u, 991763351u},
                                    {0.20833333333333334, 4051850170u, 3912416388u},
                                    {0.325, 1383269855u, 3049587642u},
                                    {0.3333333333333333, 2250878935u, 2040893199u},
                                    {0.3416666666666667, 1544733099u, 3253139726u},
                                    {0.35, 2236099945u, 1134587844u},
                                    {0.4083333333333333, 2784491833u, 754394822u},
                                    {0.4166666666666667, 2362710763u, 164573507u},
                                    {0.425, 3739137262u, 1367699048u},
                                    {0.55, 429622792u, 3978899061u},
                                    {0.5583333333333333, 473338182u, 2040879114u},
                                    {0.5666666666666667, 1661802611u, 1109207756u},
                                    {0.8333333333333334, 2362057087u, 1180685692u},
                                    {1.1083333333333334, 3540729218u, 4053115457u},
                                    {1.1166666666666667, 1972498175u, 1868329009u},
                                    {1.125, 681641532u, 212910952u},
                                    {1.5, 1780069906u, 3599941399u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 870802337u, 1046025244u},
                                    {0.075, 3503849602u, 755406508u},
                                    {0.08333333333333333, 3333796794u, 2028113705u},
                                    {0.14166666666666666, 322180238u, 453591136u},
                                    {0.15, 1349908600u, 188802561u},
                                    {0.15833333333333333, 1333466992u, 1906794502u},
                                    {0.16666666666666666, 3584291806u, 762391591u},
                                    {0.2, 2034874843u, 4015364663u},
                                    {0.20833333333333334, 2890256602u, 3827632110u},
                                    {0.325, 3153846726u, 542985428u},
                                    {0.3333333333333333, 309951203u, 2166594837u},
                                    {0.3416666666666667, 3818312861u, 1607176400u},
                                    {0.35, 111063570u, 2444387841u},
                                    {0.4083333333333333, 1996553746u, 934813426u},
                                    {0.4166666666666667, 2404720147u, 2529085973u},
                                    {0.425, 689763747u, 2139957660u},
                                    {0.55, 3641669558u, 576974749u},
                                    {0.5583333333333333, 3008217767u, 2329801068u},
                                    {0.5666666666666667, 1962835637u, 836812498u},
                                    {0.8333333333333334, 503129999u, 1720383779u},
                                    {1.1083333333333334, 3073348873u, 425488136u},
                                    {1.1166666666666667, 962241839u, 4005440460u},
                                    {1.125, 549240195u, 3746812763u},
                                    {1.5, 536334853u, 835013949u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
TEST_CASE("stylize.pixelfx.melee.scytheReapPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 3365443235u, 2702031552u},
                                    {0.08333333333333333, 322609031u, 3060510409u},
                                    {0.14166666666666666, 250861627u, 3218790604u},
                                    {0.15, 978091292u, 2077822993u},
                                    {0.15833333333333333, 3760797274u, 1153442839u},
                                    {0.16666666666666666, 1190435416u, 3833375344u},
                                    {0.2, 105287780u, 2805171327u},
                                    {0.20833333333333334, 4102033816u, 298182915u},
                                    {0.325, 1487120267u, 1064244214u},
                                    {0.3333333333333333, 2511723678u, 2923224728u},
                                    {0.3416666666666667, 3111822150u, 2120256266u},
                                    {0.35, 681936778u, 1294055201u},
                                    {0.4083333333333333, 3215248230u, 4106824983u},
                                    {0.4166666666666667, 2557191001u, 3740277928u},
                                    {0.425, 3285720769u, 856624517u},
                                    {0.55, 4233397209u, 2295594255u},
                                    {0.5583333333333333, 1885113209u, 1912261786u},
                                    {0.5666666666666667, 952795156u, 999140840u},
                                    {0.8333333333333334, 484960249u, 557039795u},
                                    {1.1083333333333334, 493849066u, 985325863u},
                                    {1.1166666666666667, 2563976848u, 569910389u},
                                    {1.125, 4167729614u, 1872010325u},
                                    {1.5, 368315603u, 4062361212u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 11244177u, 3367013391u},
                                    {0.08333333333333333, 3413828988u, 364324676u},
                                    {0.14166666666666666, 3525513206u, 1811598045u},
                                    {0.15, 271660834u, 2201603442u},
                                    {0.15833333333333333, 289600951u, 1058343358u},
                                    {0.16666666666666666, 1942645840u, 3683373417u},
                                    {0.2, 2059621244u, 4101692586u},
                                    {0.20833333333333334, 3150371406u, 4190288166u},
                                    {0.325, 2677820427u, 3714150709u},
                                    {0.3333333333333333, 1833015870u, 3062004948u},
                                    {0.3416666666666667, 3219784470u, 1015989104u},
                                    {0.35, 3925361259u, 1715930647u},
                                    {0.4083333333333333, 97880516u, 3669497108u},
                                    {0.4166666666666667, 3088277819u, 2303317965u},
                                    {0.425, 2207214287u, 2072628116u},
                                    {0.55, 2422765908u, 2860913893u},
                                    {0.5583333333333333, 1907731102u, 936309079u},
                                    {0.5666666666666667, 2250001752u, 4083304395u},
                                    {0.8333333333333334, 3785658464u, 2642318426u},
                                    {1.1083333333333334, 1030025464u, 3146970872u},
                                    {1.1166666666666667, 2769936874u, 3284750945u},
                                    {1.125, 2150505538u, 1559549166u},
                                    {1.5, 2166594148u, 8306438u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 3365443235u, 2702031552u},
                                    {0.08333333333333333, 322609031u, 3060510409u},
                                    {0.14166666666666666, 250861627u, 3218790604u},
                                    {0.15, 978091292u, 2077822993u},
                                    {0.15833333333333333, 3760797274u, 1153442839u},
                                    {0.16666666666666666, 1190435416u, 3833375344u},
                                    {0.2, 105287780u, 2805171327u},
                                    {0.20833333333333334, 4102033816u, 298182915u},
                                    {0.325, 1487120267u, 1064244214u},
                                    {0.3333333333333333, 2511723678u, 2923224728u},
                                    {0.3416666666666667, 3111822150u, 2120256266u},
                                    {0.35, 681936778u, 1294055201u},
                                    {0.4083333333333333, 3215248230u, 4106824983u},
                                    {0.4166666666666667, 2557191001u, 3740277928u},
                                    {0.425, 3285720769u, 856624517u},
                                    {0.55, 4233397209u, 2295594255u},
                                    {0.5583333333333333, 1885113209u, 1912261786u},
                                    {0.5666666666666667, 952795156u, 999140840u},
                                    {0.8333333333333334, 484960249u, 557039795u},
                                    {1.1083333333333334, 493849066u, 985325863u},
                                    {1.1166666666666667, 2563976848u, 569910389u},
                                    {1.125, 4167729614u, 1872010325u},
                                    {1.5, 368315603u, 4062361212u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
        constexpr Frame frames[] = {{0, 1893850053u, 1893850053u},
                                    {0.008333333333333333, 4255965585u, 90298509u},
                                    {0.075, 3365443235u, 2702031552u},
                                    {0.08333333333333333, 322609031u, 3060510409u},
                                    {0.14166666666666666, 250861627u, 3218790604u},
                                    {0.15, 978091292u, 2077822993u},
                                    {0.15833333333333333, 3760797274u, 1153442839u},
                                    {0.16666666666666666, 1190435416u, 3833375344u},
                                    {0.2, 105287780u, 2805171327u},
                                    {0.20833333333333334, 4102033816u, 298182915u},
                                    {0.325, 1487120267u, 1064244214u},
                                    {0.3333333333333333, 2511723678u, 2923224728u},
                                    {0.3416666666666667, 3111822150u, 2120256266u},
                                    {0.35, 681936778u, 1294055201u},
                                    {0.4083333333333333, 3215248230u, 4106824983u},
                                    {0.4166666666666667, 2557191001u, 3740277928u},
                                    {0.425, 3285720769u, 856624517u},
                                    {0.55, 4233397209u, 2295594255u},
                                    {0.5583333333333333, 1885113209u, 1912261786u},
                                    {0.5666666666666667, 952795156u, 999140840u},
                                    {0.8333333333333334, 484960249u, 557039795u},
                                    {1.1083333333333334, 493849066u, 985325863u},
                                    {1.1166666666666667, 2563976848u, 569910389u},
                                    {1.125, 4167729614u, 1872010325u},
                                    {1.5, 368315603u, 4062361212u},
                                    {2, 1893850053u, 1893850053u},
                                    {5, 1893850053u, 1893850053u}};
        PixelFx         fx(PixelFxKind::ScytheReap);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::ScytheReap, value);
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
TEST_CASE("stylize.pixelfx.melee.heavyCleavePinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 2479476784u, 1695396651u},
                                    {0.35, 3040848821u, 1113018944u},
                                    {0.4083333333333333, 2175474000u, 3231082268u},
                                    {0.4166666666666667, 2640255938u, 1851808077u},
                                    {0.425, 839778661u, 3717156759u},
                                    {0.55, 2055681902u, 2513446315u},
                                    {0.5583333333333333, 3327439338u, 1047772655u},
                                    {0.5666666666666667, 4103451858u, 3390920005u},
                                    {0.8333333333333334, 1016533241u, 3358450260u},
                                    {1.1083333333333334, 2222126625u, 740024887u},
                                    {1.1166666666666667, 1053933786u, 601894631u},
                                    {1.125, 3272182996u, 3876838973u},
                                    {1.5, 93357284u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 620461729u, 3058194503u},
                                    {0.008333333333333333, 3622587841u, 176838006u},
                                    {0.075, 2806048277u, 4220178092u},
                                    {0.08333333333333333, 1582266092u, 2785233508u},
                                    {0.14166666666666666, 3979257777u, 2270926748u},
                                    {0.15, 3675069483u, 4050419163u},
                                    {0.15833333333333333, 1111226356u, 1066022801u},
                                    {0.16666666666666666, 1838281050u, 2571465271u},
                                    {0.2, 2859577009u, 2973259623u},
                                    {0.20833333333333334, 70111857u, 2744147329u},
                                    {0.325, 3831226228u, 2120728373u},
                                    {0.3333333333333333, 2647530273u, 3101964103u},
                                    {0.3416666666666667, 644817779u, 3654438962u},
                                    {0.35, 1647259452u, 3207344101u},
                                    {0.4083333333333333, 3375363696u, 3510942266u},
                                    {0.4166666666666667, 2852855798u, 62675195u},
                                    {0.425, 1128135175u, 1432371445u},
                                    {0.55, 2588654389u, 2994561844u},
                                    {0.5583333333333333, 1807542447u, 3467365235u},
                                    {0.5666666666666667, 1932983328u, 2933745928u},
                                    {0.8333333333333334, 284308248u, 3281001558u},
                                    {1.1083333333333334, 3108698850u, 1384668003u},
                                    {1.1166666666666667, 836132558u, 1121821851u},
                                    {1.125, 3998321617u, 1069876436u},
                                    {1.5, 3610826708u, 1459766523u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_stone = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 1788040313u, 4043421640u},
                                    {0.075, 2978081087u, 1034057157u},
                                    {0.08333333333333333, 3129412514u, 2262856209u},
                                    {0.14166666666666666, 478726722u, 580759113u},
                                    {0.15, 2824522632u, 689536995u},
                                    {0.15833333333333333, 1940241348u, 1632145446u},
                                    {0.16666666666666666, 1940713083u, 4128715839u},
                                    {0.2, 3212824790u, 3947661947u},
                                    {0.20833333333333334, 3181194819u, 3606831650u},
                                    {0.325, 2127749763u, 4238777303u},
                                    {0.3333333333333333, 214371170u, 860525754u},
                                    {0.3416666666666667, 2781741279u, 2154927450u},
                                    {0.35, 4032603528u, 700943398u},
                                    {0.4083333333333333, 787075542u, 420859009u},
                                    {0.4166666666666667, 1281595854u, 3329911937u},
                                    {0.425, 2136776847u, 3444253946u},
                                    {0.55, 549812883u, 2228282427u},
                                    {0.5583333333333333, 32854283u, 3584012649u},
                                    {0.5666666666666667, 3561117199u, 21854908u},
                                    {0.8333333333333334, 802383810u, 1905582200u},
                                    {1.1083333333333334, 1764175783u, 3887833396u},
                                    {1.1166666666666667, 833727070u, 2045478676u},
                                    {1.125, 2514960148u, 1471444217u},
                                    {1.5, 240099772u, 3170960052u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 1788040313u, 4043421640u},
                                    {0.075, 2376016021u, 3480756806u},
                                    {0.08333333333333333, 2008106097u, 912949766u},
                                    {0.14166666666666666, 439989836u, 3225666859u},
                                    {0.15, 2851034701u, 4100549411u},
                                    {0.15833333333333333, 298609310u, 1293832346u},
                                    {0.16666666666666666, 2700414186u, 2380224962u},
                                    {0.2, 2181708645u, 3145735885u},
                                    {0.20833333333333334, 2694236301u, 3045309642u},
                                    {0.325, 1567436965u, 3639789044u},
                                    {0.3333333333333333, 793243866u, 772415238u},
                                    {0.3416666666666667, 3618427465u, 1286013661u},
                                    {0.35, 317739110u, 495524897u},
                                    {0.4083333333333333, 885797024u, 549406756u},
                                    {0.4166666666666667, 2880810093u, 815813083u},
                                    {0.425, 864440750u, 1840693483u},
                                    {0.55, 1037449472u, 3778938511u},
                                    {0.5583333333333333, 177522326u, 1382153827u},
                                    {0.5666666666666667, 2199257705u, 3972682435u},
                                    {0.8333333333333334, 1869192358u, 1617515509u},
                                    {1.1083333333333334, 1972170300u, 789274868u},
                                    {1.1166666666666667, 277052107u, 2640840134u},
                                    {1.125, 2410730816u, 258971424u},
                                    {1.5, 1124694124u, 2882204281u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 571355409u, 3755327781u},
                                    {0.008333333333333333, 1738469855u, 2350098356u},
                                    {0.075, 1619796007u, 448014130u},
                                    {0.08333333333333333, 913554553u, 938513291u},
                                    {0.14166666666666666, 100594683u, 2661126256u},
                                    {0.15, 56252525u, 3660480504u},
                                    {0.15833333333333333, 2668706111u, 2508075887u},
                                    {0.16666666666666666, 482543527u, 2089998689u},
                                    {0.2, 2455959073u, 2504531799u},
                                    {0.20833333333333334, 1890457299u, 1292388844u},
                                    {0.325, 268425221u, 4092815850u},
                                    {0.3333333333333333, 3387978953u, 3332312994u},
                                    {0.3416666666666667, 2348300420u, 2917786695u},
                                    {0.35, 2069322379u, 908191334u},
                                    {0.4083333333333333, 555747228u, 2897990661u},
                                    {0.4166666666666667, 2242071812u, 2191885382u},
                                    {0.425, 504380815u, 716462443u},
                                    {0.55, 1057924047u, 1829084546u},
                                    {0.5583333333333333, 3767888725u, 1510776318u},
                                    {0.5666666666666667, 3734698461u, 2197576584u},
                                    {0.8333333333333334, 915329309u, 1255070507u},
                                    {1.1083333333333334, 2385395370u, 3186365452u},
                                    {1.1166666666666667, 2889775177u, 3231588720u},
                                    {1.125, 3536295188u, 111845191u},
                                    {1.5, 664363773u, 1750236805u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 1366511745u, 2173237352u},
                                    {0.008333333333333333, 12652954u, 2709411021u},
                                    {0.075, 674978724u, 21662523u},
                                    {0.08333333333333333, 1346857779u, 226665485u},
                                    {0.14166666666666666, 3991881890u, 4196319842u},
                                    {0.15, 643637907u, 2088186934u},
                                    {0.15833333333333333, 1131403216u, 4057613672u},
                                    {0.16666666666666666, 2607580566u, 2606548499u},
                                    {0.2, 3582368343u, 3296285980u},
                                    {0.20833333333333334, 323784732u, 2878615954u},
                                    {0.325, 1862737743u, 203628991u},
                                    {0.3333333333333333, 1235315461u, 218161976u},
                                    {0.3416666666666667, 1092757548u, 4138310567u},
                                    {0.35, 857732425u, 647835412u},
                                    {0.4083333333333333, 1688842932u, 1381013539u},
                                    {0.4166666666666667, 2536710549u, 3177731475u},
                                    {0.425, 1779798391u, 2629106321u},
                                    {0.55, 2942228028u, 1978031264u},
                                    {0.5583333333333333, 944636624u, 1236672933u},
                                    {0.5666666666666667, 223539127u, 3351352425u},
                                    {0.8333333333333334, 3471133613u, 218093938u},
                                    {1.1083333333333334, 1307029059u, 3559687278u},
                                    {1.1166666666666667, 2952325444u, 2857854418u},
                                    {1.125, 2102380700u, 2472058806u},
                                    {1.5, 3866463388u, 4166702901u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 1964259818u, 1695396651u},
                                    {0.35, 3442779402u, 1113018944u},
                                    {0.4083333333333333, 84609239u, 3231082268u},
                                    {0.4166666666666667, 1176312495u, 1851808077u},
                                    {0.425, 4058323630u, 3717156759u},
                                    {0.55, 3975261124u, 2513446315u},
                                    {0.5583333333333333, 566225713u, 1047772655u},
                                    {0.5666666666666667, 2952112788u, 3390920005u},
                                    {0.8333333333333334, 1569241962u, 3358450260u},
                                    {1.1083333333333334, 4161843325u, 740024887u},
                                    {1.1166666666666667, 2219870797u, 601894631u},
                                    {1.125, 1351201725u, 3876838973u},
                                    {1.5, 3967111969u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 1823825063u, 1695396651u},
                                    {0.35, 3947458183u, 1113018944u},
                                    {0.4083333333333333, 1602521783u, 3231082268u},
                                    {0.4166666666666667, 527900099u, 1851808077u},
                                    {0.425, 2360645638u, 3717156759u},
                                    {0.55, 1038469819u, 2513446315u},
                                    {0.5583333333333333, 3919187617u, 1047772655u},
                                    {0.5666666666666667, 342531698u, 3390920005u},
                                    {0.8333333333333334, 1701641073u, 3358450260u},
                                    {1.1083333333333334, 714249283u, 740024887u},
                                    {1.1166666666666667, 11771423u, 601894631u},
                                    {1.125, 889919951u, 3876838973u},
                                    {1.5, 1713650065u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(1193046)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 3000991951u, 1695396651u},
                                    {0.35, 1881532849u, 1113018944u},
                                    {0.4083333333333333, 2583825872u, 3231082268u},
                                    {0.4166666666666667, 219800813u, 1851808077u},
                                    {0.425, 682099884u, 3717156759u},
                                    {0.55, 1148691809u, 2513446315u},
                                    {0.5583333333333333, 2883562369u, 1047772655u},
                                    {0.5666666666666667, 2154839421u, 3390920005u},
                                    {0.8333333333333334, 91306871u, 3358450260u},
                                    {1.1083333333333334, 3094938035u, 740024887u},
                                    {1.1166666666666667, 2861790785u, 601894631u},
                                    {1.125, 1862983776u, 3876838973u},
                                    {1.5, 3950729172u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 3271928969u, 1695396651u},
                                    {0.35, 967395985u, 1113018944u},
                                    {0.4083333333333333, 3163700411u, 3231082268u},
                                    {0.4166666666666667, 3006822023u, 1851808077u},
                                    {0.425, 414622260u, 3717156759u},
                                    {0.55, 847889011u, 2513446315u},
                                    {0.5583333333333333, 552111640u, 1047772655u},
                                    {0.5666666666666667, 810871652u, 3390920005u},
                                    {0.8333333333333334, 1601612280u, 3358450260u},
                                    {1.1083333333333334, 539724498u, 740024887u},
                                    {1.1166666666666667, 954747016u, 601894631u},
                                    {1.125, 972021787u, 3876838973u},
                                    {1.5, 2345323086u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 102848938u, 1695396651u},
                                    {0.35, 3093623964u, 1113018944u},
                                    {0.4083333333333333, 371937198u, 3231082268u},
                                    {0.4166666666666667, 507193583u, 1851808077u},
                                    {0.425, 1519609964u, 3717156759u},
                                    {0.55, 3862776468u, 2513446315u},
                                    {0.5583333333333333, 1896971846u, 1047772655u},
                                    {0.5666666666666667, 3003502996u, 3390920005u},
                                    {0.8333333333333334, 3398183100u, 3358450260u},
                                    {1.1083333333333334, 8601973u, 740024887u},
                                    {1.1166666666666667, 834717968u, 601894631u},
                                    {1.125, 1174611957u, 3876838973u},
                                    {1.5, 3041950001u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(4278190080)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
TEST_CASE("stylize.pixelfx.melee.heavyCleavePinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 2479476784u, 1695396651u},
                                    {0.35, 3040848821u, 1113018944u},
                                    {0.4083333333333333, 2175474000u, 3231082268u},
                                    {0.4166666666666667, 2640255938u, 1851808077u},
                                    {0.425, 839778661u, 3717156759u},
                                    {0.55, 2055681902u, 2513446315u},
                                    {0.5583333333333333, 3327439338u, 1047772655u},
                                    {0.5666666666666667, 4103451858u, 3390920005u},
                                    {0.8333333333333334, 1016533241u, 3358450260u},
                                    {1.1083333333333334, 2222126625u, 740024887u},
                                    {1.1166666666666667, 1053933786u, 601894631u},
                                    {1.125, 3272182996u, 3876838973u},
                                    {1.5, 93357284u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 1788040313u, 4043421640u},
                                    {0.075, 3934210125u, 3184523136u},
                                    {0.08333333333333333, 4140539730u, 3025636165u},
                                    {0.14166666666666666, 4085495416u, 1450539664u},
                                    {0.15, 4245768492u, 1558490400u},
                                    {0.15833333333333333, 3048047778u, 3524018865u},
                                    {0.16666666666666666, 3878558316u, 4112110149u},
                                    {0.2, 1212767186u, 2795441835u},
                                    {0.20833333333333334, 1382434536u, 284335670u},
                                    {0.325, 663916807u, 1448756455u},
                                    {0.3333333333333333, 426694282u, 2000126223u},
                                    {0.3416666666666667, 3091342001u, 4094342702u},
                                    {0.35, 3968788593u, 2859827677u},
                                    {0.4083333333333333, 1552736999u, 814940943u},
                                    {0.4166666666666667, 30490821u, 3947254321u},
                                    {0.425, 3660245046u, 3823130590u},
                                    {0.55, 732490929u, 3717395647u},
                                    {0.5583333333333333, 3043950278u, 517200627u},
                                    {0.5666666666666667, 3301105832u, 3228680086u},
                                    {0.8333333333333334, 2884267812u, 2192729111u},
                                    {1.1083333333333334, 1630755064u, 3653864303u},
                                    {1.1166666666666667, 3473830273u, 762006814u},
                                    {1.125, 3544723399u, 2906727145u},
                                    {1.5, 3389562123u, 1197323566u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 2479476784u, 1695396651u},
                                    {0.35, 3040848821u, 1113018944u},
                                    {0.4083333333333333, 2175474000u, 3231082268u},
                                    {0.4166666666666667, 2640255938u, 1851808077u},
                                    {0.425, 839778661u, 3717156759u},
                                    {0.55, 2055681902u, 2513446315u},
                                    {0.5583333333333333, 3327439338u, 1047772655u},
                                    {0.5666666666666667, 4103451858u, 3390920005u},
                                    {0.8333333333333334, 1016533241u, 3358450260u},
                                    {1.1083333333333334, 2222126625u, 740024887u},
                                    {1.1166666666666667, 1053933786u, 601894631u},
                                    {1.125, 3272182996u, 3876838973u},
                                    {1.5, 93357284u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
        constexpr Frame frames[] = {{0, 3867274521u, 4121985294u},
                                    {0.008333333333333333, 3700619468u, 718851947u},
                                    {0.075, 2255845327u, 1032262470u},
                                    {0.08333333333333333, 2617429676u, 2607957231u},
                                    {0.14166666666666666, 862196710u, 3590219314u},
                                    {0.15, 367202996u, 3279240483u},
                                    {0.15833333333333333, 2193178745u, 2272604612u},
                                    {0.16666666666666666, 272569252u, 1060973183u},
                                    {0.2, 1497934226u, 3025988861u},
                                    {0.20833333333333334, 1464273561u, 3987900154u},
                                    {0.325, 3590976859u, 3919832295u},
                                    {0.3333333333333333, 3663287057u, 2306564623u},
                                    {0.3416666666666667, 2479476784u, 1695396651u},
                                    {0.35, 3040848821u, 1113018944u},
                                    {0.4083333333333333, 2175474000u, 3231082268u},
                                    {0.4166666666666667, 2640255938u, 1851808077u},
                                    {0.425, 839778661u, 3717156759u},
                                    {0.55, 2055681902u, 2513446315u},
                                    {0.5583333333333333, 3327439338u, 1047772655u},
                                    {0.5666666666666667, 4103451858u, 3390920005u},
                                    {0.8333333333333334, 1016533241u, 3358450260u},
                                    {1.1083333333333334, 2222126625u, 740024887u},
                                    {1.1166666666666667, 1053933786u, 601894631u},
                                    {1.125, 3272182996u, 3876838973u},
                                    {1.5, 93357284u, 105246437u},
                                    {2, 1661350341u, 1661350341u},
                                    {5, 1661350341u, 1661350341u}};
        PixelFx         fx(PixelFxKind::HeavyCleave);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyCleave, value);
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
TEST_CASE("stylize.pixelfx.melee.hammerSmashPinnedMaterialsSeedsAndFullTail") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 3256208779u, 589340494u},
                                    {0.15833333333333333, 1114906140u, 3749751085u},
                                    {0.16666666666666666, 4177662582u, 886002785u},
                                    {0.2, 1552554272u, 2774798200u},
                                    {0.20833333333333334, 442871185u, 3143917457u},
                                    {0.325, 1907069439u, 2557698373u},
                                    {0.3333333333333333, 2120354523u, 660187362u},
                                    {0.3416666666666667, 318298769u, 904733015u},
                                    {0.35, 121713149u, 848672865u},
                                    {0.4083333333333333, 3930553997u, 918648723u},
                                    {0.4166666666666667, 361288815u, 3251529367u},
                                    {0.425, 1831456318u, 2255040333u},
                                    {0.55, 4223345109u, 1377608417u},
                                    {0.5583333333333333, 2782884799u, 2812055453u},
                                    {0.5666666666666667, 2216817858u, 1660205807u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value     = fx.configurationValue();
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2617601968u, 1916248211u},
                                    {0.008333333333333333, 1548137080u, 2941309879u},
                                    {0.075, 3210472237u, 2239598141u},
                                    {0.08333333333333333, 3505015417u, 2946651568u},
                                    {0.14166666666666666, 2945833515u, 4155022312u},
                                    {0.15, 3467042811u, 3789528891u},
                                    {0.15833333333333333, 2025638683u, 3829534780u},
                                    {0.16666666666666666, 9015170u, 3647385089u},
                                    {0.2, 2007352095u, 3972227108u},
                                    {0.20833333333333334, 3946440698u, 963475068u},
                                    {0.325, 2222658250u, 184402140u},
                                    {0.3333333333333333, 2391890727u, 2605562039u},
                                    {0.3416666666666667, 2070932441u, 3490600097u},
                                    {0.35, 1579243522u, 3751479824u},
                                    {0.4083333333333333, 644511955u, 4265488422u},
                                    {0.4166666666666667, 3911771663u, 2867134334u},
                                    {0.425, 78997183u, 2102710606u},
                                    {0.55, 419314558u, 3919948907u},
                                    {0.5583333333333333, 113114994u, 2102056592u},
                                    {0.5666666666666667, 1114839823u, 3820031149u},
                                    {0.8333333333333334, 4089347859u, 1418108005u},
                                    {1.1083333333333334, 986645520u, 835125214u},
                                    {1.1166666666666667, 882736733u, 300475859u},
                                    {1.125, 2271569060u, 445026560u},
                                    {1.5, 2467362654u, 1706976136u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto input_palette =
            eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto input_stone = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 2729818533u, 1329888812u},
                                    {0.15833333333333333, 2574049731u, 2022327598u},
                                    {0.16666666666666666, 999571914u, 957292935u},
                                    {0.2, 1056378872u, 606194323u},
                                    {0.20833333333333334, 24804993u, 998898038u},
                                    {0.325, 1347186709u, 2431387408u},
                                    {0.3333333333333333, 1372094101u, 3160871701u},
                                    {0.3416666666666667, 2523089065u, 229975057u},
                                    {0.35, 226679491u, 972725079u},
                                    {0.4083333333333333, 3174479898u, 796199801u},
                                    {0.4166666666666667, 864300723u, 3830215423u},
                                    {0.425, 1056510009u, 1114796199u},
                                    {0.55, 876049354u, 292162110u},
                                    {0.5583333333333333, 3710787670u, 1047221950u},
                                    {0.5666666666666667, 1137438668u, 2175169796u},
                                    {0.8333333333333334, 3668484345u, 892379594u},
                                    {1.1083333333333334, 3896541958u, 3146614366u},
                                    {1.1166666666666667, 2553257213u, 829241287u},
                                    {1.125, 3252345685u, 3859600078u},
                                    {1.5, 2878807810u, 3007556205u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 2872446711u, 2975156139u},
                                    {0.15833333333333333, 1298418341u, 4031677696u},
                                    {0.16666666666666666, 3362450866u, 2108125345u},
                                    {0.2, 1830592937u, 419860223u},
                                    {0.20833333333333334, 1098782947u, 594438147u},
                                    {0.325, 2929194396u, 236177431u},
                                    {0.3333333333333333, 2188823373u, 334203684u},
                                    {0.3416666666666667, 1491134059u, 92652851u},
                                    {0.35, 173524462u, 751358174u},
                                    {0.4083333333333333, 1308280660u, 4210446467u},
                                    {0.4166666666666667, 4056717748u, 1275230326u},
                                    {0.425, 1956517729u, 938900921u},
                                    {0.55, 1667932965u, 3487274786u},
                                    {0.5583333333333333, 1538393063u, 3847047935u},
                                    {0.5666666666666667, 1741500552u, 2743677574u},
                                    {0.8333333333333334, 1802040049u, 2551578676u},
                                    {1.1083333333333334, 4015443047u, 1847855594u},
                                    {1.1166666666666667, 2094585430u, 1762270066u},
                                    {1.125, 2983231297u, 2711474368u},
                                    {1.5, 488986955u, 1605827234u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value      = fx.configurationValue();
        auto input_seed = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_seed);
        value.find("options")->set("seed", input_seed.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2200869865u, 2071898331u},
                                    {0.008333333333333333, 3871860689u, 2414283854u},
                                    {0.075, 2956442381u, 3110234621u},
                                    {0.08333333333333333, 686542341u, 3646540852u},
                                    {0.14166666666666666, 408553509u, 2714108491u},
                                    {0.15, 3309793910u, 1448100466u},
                                    {0.15833333333333333, 2465449462u, 4108790434u},
                                    {0.16666666666666666, 1669834175u, 3111433961u},
                                    {0.2, 405196550u, 2191752565u},
                                    {0.20833333333333334, 3861875882u, 198969489u},
                                    {0.325, 444392231u, 3756192521u},
                                    {0.3333333333333333, 2844799851u, 1459358694u},
                                    {0.3416666666666667, 4010629162u, 338758202u},
                                    {0.35, 2577881656u, 3902393097u},
                                    {0.4083333333333333, 1886678688u, 3313369968u},
                                    {0.4166666666666667, 2706980019u, 3383403071u},
                                    {0.425, 419255836u, 2109886527u},
                                    {0.55, 1015616085u, 2692534145u},
                                    {0.5583333333333333, 735327291u, 2494819244u},
                                    {0.5666666666666667, 3918179866u, 3208960522u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 1044165097u, 4255073627u},
                                    {0.008333333333333333, 511276784u, 3641297884u},
                                    {0.075, 549905796u, 3355929583u},
                                    {0.08333333333333333, 1215702489u, 3411842384u},
                                    {0.14166666666666666, 161206312u, 1727946367u},
                                    {0.15, 2175168077u, 3312110010u},
                                    {0.15833333333333333, 550393170u, 741070038u},
                                    {0.16666666666666666, 1443850764u, 2148425166u},
                                    {0.2, 788524535u, 4270770237u},
                                    {0.20833333333333334, 3197686648u, 1192817516u},
                                    {0.325, 1561785646u, 506868232u},
                                    {0.3333333333333333, 942611573u, 3210573368u},
                                    {0.3416666666666667, 4046390593u, 2979480077u},
                                    {0.35, 3192768028u, 2281276110u},
                                    {0.4083333333333333, 1111603836u, 55969237u},
                                    {0.4166666666666667, 2045241996u, 622323189u},
                                    {0.425, 3118464784u, 3242137367u},
                                    {0.55, 3985687097u, 3649855752u},
                                    {0.5583333333333333, 1632594729u, 2787706501u},
                                    {0.5666666666666667, 61766995u, 306086327u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value         = fx.configurationValue();
        auto input_palette = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input_palette);
        value.find("options")->set("palette", input_palette.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 4074297374u, 589340494u},
                                    {0.15833333333333333, 1467458043u, 3749751085u},
                                    {0.16666666666666666, 3900142672u, 886002785u},
                                    {0.2, 3997077585u, 2774798200u},
                                    {0.20833333333333334, 2518970872u, 3143917457u},
                                    {0.325, 2105087965u, 2557698373u},
                                    {0.3333333333333333, 3983431482u, 660187362u},
                                    {0.3416666666666667, 3135530889u, 904733015u},
                                    {0.35, 1436269501u, 848672865u},
                                    {0.4083333333333333, 1643911941u, 918648723u},
                                    {0.4166666666666667, 1097388062u, 3251529367u},
                                    {0.425, 3555344583u, 2255040333u},
                                    {0.55, 506397532u, 1377608417u},
                                    {0.5583333333333333, 1252548203u, 2812055453u},
                                    {0.5666666666666667, 3777967085u, 1660205807u},
                                    {0.8333333333333334, 3739462763u, 1824261832u},
                                    {1.1083333333333334, 1915085740u, 653716115u},
                                    {1.1166666666666667, 1953735118u, 1825398045u},
                                    {1.125, 3581951934u, 2180056962u},
                                    {1.5, 1886721471u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 2561897857u, 589340494u},
                                    {0.15833333333333333, 772934817u, 3749751085u},
                                    {0.16666666666666666, 3897584399u, 886002785u},
                                    {0.2, 2432258438u, 2774798200u},
                                    {0.20833333333333334, 2220509506u, 3143917457u},
                                    {0.325, 570406230u, 2557698373u},
                                    {0.3333333333333333, 4053140941u, 660187362u},
                                    {0.3416666666666667, 2169629130u, 904733015u},
                                    {0.35, 2776756778u, 848672865u},
                                    {0.4083333333333333, 1825478338u, 918648723u},
                                    {0.4166666666666667, 1334623107u, 3251529367u},
                                    {0.425, 3223938855u, 2255040333u},
                                    {0.55, 2907927920u, 1377608417u},
                                    {0.5583333333333333, 664886462u, 2812055453u},
                                    {0.5666666666666667, 4099882535u, 1660205807u},
                                    {0.8333333333333334, 2614089374u, 1824261832u},
                                    {1.1083333333333334, 4185444061u, 653716115u},
                                    {1.1166666666666667, 3401745860u, 1825398045u},
                                    {1.125, 348221968u, 2180056962u},
                                    {1.5, 2256787250u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(1193046)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 3474686117u, 589340494u},
                                    {0.15833333333333333, 2247949744u, 3749751085u},
                                    {0.16666666666666666, 2871967307u, 886002785u},
                                    {0.2, 1666064357u, 2774798200u},
                                    {0.20833333333333334, 1128494270u, 3143917457u},
                                    {0.325, 2271072963u, 2557698373u},
                                    {0.3333333333333333, 1699682047u, 660187362u},
                                    {0.3416666666666667, 2938080273u, 904733015u},
                                    {0.35, 3657672053u, 848672865u},
                                    {0.4083333333333333, 3231390478u, 918648723u},
                                    {0.4166666666666667, 1534051217u, 3251529367u},
                                    {0.425, 3475131444u, 2255040333u},
                                    {0.55, 1818796345u, 1377608417u},
                                    {0.5583333333333333, 2368261654u, 2812055453u},
                                    {0.5666666666666667, 2481448271u, 1660205807u},
                                    {0.8333333333333334, 1223737402u, 1824261832u},
                                    {1.1083333333333334, 716040345u, 653716115u},
                                    {1.1166666666666667, 665419881u, 1825398045u},
                                    {1.125, 1834547308u, 2180056962u},
                                    {1.5, 4035607871u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 813434004u, 589340494u},
                                    {0.15833333333333333, 1766659457u, 3749751085u},
                                    {0.16666666666666666, 1209888243u, 886002785u},
                                    {0.2, 3062127690u, 2774798200u},
                                    {0.20833333333333334, 664538555u, 3143917457u},
                                    {0.325, 654561315u, 2557698373u},
                                    {0.3333333333333333, 1784732170u, 660187362u},
                                    {0.3416666666666667, 3981904259u, 904733015u},
                                    {0.35, 237706019u, 848672865u},
                                    {0.4083333333333333, 3827937620u, 918648723u},
                                    {0.4166666666666667, 3654135957u, 3251529367u},
                                    {0.425, 3354743420u, 2255040333u},
                                    {0.55, 1716184125u, 1377608417u},
                                    {0.5583333333333333, 73240635u, 2812055453u},
                                    {0.5666666666666667, 2635540008u, 1660205807u},
                                    {0.8333333333333334, 1312533376u, 1824261832u},
                                    {1.1083333333333334, 4085154528u, 653716115u},
                                    {1.1166666666666667, 1399292901u, 1825398045u},
                                    {1.125, 1365785932u, 2180056962u},
                                    {1.5, 3151620003u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 4170933566u, 589340494u},
                                    {0.15833333333333333, 2316138934u, 3749751085u},
                                    {0.16666666666666666, 2630326655u, 886002785u},
                                    {0.2, 1678334662u, 2774798200u},
                                    {0.20833333333333334, 4058910936u, 3143917457u},
                                    {0.325, 1651936650u, 2557698373u},
                                    {0.3333333333333333, 1423130453u, 660187362u},
                                    {0.3416666666666667, 4026287377u, 904733015u},
                                    {0.35, 279522456u, 848672865u},
                                    {0.4083333333333333, 1886692695u, 918648723u},
                                    {0.4166666666666667, 4161221975u, 3251529367u},
                                    {0.425, 3134176064u, 2255040333u},
                                    {0.55, 725396630u, 1377608417u},
                                    {0.5583333333333333, 2307053696u, 2812055453u},
                                    {0.5666666666666667, 1421374698u, 1660205807u},
                                    {0.8333333333333334, 297433081u, 1824261832u},
                                    {1.1083333333333334, 309815997u, 653716115u},
                                    {1.1166666666666667, 1294937114u, 1825398045u},
                                    {1.125, 266887609u, 2180056962u},
                                    {1.5, 4080704526u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        REQUIRE(fx.isSimulated());
        auto value       = fx.configurationValue();
        auto input_stone = eve::Value::fromJson(R"json(4278190080)json");
        REQUIRE(input_stone);
        value.find("options")->set("stone", input_stone.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
TEST_CASE("stylize.pixelfx.melee.hammerSmashPinnedOptionalActorSnapshots") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    {
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 3256208779u, 589340494u},
                                    {0.15833333333333333, 1114906140u, 3749751085u},
                                    {0.16666666666666666, 4177662582u, 886002785u},
                                    {0.2, 1552554272u, 2774798200u},
                                    {0.20833333333333334, 442871185u, 3143917457u},
                                    {0.325, 1907069439u, 2557698373u},
                                    {0.3333333333333333, 2120354523u, 660187362u},
                                    {0.3416666666666667, 318298769u, 904733015u},
                                    {0.35, 121713149u, 848672865u},
                                    {0.4083333333333333, 3930553997u, 918648723u},
                                    {0.4166666666666667, 361288815u, 3251529367u},
                                    {0.425, 1831456318u, 2255040333u},
                                    {0.55, 4223345109u, 1377608417u},
                                    {0.5583333333333333, 2782884799u, 2812055453u},
                                    {0.5666666666666667, 2216817858u, 1660205807u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "hero"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 4199215029u, 2309226652u},
                                    {0.15833333333333333, 1225287890u, 3312858751u},
                                    {0.16666666666666666, 734223262u, 3858137519u},
                                    {0.2, 685395118u, 1473615199u},
                                    {0.20833333333333334, 2594631590u, 2479777925u},
                                    {0.325, 2474057546u, 2709539010u},
                                    {0.3333333333333333, 3295684179u, 1305839455u},
                                    {0.3416666666666667, 2479017955u, 25801155u},
                                    {0.35, 1119002497u, 2279832780u},
                                    {0.4083333333333333, 2295073633u, 60286910u},
                                    {0.4166666666666667, 2223850729u, 2689114388u},
                                    {0.425, 1882051328u, 2106506675u},
                                    {0.55, 949018215u, 2594531960u},
                                    {0.5583333333333333, 159324803u, 2749758444u},
                                    {0.5666666666666667, 3609818905u, 3737666519u},
                                    {0.8333333333333334, 819048326u, 1418108005u},
                                    {1.1083333333333334, 3471734038u, 835125214u},
                                    {1.1166666666666667, 3263768212u, 300475859u},
                                    {1.125, 3231986321u, 445026560u},
                                    {1.5, 91703738u, 1706976136u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 10, "height": 8, "scale": 1, "x": -5.5, "y": -34.25, "head": [0, -34.25], "chest": [0, -29], "eyes": [[3, 2], [6, 2]], "mask": [0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0], "pixels": [0, 0, 0, 4284497952, 4289249496, 4289249496, 4284497952, 0, 0, 0, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 0, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 4284497952, 4289249496, 4289249496, 0, 0, 0, 0, 4289249496, 4289249496, 4284497952, 4289249496, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        value.find("options")->set("seed", eve::Value(int64_t(123)));
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 3256208779u, 589340494u},
                                    {0.15833333333333333, 1114906140u, 3749751085u},
                                    {0.16666666666666666, 4177662582u, 886002785u},
                                    {0.2, 1552554272u, 2774798200u},
                                    {0.20833333333333334, 442871185u, 3143917457u},
                                    {0.325, 1907069439u, 2557698373u},
                                    {0.3333333333333333, 2120354523u, 660187362u},
                                    {0.3416666666666667, 318298769u, 904733015u},
                                    {0.35, 121713149u, 848672865u},
                                    {0.4083333333333333, 3930553997u, 918648723u},
                                    {0.4166666666666667, 361288815u, 3251529367u},
                                    {0.425, 1831456318u, 2255040333u},
                                    {0.55, 4223345109u, 1377608417u},
                                    {0.5583333333333333, 2782884799u, 2812055453u},
                                    {0.5666666666666667, 2216817858u, 1660205807u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        auto            value = fx.configurationValue();
        auto            input = eve::Value::fromJson(
            R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "custom", "width": 8, "height": 3, "scale": 1, "x": 1.5, "y": -3.25, "head": [0, -3], "chest": [0, -1.5], "eyes": [], "mask": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], "pixels": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
        constexpr Frame frames[] = {{0, 2128454311u, 410641358u},
                                    {0.008333333333333333, 3079324567u, 719072646u},
                                    {0.075, 1182443994u, 130742035u},
                                    {0.08333333333333333, 488302010u, 1982071037u},
                                    {0.14166666666666666, 2327166512u, 1836408700u},
                                    {0.15, 3256208779u, 589340494u},
                                    {0.15833333333333333, 1114906140u, 3749751085u},
                                    {0.16666666666666666, 4177662582u, 886002785u},
                                    {0.2, 1552554272u, 2774798200u},
                                    {0.20833333333333334, 442871185u, 3143917457u},
                                    {0.325, 1907069439u, 2557698373u},
                                    {0.3333333333333333, 2120354523u, 660187362u},
                                    {0.3416666666666667, 318298769u, 904733015u},
                                    {0.35, 121713149u, 848672865u},
                                    {0.4083333333333333, 3930553997u, 918648723u},
                                    {0.4166666666666667, 361288815u, 3251529367u},
                                    {0.425, 1831456318u, 2255040333u},
                                    {0.55, 4223345109u, 1377608417u},
                                    {0.5583333333333333, 2782884799u, 2812055453u},
                                    {0.5666666666666667, 2216817858u, 1660205807u},
                                    {0.8333333333333334, 3184423995u, 1824261832u},
                                    {1.1083333333333334, 1663367791u, 653716115u},
                                    {1.1166666666666667, 1659340120u, 1825398045u},
                                    {1.125, 536566033u, 2180056962u},
                                    {1.5, 2502719749u, 495714667u},
                                    {2, 2451738053u, 2451738053u},
                                    {5, 2451738053u, 2451738053u}};
        PixelFx         fx(PixelFxKind::HammerSmash);
        auto            value = fx.configurationValue();
        auto            input =
            eve::Value::fromJson(R"actor({"schema": "eve.pixelfx.actor", "version": 1, "source": "none"})actor");
        REQUIRE(input);
        value.find("options")->set("actor", input.value());
        auto candidate = PixelFx::prepareConfigurationValue(PixelFxKind::HammerSmash, value);
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
TEST_CASE("stylize.pixelfx.melee.fourthReverseSeekLayersPurityAndStopRestore") {
    for (auto kind : {PixelFxKind::ScytheReap, PixelFxKind::HeavyCleave, PixelFxKind::HammerSmash}) {
        PixelFx  fx(kind);
        uint32_t originalColor = 0, originalLight = 0;
        for (double t : {.13, 5., .15, .15833333333333333, .3333333333333333, .3416666666666667, .42, .55,
                         .5666666666666667, 1.1, 1.5, .13, 0., .13}) {
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
TEST_CASE("stylize.pixelfx.melee.fourthPartitionedClocksPreserveEmissionOrder") {
    for (auto kind : {PixelFxKind::ScytheReap, PixelFxKind::HeavyCleave, PixelFxKind::HammerSmash})
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
TEST_CASE("stylize.pixelfx.melee.fourthMaterialOwnershipRoundTripBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::ScytheReap, PixelFxKind::HeavyCleave, PixelFxKind::HammerSmash}) {
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
TEST_CASE("stylize.pixelfx.melee.fourthPinnedStaticStockBackBodyFrontComposition") {
    struct Frame {
        PixelFxKind kind;
        int         actor;
        double      time;
        uint32_t    color, light;
    };
    constexpr Frame frames[] = {
        {PixelFxKind::ScytheReap, 1, 0, 35182356u, 1893850053u},
        {PixelFxKind::ScytheReap, 1, 0.008333333333333333, 2791689080u, 90298509u},
        {PixelFxKind::ScytheReap, 1, 0.075, 3810076662u, 2702031552u},
        {PixelFxKind::ScytheReap, 1, 0.08333333333333333, 2487443314u, 3060510409u},
        {PixelFxKind::ScytheReap, 1, 0.09166666666666666, 2964784438u, 3379474933u},
        {PixelFxKind::ScytheReap, 1, 0.3, 2119131679u, 118653462u},
        {PixelFxKind::ScytheReap, 1, 0.4083333333333333, 3729556378u, 4106824983u},
        {PixelFxKind::ScytheReap, 1, 0.4166666666666667, 3036498241u, 3740277928u},
        {PixelFxKind::ScytheReap, 1, 0.425, 2103517409u, 856624517u},
        {PixelFxKind::ScytheReap, 1, 0.55, 2224435357u, 2295594255u},
        {PixelFxKind::ScytheReap, 1, 0.5583333333333333, 780524633u, 1912261786u},
        {PixelFxKind::ScytheReap, 1, 0.5666666666666667, 2640709636u, 999140840u},
        {PixelFxKind::ScytheReap, 1, 0.8333333333333334, 1197464637u, 557039795u},
        {PixelFxKind::ScytheReap, 1, 1.1083333333333334, 2594208807u, 985325863u},
        {PixelFxKind::ScytheReap, 1, 1.1166666666666667, 935783327u, 569910389u},
        {PixelFxKind::ScytheReap, 1, 1.125, 2333489090u, 1872010325u},
        {PixelFxKind::ScytheReap, 1, 5, 35182356u, 1893850053u},
        {PixelFxKind::ScytheReap, 2, 0, 3681507951u, 1893850053u},
        {PixelFxKind::ScytheReap, 2, 0.008333333333333333, 3975205211u, 90298509u},
        {PixelFxKind::ScytheReap, 2, 0.075, 2989611909u, 2702031552u},
        {PixelFxKind::ScytheReap, 2, 0.08333333333333333, 1199358329u, 3060510409u},
        {PixelFxKind::ScytheReap, 2, 0.09166666666666666, 4215090245u, 3379474933u},
        {PixelFxKind::ScytheReap, 2, 0.3, 2578702385u, 118653462u},
        {PixelFxKind::ScytheReap, 2, 0.4083333333333333, 4158996911u, 4106824983u},
        {PixelFxKind::ScytheReap, 2, 0.4166666666666667, 520292728u, 3740277928u},
        {PixelFxKind::ScytheReap, 2, 0.425, 2084287624u, 856624517u},
        {PixelFxKind::ScytheReap, 2, 0.55, 176350563u, 2295594255u},
        {PixelFxKind::ScytheReap, 2, 0.5583333333333333, 3240992725u, 1912261786u},
        {PixelFxKind::ScytheReap, 2, 0.5666666666666667, 2745521089u, 999140840u},
        {PixelFxKind::ScytheReap, 2, 0.8333333333333334, 332271630u, 557039795u},
        {PixelFxKind::ScytheReap, 2, 1.1083333333333334, 452296320u, 985325863u},
        {PixelFxKind::ScytheReap, 2, 1.1166666666666667, 3224495277u, 569910389u},
        {PixelFxKind::ScytheReap, 2, 1.125, 2499653307u, 1872010325u},
        {PixelFxKind::ScytheReap, 2, 5, 3681507951u, 1893850053u},
        {PixelFxKind::HeavyCleave, 1, 0, 1033977136u, 4121985294u},
        {PixelFxKind::HeavyCleave, 1, 0.008333333333333333, 3972595665u, 718851947u},
        {PixelFxKind::HeavyCleave, 1, 0.19166666666666668, 1393386180u, 3554854882u},
        {PixelFxKind::HeavyCleave, 1, 0.2, 430302883u, 3025988861u},
        {PixelFxKind::HeavyCleave, 1, 0.20833333333333334, 3631280048u, 3987900154u},
        {PixelFxKind::HeavyCleave, 1, 0.325, 147202658u, 3919832295u},
        {PixelFxKind::HeavyCleave, 1, 0.3333333333333333, 969048944u, 2306564623u},
        {PixelFxKind::HeavyCleave, 1, 0.3416666666666667, 678014933u, 1695396651u},
        {PixelFxKind::HeavyCleave, 1, 0.35, 3498853664u, 1113018944u},
        {PixelFxKind::HeavyCleave, 1, 0.38333333333333336, 2707348452u, 3037449884u},
        {PixelFxKind::HeavyCleave, 1, 0.5, 2253429584u, 294688325u},
        {PixelFxKind::HeavyCleave, 1, 0.8333333333333334, 3272152447u, 3358450260u},
        {PixelFxKind::HeavyCleave, 1, 1.5, 539208317u, 105246437u},
        {PixelFxKind::HeavyCleave, 1, 1.8916666666666666, 292215956u, 1661350341u},
        {PixelFxKind::HeavyCleave, 1, 1.9, 292215956u, 1661350341u},
        {PixelFxKind::HeavyCleave, 1, 1.9083333333333334, 292215956u, 1661350341u},
        {PixelFxKind::HeavyCleave, 1, 5, 292215956u, 1661350341u},
        {PixelFxKind::HeavyCleave, 2, 0, 967214195u, 4121985294u},
        {PixelFxKind::HeavyCleave, 2, 0.008333333333333333, 352319838u, 718851947u},
        {PixelFxKind::HeavyCleave, 2, 0.19166666666666668, 611349919u, 3554854882u},
        {PixelFxKind::HeavyCleave, 2, 0.2, 266825836u, 3025988861u},
        {PixelFxKind::HeavyCleave, 2, 0.20833333333333334, 491146483u, 3987900154u},
        {PixelFxKind::HeavyCleave, 2, 0.325, 4254753385u, 3919832295u},
        {PixelFxKind::HeavyCleave, 2, 0.3333333333333333, 1021415755u, 2306564623u},
        {PixelFxKind::HeavyCleave, 2, 0.3416666666666667, 781625385u, 1695396651u},
        {PixelFxKind::HeavyCleave, 2, 0.35, 2663053384u, 1113018944u},
        {PixelFxKind::HeavyCleave, 2, 0.38333333333333336, 1109996846u, 3037449884u},
        {PixelFxKind::HeavyCleave, 2, 0.5, 2979756457u, 294688325u},
        {PixelFxKind::HeavyCleave, 2, 0.8333333333333334, 408196298u, 3358450260u},
        {PixelFxKind::HeavyCleave, 2, 1.5, 3365498028u, 105246437u},
        {PixelFxKind::HeavyCleave, 2, 1.8916666666666666, 281297775u, 1661350341u},
        {PixelFxKind::HeavyCleave, 2, 1.9, 281297775u, 1661350341u},
        {PixelFxKind::HeavyCleave, 2, 1.9083333333333334, 281297775u, 1661350341u},
        {PixelFxKind::HeavyCleave, 2, 5, 281297775u, 1661350341u},
        {PixelFxKind::HammerSmash, 1, 0, 876441170u, 410641358u},
        {PixelFxKind::HammerSmash, 1, 0.008333333333333333, 1866541730u, 719072646u},
        {PixelFxKind::HammerSmash, 1, 0.13333333333333333, 1320921901u, 505461606u},
        {PixelFxKind::HammerSmash, 1, 0.14166666666666666, 3901470685u, 1836408700u},
        {PixelFxKind::HammerSmash, 1, 0.15, 2732097770u, 589340494u},
        {PixelFxKind::HammerSmash, 1, 0.15833333333333333, 4126471349u, 3749751085u},
        {PixelFxKind::HammerSmash, 1, 0.16666666666666666, 3269474235u, 886002785u},
        {PixelFxKind::HammerSmash, 1, 0.19166666666666668, 2385418130u, 3765898889u},
        {PixelFxKind::HammerSmash, 1, 0.2, 2234400819u, 2774798200u},
        {PixelFxKind::HammerSmash, 1, 0.20833333333333334, 1739697251u, 3143917457u},
        {PixelFxKind::HammerSmash, 1, 0.25, 2481992811u, 2469920240u},
        {PixelFxKind::HammerSmash, 1, 0.5, 647946980u, 1501330499u},
        {PixelFxKind::HammerSmash, 1, 1, 1602891539u, 3749157788u},
        {PixelFxKind::HammerSmash, 1, 1.9916666666666667, 3402924244u, 2451738053u},
        {PixelFxKind::HammerSmash, 1, 2, 3402924244u, 2451738053u},
        {PixelFxKind::HammerSmash, 1, 2.0083333333333333, 3402924244u, 2451738053u},
        {PixelFxKind::HammerSmash, 1, 5, 3402924244u, 2451738053u},
        {PixelFxKind::HammerSmash, 2, 0, 2008167129u, 410641358u},
        {PixelFxKind::HammerSmash, 2, 0.008333333333333333, 3102687785u, 719072646u},
        {PixelFxKind::HammerSmash, 2, 0.13333333333333333, 750775046u, 505461606u},
        {PixelFxKind::HammerSmash, 2, 0.14166666666666666, 2394171302u, 1836408700u},
        {PixelFxKind::HammerSmash, 2, 0.15, 3494903562u, 589340494u},
        {PixelFxKind::HammerSmash, 2, 0.15833333333333333, 335771945u, 3749751085u},
        {PixelFxKind::HammerSmash, 2, 0.16666666666666666, 3227714975u, 886002785u},
        {PixelFxKind::HammerSmash, 2, 0.19166666666666668, 3654457037u, 3765898889u},
        {PixelFxKind::HammerSmash, 2, 0.2, 1881766989u, 2774798200u},
        {PixelFxKind::HammerSmash, 2, 0.20833333333333334, 3545949029u, 3143917457u},
        {PixelFxKind::HammerSmash, 2, 0.25, 696413247u, 2469920240u},
        {PixelFxKind::HammerSmash, 2, 0.5, 3214109328u, 1501330499u},
        {PixelFxKind::HammerSmash, 2, 1, 698651734u, 3749157788u},
        {PixelFxKind::HammerSmash, 2, 1.9916666666666667, 2084765935u, 2451738053u},
        {PixelFxKind::HammerSmash, 2, 2, 2084765935u, 2451738053u},
        {PixelFxKind::HammerSmash, 2, 2.0083333333333333, 2084765935u, 2451738053u},
        {PixelFxKind::HammerSmash, 2, 5, 2084765935u, 2451738053u},
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
TEST_CASE("stylize.pixelfx.melee.sourceCracksForksPointsAndFollowingRng") {
    namespace m = pixelfx::melee;
    {
        pixelfx::Rng                rng(0u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{41, 4}, {44, 5}, {45, 6}}, 0.3513862386025721, true},
            {{{28.04117055993993, 0}, {32, 2}, {34, 3}, {38, 3}, {39, 3}, {41, 4}, {44, 4}, {46, 4}}, 0, false},
            {{{27.539221046725288, 0},
              {30, 0},
              {34, 0},
              {36, 0},
              {38, 0},
              {40, 0},
              {42, 1},
              {44, 2},
              {46, 2},
              {50, 4},
              {53, 5},
              {55, 5},
              {57, 6},
              {60, 7},
              {63, 7}},
             0,
             false},
            {{{15, 2}, {13, 3}, {12, 4}}, 0.3029644449939951, true},
            {{{26.667466721963137, 0}, {25, 0}, {22, 1}, {20, 2}, {18, 2}, {15, 2}, {13, 2}, {11, 1}}, 0, false},
            {{{38, 4}, {41, 6}}, 0.27327629930805414, true},
            {{{41, 4}, {43, 5}}, 0.35036738608032464, true},
            {{{28.438297735527158, 0}, {30, 2}, {34, 1}, {35, 3}, {38, 4}, {41, 4}, {43, 5}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.9091057269833982);
    }
    {
        pixelfx::Rng                rng(0u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{20.70882944006007, 0}, {17, 2}, {15, 3}, {11, 3}, {9, 3}}, 0, false},
            {{{38, 4}, {40, 5}}, 0.6778153972603226, true},
            {{{21.601054825121537, 0}, {24, 0}, {26, 0}, {28, 1}, {30, 2}, {33, 2}, {36, 3}, {38, 4}, {40, 5}},
             0,
             false},
            {{{21.57308519689832, 0}, {24, 0}, {28, 1}, {30, 2}, {33, 3}}, 0, false},
            {{{8, 1}, {7, 2}, {5, 4}}, 0.5477556816040305, true},
            {{{21.327791511430405, 0}, {20, 0}, {16, 1}, {14, 1}, {10, 0}, {8, 1}, {7, 2}, {5, 2}}, 0, false},
            {{{21.28440629551187, 0}, {18, 1}, {17, 0}, {15, 1}, {13, 2}, {11, 3}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.9916538530960679);
    }
    {
        pixelfx::Rng                rng(0u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-4.041170559939928, 0}, {-8, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6128388606011868);
    }
    {
        pixelfx::Rng                rng(0u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6270739405881613);
    }
    {
        pixelfx::Rng                rng(1u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{41, 4}, {44, 5}, {45, 6}}, 0.3513862386025721, true},
            {{{28.04117055993993, 0}, {32, 2}, {34, 3}, {38, 3}, {39, 3}, {41, 4}, {44, 4}, {46, 4}}, 0, false},
            {{{27.539221046725288, 0},
              {30, 0},
              {34, 0},
              {36, 0},
              {38, 0},
              {40, 0},
              {42, 1},
              {44, 2},
              {46, 2},
              {50, 4},
              {53, 5},
              {55, 5},
              {57, 6},
              {60, 7},
              {63, 7}},
             0,
             false},
            {{{15, 2}, {13, 3}, {12, 4}}, 0.3029644449939951, true},
            {{{26.667466721963137, 0}, {25, 0}, {22, 1}, {20, 2}, {18, 2}, {15, 2}, {13, 2}, {11, 1}}, 0, false},
            {{{38, 4}, {41, 6}}, 0.27327629930805414, true},
            {{{41, 4}, {43, 5}}, 0.35036738608032464, true},
            {{{28.438297735527158, 0}, {30, 2}, {34, 1}, {35, 3}, {38, 4}, {41, 4}, {43, 5}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.9091057269833982);
    }
    {
        pixelfx::Rng                rng(1u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{20.70882944006007, 0}, {17, 2}, {15, 3}, {11, 3}, {9, 3}}, 0, false},
            {{{38, 4}, {40, 5}}, 0.6778153972603226, true},
            {{{21.601054825121537, 0}, {24, 0}, {26, 0}, {28, 1}, {30, 2}, {33, 2}, {36, 3}, {38, 4}, {40, 5}},
             0,
             false},
            {{{21.57308519689832, 0}, {24, 0}, {28, 1}, {30, 2}, {33, 3}}, 0, false},
            {{{8, 1}, {7, 2}, {5, 4}}, 0.5477556816040305, true},
            {{{21.327791511430405, 0}, {20, 0}, {16, 1}, {14, 1}, {10, 0}, {8, 1}, {7, 2}, {5, 2}}, 0, false},
            {{{21.28440629551187, 0}, {18, 1}, {17, 0}, {15, 1}, {13, 2}, {11, 3}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.9916538530960679);
    }
    {
        pixelfx::Rng                rng(1u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-4.041170559939928, 0}, {-8, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6128388606011868);
    }
    {
        pixelfx::Rng                rng(1u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6270739405881613);
    }
    {
        pixelfx::Rng                rng(5u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{50, 4}, {52, 5}, {55, 6}, {57, 8}}, 0.5743562830204609, true},
            {{{27.57964451541193, 0},
              {30, 0},
              {33, 0},
              {35, 2},
              {38, 1},
              {40, 2},
              {43, 3},
              {45, 4},
              {47, 4},
              {50, 4},
              {53, 5},
              {56, 6},
              {58, 7}},
             0,
             false},
            {{{54, 5}, {56, 6}}, 0.6741434100695187, true},
            {{{28.164127149269916, 0},
              {30, 0},
              {32, 2},
              {34, 3},
              {37, 2},
              {40, 3},
              {42, 2},
              {44, 2},
              {46, 3},
              {48, 4},
              {51, 4},
              {54, 5},
              {56, 6},
              {58, 6},
              {61, 6},
              {63, 7}},
             0,
             false},
            {{{19, 3}, {17, 4}, {15, 6}, {13, 6}}, 0.2167341184191173, true},
            {{{4, 4}, {3, 5}, {0, 7}}, 0.5756224048644072, true},
            {{{27.09668016724754, 0},
              {24, 1},
              {22, 2},
              {21, 2},
              {19, 3},
              {16, 4},
              {13, 4},
              {10, 5},
              {7, 4},
              {4, 4},
              {1, 5}},
             0,
             false},
            {{{27.312369840336032, 0}, {29, 0}, {31, 2}, {33, 3}, {36, 3}, {39, 3}, {43, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.5303444368764758);
    }
    {
        pixelfx::Rng                rng(5u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{21.17035548458807, 0}, {18, 0}, {15, 0}, {14, 2}, {11, 1}, {9, 2}, {6, 3}, {4, 4}, {2, 4}, {-1, 4}},
             0,
             false},
            {{{20.888656952534802, 0}, {17, 0}, {15, 1}, {12, 2}, {9, 2}, {7, 2}, {4, 1}}, 0, false},
            {{{32, 2}, {34, 3}, {37, 4}, {39, 6}}, 0.4329881822244109, true},
            {{{22.811903671594337, 0}, {25, 0}, {27, 2}, {29, 3}, {32, 2}, {34, 3}}, 0, false},
            {{{13, 2}, {11, 4}}, 0.35133399200761534, true},
            {{{10, 3}, {7, 4}}, 0.4674309064624443, true},
            {{{7, 4}, {5, 5}, {3, 6}, {1, 7}}, 0.5884958583386227, true},
            {{{20.065271852188744, 0}, {17, 1}, {15, 1}, {13, 2}, {10, 3}, {7, 4}}, 0, false},
            {{{29, 1}, {31, 2}, {33, 3}}, 0.29672026708431076, true},
            {{{33, 4}, {35, 5}, {37, 6}}, 0.4805724081476607, true},
            {{{21.96690263657365, 0}, {25, 0}, {27, 1}, {29, 1}, {31, 3}, {33, 4}, {35, 5}, {38, 6}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.28343368298374116);
    }
    {
        pixelfx::Rng                rng(5u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-3.5796445154119283, 0}, {-6, 0}, {-9, 0}, {-11, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6605600535403937);
    }
    {
        pixelfx::Rng                rng(5u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6897749109193683);
    }
    {
        pixelfx::Rng                rng(9u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{27.462649160064757, 0}, {31, 1}, {33, 2}, {35, 3}, {37, 5}, {40, 6}, {43, 7}}, 0, false},
            {{{39, 0}, {41, 2}, {42, 3}}, 0.29468858377367724, true},
            {{{42, 1}, {44, 2}, {47, 4}}, 0.3800420433661202, true},
            {{{55, 4}, {57, 6}, {59, 8}}, 0.68682495582907, true},
            {{{27.534723973018117, 0},
              {30, 0},
              {34, 0},
              {36, 0},
              {39, 0},
              {42, 1},
              {45, 1},
              {48, 1},
              {50, 2},
              {53, 3},
              {55, 4},
              {58, 4},
              {61, 4},
              {64, 5},
              {67, 5}},
             0,
             false},
            {{{15, 3}, {13, 4}}, 0.30205164341896307, true},
            {{{26.37467074219603, 0}, {24, 0}, {22, 1}, {19, 2}, {15, 3}, {13, 3}}, 0, false},
            {{{27.941431437968276, 0}, {30, 0}, {31, 0}, {35, 0}, {38, 1}, {41, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.8281138679012656);
    }
    {
        pixelfx::Rng                rng(9u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{21.712649160064757, 0}, {25, 1}, {27, 2}, {30, 3}, {32, 5}, {35, 6}, {37, 6}}, 0, false},
            {{{33, 0}, {35, 2}, {36, 3}}, 0.4911476396227954, true},
            {{{37, 1}, {38, 2}, {41, 4}}, 0.6334034056102004, true},
            {{{21.784723973018117, 0}, {25, 0}, {28, 0}, {30, 0}, {33, 0}, {37, 1}, {39, 1}, {42, 1}, {45, 2}},
             0,
             false},
            {{{20.123786977143027, 0}, {19, 0}, {16, 1}, {13, 3}, {11, 3}, {9, 5}, {6, 5}, {4, 6}}, 0, false},
            {{{22.335296186734922, 0}, {24, 0}, {27, 0}, {30, 1}, {33, 0}, {36, 1}}, 0, false},
            {{{22.46374959568493, 0}, {24, 0}, {26, 0}, {28, 0}, {30, 1}, {33, 2}, {35, 3}, {38, 3}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.9416943695396185);
    }
    {
        pixelfx::Rng                rng(9u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-3.037350839935243, 0}, {0, 1}, {2, 2}, {5, 3}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.28342978144064546);
    }
    {
        pixelfx::Rng                rng(9u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.19872891809791327);
    }
    {
        pixelfx::Rng                rng(123u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{11, 2}, {9, 3}, {6, 5}}, 0.40165546550997533, true},
            {{{8, 4}, {6, 6}, {5, 7}}, 0.47457036418491044, true},
            {{{26.507026728941128, 0},
              {25, 0},
              {21, 2},
              {19, 2},
              {17, 2},
              {15, 2},
              {13, 3},
              {11, 2},
              {8, 4},
              {6, 5},
              {4, 5}},
             0,
             false},
            {{{38, 3}, {41, 4}, {43, 5}}, 0.2699035582918441, true},
            {{{28.664239182020538, 0},
              {31, 0},
              {33, 1},
              {36, 1},
              {38, 3},
              {40, 2},
              {43, 4},
              {45, 3},
              {47, 3},
              {50, 4},
              {53, 4},
              {56, 5},
              {59, 5},
              {61, 5},
              {63, 6},
              {65, 7}},
             0,
             false},
            {{{35, 1}, {36, 3}, {37, 4}, {39, 6}}, 0.18783672851859592, true},
            {{{37, 1}, {39, 2}, {41, 4}}, 0.24804462092579344, true},
            {{{51, 1}, {52, 2}}, 0.5956507366790902, true},
            {{{27.819378644460812, 0},
              {30, 0},
              {31, 0},
              {33, 0},
              {35, 1},
              {37, 1},
              {39, 1},
              {41, 1},
              {44, 1},
              {46, 1},
              {49, 1},
              {51, 1}},
             0,
             false},
            {{{27.751897603389807, 0}, {30, 0}, {32, 2}, {36, 3}, {38, 4}, {41, 6}, {44, 6}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.09054857352748513);
    }
    {
        pixelfx::Rng                rng(123u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{20.757026728941128, 0}, {19, 0}, {16, 2}, {13, 2}, {11, 2}, {9, 2}, {7, 3}}, 0, false},
            {{{21.668812467250973, 0}, {24, 0}, {27, 2}, {30, 1}, {33, 3}, {35, 2}}, 0, false},
            {{{21.11487038654741, 0}, {19, 0}, {16, 1}, {14, 1}, {12, 1}, {9, 1}}, 0, false},
            {{{22.778144268202595, 0}, {25, 1}, {27, 0}, {30, 1}, {32, 1}, {34, 1}, {37, 2}}, 0, false},
            {{{21.22696475056, 0}, {18, 1}, {15, 1}, {13, 1}, {11, 2}, {9, 4}, {7, 5}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.23060493008233607);
    }
    {
        pixelfx::Rng                rng(123u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-3.9929732710588723, 0}, {-6, 0}, {-9, 2}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.4090933797415346);
    }
    {
        pixelfx::Rng                rng(123u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.7872516233474016);
    }
    {
        pixelfx::Rng                rng(4294967295u);
        const auto                  actual = m::cracks(rng, 27.25, 4, 40, 7, 0.4);
        const std::vector<m::Crack> expected{
            {{{26.17652098275721, 0}, {23, 1}, {20, 2}, {16, 3}, {14, 4}, {11, 5}, {9, 6}, {6, 7}, {4, 6}}, 0, false},
            {{{35, 1}, {37, 2}}, 0.18468764511635527, true},
            {{{27.279593352694064, 0},
              {30, 0},
              {32, 0},
              {35, 1},
              {36, 1},
              {39, 2},
              {41, 3},
              {44, 4},
              {47, 4},
              {49, 5},
              {52, 5}},
             0,
             false},
            {{{28.26446613087319, 0}, {31, 1}, {33, 1}, {35, 2}, {37, 2}, {40, 3}, {42, 4}}, 0, false},
            {{{37, 0}, {39, 1}, {42, 3}}, 0.24176627221459057, true},
            {{{27.602405304904096, 0}, {31, 0}, {34, 0}, {37, 0}, {39, 0}, {42, 0}, {45, 1}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.7777665639296174);
    }
    {
        pixelfx::Rng                rng(4294967295u);
        const auto                  actual = m::cracks(rng, 21.5, 5, 24, 6, 0);
        const std::vector<m::Crack> expected{
            {{{20.42652098275721, 0}, {17, 1}, {14, 2}, {11, 3}, {9, 4}, {5, 5}}, 0, false},
            {{{30, 1}, {32, 2}, {33, 3}}, 0.3545464570343029, true},
            {{{22.880361492512748, 0}, {26, 1}, {28, 1}, {30, 1}, {32, 1}, {34, 1}, {37, 1}, {39, 2}}, 0, false},
            {{{20.058003510697745, 0}, {17, 2}, {14, 2}, {11, 2}}, 0, false},
            {{{21.745555800851434, 0}, {25, 0}, {28, 1}, {31, 1}, {34, 1}, {37, 1}}, 0, false},
            {{{22.839237429085188, 0}, {26, 0}, {29, 1}, {31, 2}, {32, 4}, {34, 4}, {36, 4}, {38, 6}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.20412658783607185);
    }
    {
        pixelfx::Rng                rng(4294967295u);
        const auto                  actual = m::cracks(rng, -3.25, 1, 7, 3, -0.4);
        const std::vector<m::Crack> expected{
            {{{-4.323479017242789, 0}, {-8, 1}}, 0, false},
        };
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.6804977387655526);
    }
    {
        pixelfx::Rng                rng(4294967295u);
        const auto                  actual = m::cracks(rng, 0, 0, 40, 7, 0.3);
        const std::vector<m::Crack> expected{};
        REQUIRE(actual.size() == expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].at == expected[i].at);
            REQUIRE(actual[i].fork == expected[i].fork);
            REQUIRE(actual[i].points.size() == expected[i].points.size());
            for (size_t j = 0; j < actual[i].points.size(); ++j) {
                REQUIRE(actual[i].points[j].x == expected[i].points[j].x);
                REQUIRE(actual[i].points[j].y == expected[i].points[j].y);
            }
        }
        REQUIRE(rng.next() == 0.8964226141106337);
    }
}
TEST_CASE("stylize.pixelfx.melee.fourthRawStoneRejectsMalformedValuesAtomically") {
    for (auto kind : {PixelFxKind::HeavyCleave, PixelFxKind::HammerSmash}) {
        PixelFx    fx(kind);
        const auto before = fx.configurationValue();
        for (const char* json : {"-1", "4294967296", "1.5", "null", "[]", "\"stone\""}) {
            auto value = before;
            auto input = eve::Value::fromJson(json);
            REQUIRE(input);
            value.find("options")->set("stone", input.value());
            REQUIRE(!PixelFx::prepareConfigurationValue(kind, value));
            REQUIRE(fx.configurationValue() == before);
        }
    }
}
}  // namespace test_pixelfx_melee_heavy_unit
