// Pinned upstream UI counter and paper-flight raster fixtures.
#include <algorithm>
#include <array>
#include <limits>
#include "stylize/PixelFx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_ui_counters_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.ui.counters.countdown.dense") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 2816838882u, 2175147826u},
        {2, 3578915591u, 3909335267u},
        {2.3916666666666666, 2356669497u, 231083870u},
        {2.4, 3376815015u, 937827570u},
        {2.691666666666667, 712630487u, 2843829021u},
        {2.7, 2589407407u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value    = fx.configurationValue();
    auto    prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.var") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3152291352u, 1705248440u},
        {0.008333333333333333, 2266628404u, 1473001470u},
        {0.09166666666666666, 1818825222u, 2217081725u},
        {0.1, 2123665253u, 4113881111u},
        {0.10833333333333334, 3159425917u, 2685966847u},
        {0.2, 935692769u, 160088634u},
        {0.25833333333333336, 1346142916u, 4202200319u},
        {0.2916666666666667, 206426903u, 3843234226u},
        {0.3, 1454583687u, 382658939u},
        {0.30833333333333335, 784671016u, 714785238u},
        {0.575, 4033715693u, 2952593275u},
        {0.5833333333333334, 3485736770u, 2866926994u},
        {0.5916666666666667, 3375236452u, 2142870452u},
        {0.6166666666666667, 113933002u, 651139540u},
        {0.625, 2053744609u, 1160380840u},
        {1, 243938437u, 1429953264u},
        {1.1916666666666667, 4283262006u, 1462913568u},
        {1.2, 2357086624u, 2543713236u},
        {1.2083333333333333, 3015551799u, 887866081u},
        {1.8583333333333334, 3872620217u, 2868082835u},
        {1.8666666666666667, 2749253571u, 929060470u},
        {2, 3990926287u, 970667131u},
        {2.3916666666666666, 487654680u, 2305595608u},
        {2.4, 2048734167u, 4104073881u},
        {2.691666666666667, 1946916558u, 3556536219u},
        {2.7, 3257475574u, 771270760u},
        {2.7583333333333333, 575475345u, 1165625105u},
        {2.908333333333333, 3196372668u, 1791678759u},
        {3, 1981626821u, 3774618496u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(2.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json("START!")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("go", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.zero") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 855587529u, 325166001u},
        {0.008333333333333333, 2989390112u, 1168727828u},
        {0.09166666666666666, 3266701784u, 3344396734u},
        {0.1, 1194887341u, 414638286u},
        {0.10833333333333334, 1607441334u, 4239082846u},
        {0.2, 1823688564u, 4130163533u},
        {0.25833333333333336, 2871233539u, 1126940692u},
        {0.2916666666666667, 1810876579u, 563860957u},
        {0.3, 1069015313u, 1903110858u},
        {0.30833333333333335, 411125533u, 1747879352u},
        {0.575, 404626013u, 1843358396u},
        {0.5833333333333334, 2060607938u, 1660562363u},
        {0.5916666666666667, 1897517663u, 3597298963u},
        {0.6166666666666667, 650439635u, 1292547144u},
        {0.625, 3866233065u, 573593035u},
        {1, 2124297620u, 257842533u},
        {1.1916666666666667, 2694229073u, 655818174u},
        {1.2, 941551839u, 2693273577u},
        {1.2083333333333333, 4106951292u, 3419346968u},
        {1.8583333333333334, 681118312u, 2586149262u},
        {1.8666666666666667, 3001587164u, 886600977u},
        {2, 1905565923u, 3728001169u},
        {2.3916666666666666, 1036348818u, 1617391847u},
        {2.4, 2818222349u, 1610836932u},
        {2.691666666666667, 2393039909u, 4112231690u},
        {2.7, 240556791u, 3492316411u},
        {2.7583333333333333, 3032981619u, 1649164936u},
        {2.908333333333333, 1166538647u, 3048016055u},
        {3, 58802225u, 3332864444u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.max") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3060905509u, 408143281u},
        {0.008333333333333333, 865633960u, 1325905474u},
        {0.09166666666666666, 76036506u, 1466557561u},
        {0.1, 1125725952u, 3930399081u},
        {0.10833333333333334, 3575955047u, 3991184549u},
        {0.2, 2438639249u, 210829579u},
        {0.25833333333333336, 3952469697u, 907443236u},
        {0.2916666666666667, 1993679938u, 2186493036u},
        {0.3, 4157585697u, 2334498092u},
        {0.30833333333333335, 2251042532u, 3246020102u},
        {0.575, 3656822554u, 1050671080u},
        {0.5833333333333334, 264347448u, 4251864909u},
        {0.5916666666666667, 4203581562u, 4156590095u},
        {0.6166666666666667, 2675573494u, 1563534344u},
        {0.625, 1736995769u, 3316958910u},
        {1, 2446077414u, 3895379011u},
        {1.1916666666666667, 2348192569u, 2122272897u},
        {1.2, 2494233295u, 407476108u},
        {1.2083333333333333, 661353404u, 1525859364u},
        {1.8583333333333334, 237738044u, 3305775819u},
        {1.8666666666666667, 2387343099u, 3328055717u},
        {2, 3565666776u, 3081369435u},
        {2.3916666666666666, 685004740u, 802861769u},
        {2.4, 1498572815u, 2721031203u},
        {2.691666666666667, 953798827u, 2206022957u},
        {2.7, 1295834817u, 4196754254u},
        {2.7583333333333333, 4262325379u, 48908871u},
        {2.908333333333333, 2205013867u, 1611846439u},
        {3, 1981626821u, 1246614706u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.single") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2533273901u, 2465604982u},
        {0.008333333333333333, 47100957u, 3035526920u},
        {0.09166666666666666, 1065135689u, 2891408836u},
        {0.1, 1373051313u, 2034315508u},
        {0.10833333333333334, 1160270541u, 314851589u},
        {0.2, 1425918913u, 2381325153u},
        {0.25833333333333336, 2394006233u, 2133101593u},
        {0.2916666666666667, 2735001697u, 3262744081u},
        {0.3, 1199061045u, 2488987598u},
        {0.30833333333333335, 528278145u, 4117931545u},
        {0.575, 122457381u, 2032126094u},
        {0.5833333333333334, 1999135533u, 955490678u},
        {0.5916666666666667, 3449696533u, 3832088239u},
        {0.6166666666666667, 3547431693u, 883826551u},
        {0.625, 2086640109u, 2761864580u},
        {1, 4229037601u, 2979376427u},
        {1.1916666666666667, 8661u, 3557635882u},
        {1.2, 1707420777u, 311495511u},
        {1.2083333333333333, 232683261u, 1905359407u},
        {1.8583333333333334, 4012993361u, 2531027519u},
        {1.8666666666666667, 646276527u, 414739078u},
        {2, 2294377623u, 557031716u},
        {2.3916666666666666, 2356669497u, 231083870u},
        {2.4, 3376815015u, 937827570u},
        {2.691666666666667, 712630487u, 2843829021u},
        {2.7, 2589407407u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.many") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3943274024u, 213006808u},
        {0.008333333333333333, 2785893732u, 3818726983u},
        {0.09166666666666666, 2415645649u, 2131248094u},
        {0.1, 3997011795u, 1548510281u},
        {0.10833333333333334, 2196644685u, 95319632u},
        {0.2, 127574732u, 1146932799u},
        {0.25833333333333336, 259640471u, 1750652159u},
        {0.2916666666666667, 742155261u, 818273910u},
        {0.3, 2955888678u, 418786412u},
        {0.30833333333333335, 2968587919u, 828915992u},
        {0.575, 1574633242u, 3982275805u},
        {0.5833333333333334, 4101661174u, 1162941343u},
        {0.5916666666666667, 1212420069u, 654059863u},
        {0.6166666666666667, 960687452u, 3103589745u},
        {0.625, 2240011740u, 2528697916u},
        {1, 2304843508u, 1674644118u},
        {1.1916666666666667, 2837393963u, 36948823u},
        {1.2, 3614373562u, 355282033u},
        {1.2083333333333333, 3903412531u, 57093232u},
        {1.8583333333333334, 899281219u, 3323577110u},
        {1.8666666666666667, 1653376308u, 719944092u},
        {2, 1176943543u, 2795783779u},
        {2.3916666666666666, 2356669497u, 231083870u},
        {2.4, 3376815015u, 937827570u},
        {2.691666666666667, 712630487u, 2843829021u},
        {2.7, 2589407407u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.dense") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 3590990578u, 3987102208u},
        {0.30833333333333335, 2860642827u, 3074079653u},
        {0.575, 2876102507u, 854067097u},
        {0.5833333333333334, 1038478589u, 178258340u},
        {0.5916666666666667, 3409887132u, 3203092431u},
        {0.6166666666666667, 4142783794u, 430525413u},
        {0.625, 2018535501u, 607987110u},
        {1, 2746948151u, 3614524119u},
        {1.1916666666666667, 2542016919u, 3490619659u},
        {1.2, 2172382409u, 2529876994u},
        {1.2083333333333333, 3882298656u, 84917212u},
        {1.8583333333333334, 452417833u, 4038097157u},
        {1.8666666666666667, 452417833u, 4038097157u},
        {2, 452417833u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value    = fx.configurationValue();
    auto    prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.var") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2065889815u, 3502694725u},
        {0.008333333333333333, 3078458919u, 2514093079u},
        {0.09166666666666666, 2727317626u, 3938154977u},
        {0.1, 1766758968u, 588299508u},
        {0.10833333333333334, 3348372691u, 3029814370u},
        {0.2, 4056554126u, 1090010863u},
        {0.25833333333333336, 1997959962u, 3805797648u},
        {0.2916666666666667, 2675833770u, 3584564783u},
        {0.3, 2929530827u, 3288225892u},
        {0.30833333333333335, 1682668154u, 3278936232u},
        {0.575, 1976679573u, 1405476495u},
        {0.5833333333333334, 1623189141u, 1671839568u},
        {0.5916666666666667, 445787517u, 184765619u},
        {0.6166666666666667, 3603570617u, 2507343052u},
        {0.625, 565580513u, 444917630u},
        {1, 547061773u, 2744274714u},
        {1.1916666666666667, 3750897692u, 3382391125u},
        {1.2, 1976691731u, 23821099u},
        {1.2083333333333333, 464795982u, 3166532308u},
        {1.8583333333333334, 835054348u, 3487226885u},
        {1.8666666666666667, 835054348u, 3487226885u},
        {2, 835054348u, 3487226885u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(6.5)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(99.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.zero") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 304624514u, 3888354456u},
        {0.30833333333333335, 2423918676u, 2071461945u},
        {0.575, 2797979506u, 819736911u},
        {0.5833333333333334, 3529459466u, 2051640115u},
        {0.5916666666666667, 2610186383u, 1681566164u},
        {0.6166666666666667, 1135688916u, 2746287258u},
        {0.625, 3608335267u, 844345241u},
        {1, 3465041193u, 685764117u},
        {1.1916666666666667, 4245129900u, 1569985590u},
        {1.2, 3370611291u, 3187428276u},
        {1.2083333333333333, 1965348799u, 271347793u},
        {1.8583333333333334, 452417833u, 2601421326u},
        {1.8666666666666667, 452417833u, 1961117514u},
        {2, 452417833u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.max") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 1644840613u, 2347131515u},
        {0.30833333333333335, 714021220u, 1604189051u},
        {0.575, 474732220u, 750880995u},
        {0.5833333333333334, 2099638639u, 2030380409u},
        {0.5916666666666667, 734490387u, 2186310029u},
        {0.6166666666666667, 1595664911u, 2300475695u},
        {0.625, 1606329997u, 2208372557u},
        {1, 3262702447u, 2860428908u},
        {1.1916666666666667, 2264184842u, 1502025258u},
        {1.2, 2922857983u, 782617158u},
        {1.2083333333333333, 2792205712u, 2193423613u},
        {1.8583333333333334, 452417833u, 4038097157u},
        {1.8666666666666667, 452417833u, 4038097157u},
        {2, 452417833u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.single") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2704141251u, 474094773u},
        {0.008333333333333333, 2576923923u, 2016664308u},
        {0.09166666666666666, 2685629587u, 216667324u},
        {0.1, 304647283u, 4093759735u},
        {0.10833333333333334, 3216795267u, 480650958u},
        {0.2, 1275076823u, 1166395698u},
        {0.25833333333333336, 1183618459u, 141641245u},
        {0.2916666666666667, 1808619123u, 149024606u},
        {0.3, 723549844u, 1786447329u},
        {0.30833333333333335, 3968075834u, 3237554597u},
        {0.575, 3709674126u, 1571384530u},
        {0.5833333333333334, 2800581345u, 3564083456u},
        {0.5916666666666667, 2470535726u, 670764848u},
        {0.6166666666666667, 497278691u, 3060968121u},
        {0.625, 4161958366u, 2089066228u},
        {1, 538785802u, 1291008276u},
        {1.1916666666666667, 3052481064u, 2373578660u},
        {1.2, 1663920958u, 18743350u},
        {1.2083333333333333, 896174716u, 3220366384u},
        {1.8583333333333334, 3063892009u, 3765120389u},
        {1.8666666666666667, 3063892009u, 3765120389u},
        {2, 3063892009u, 3765120389u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.many") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 394360372u, 4204798725u},
        {0.008333333333333333, 1320836482u, 2381278946u},
        {0.09166666666666666, 359340439u, 3861336639u},
        {0.1, 1287866305u, 1748370228u},
        {0.10833333333333334, 3671504579u, 1140075315u},
        {0.2, 2479546396u, 2802570483u},
        {0.25833333333333336, 3898685960u, 3376587942u},
        {0.2916666666666667, 158309572u, 4058166288u},
        {0.3, 902799119u, 576714293u},
        {0.30833333333333335, 1112091301u, 2621905612u},
        {0.575, 3124400534u, 3833239120u},
        {0.5833333333333334, 2730131658u, 2397072750u},
        {0.5916666666666667, 1112354988u, 1557669722u},
        {0.6166666666666667, 3352956470u, 2889513803u},
        {0.625, 4029340050u, 462984458u},
        {1, 1315481928u, 263424804u},
        {1.1916666666666667, 424840354u, 2274758917u},
        {1.2, 798286007u, 3984497073u},
        {1.2083333333333333, 3720287016u, 144937728u},
        {1.8583333333333334, 1267770202u, 3022799621u},
        {1.8666666666666667, 1267770202u, 3022799621u},
        {2, 1267770202u, 3022799621u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.dense") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3297919319u, 931068869u},
        {0.008333333333333333, 3297919319u, 931068869u},
        {0.09166666666666666, 3297919319u, 931068869u},
        {0.1, 3297919319u, 931068869u},
        {0.10833333333333334, 4066425787u, 3384579589u},
        {0.2, 1690877444u, 1843163984u},
        {0.25833333333333336, 404530705u, 562654066u},
        {0.2916666666666667, 2157215149u, 878273678u},
        {0.3, 754403922u, 2134156862u},
        {0.30833333333333335, 3528145561u, 3705244910u},
        {0.575, 691696371u, 1969801905u},
        {0.5833333333333334, 530248015u, 3513161177u},
        {0.5916666666666667, 3023153496u, 3254595068u},
        {0.6166666666666667, 917088552u, 3089055562u},
        {0.625, 4237347807u, 2555958497u},
        {1, 2325011711u, 3206728511u},
        {1.1916666666666667, 370422190u, 2658937988u},
        {1.2, 968502019u, 1798530284u},
        {1.2083333333333333, 2379641437u, 3638850537u},
        {1.8583333333333334, 3502511453u, 2896009881u},
        {1.8666666666666667, 219416863u, 1581761490u},
        {2, 3264184643u, 3818453870u},
        {2.3916666666666666, 2672541135u, 1315497323u},
        {2.4, 1658814022u, 3671370953u},
        {2.691666666666667, 4053741670u, 3367959198u},
        {2.7, 1966684951u, 3532186560u},
        {2.7583333333333333, 1085078264u, 424578106u},
        {2.908333333333333, 1188165671u, 2613883017u},
        {3, 378929537u, 931068869u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value    = fx.configurationValue();
    auto    prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.var") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2404872486u, 931068869u},
        {0.008333333333333333, 2404872486u, 931068869u},
        {0.09166666666666666, 2404872486u, 931068869u},
        {0.1, 2404872486u, 931068869u},
        {0.10833333333333334, 393782978u, 2659128315u},
        {0.2, 3872894375u, 3577889666u},
        {0.25833333333333336, 1635807592u, 2142045555u},
        {0.2916666666666667, 1367538792u, 3836686680u},
        {0.3, 732854464u, 3244572390u},
        {0.30833333333333335, 3185785082u, 1363384949u},
        {0.575, 3050092413u, 1508738171u},
        {0.5833333333333334, 527125309u, 661995546u},
        {0.5916666666666667, 3172165014u, 684171726u},
        {0.6166666666666667, 640963360u, 540248348u},
        {0.625, 1792381692u, 3301342047u},
        {1, 2006757061u, 299989592u},
        {1.1916666666666667, 3470087835u, 3968044765u},
        {1.2, 120515518u, 794936490u},
        {1.2083333333333333, 3629060791u, 2086622672u},
        {1.8583333333333334, 2952313457u, 2326265515u},
        {1.8666666666666667, 3774543398u, 232079442u},
        {2, 2807674195u, 1056619547u},
        {2.3916666666666666, 1909408517u, 3122528409u},
        {2.4, 3314473803u, 4015308823u},
        {2.691666666666667, 2088382964u, 88422375u},
        {2.7, 2183064373u, 2971141151u},
        {2.7583333333333333, 3051887764u, 1902087143u},
        {2.908333333333333, 1111384083u, 1411821553u},
        {3, 3789242541u, 3163740220u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(123)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.zero") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3297919319u, 931068869u},
        {0.008333333333333333, 3297919319u, 931068869u},
        {0.09166666666666666, 3297919319u, 931068869u},
        {0.1, 3297919319u, 931068869u},
        {0.10833333333333334, 4268825891u, 2905231811u},
        {0.2, 621863017u, 4277216729u},
        {0.25833333333333336, 4112421038u, 3398897245u},
        {0.2916666666666667, 3786548241u, 1390355032u},
        {0.3, 2138458691u, 3599294431u},
        {0.30833333333333335, 1100056728u, 3727058431u},
        {0.575, 910492379u, 1761023693u},
        {0.5833333333333334, 664013187u, 2580271738u},
        {0.5916666666666667, 3267753163u, 2960558260u},
        {0.6166666666666667, 4284075063u, 1539474458u},
        {0.625, 441559522u, 1220298904u},
        {1, 1393074800u, 1584052711u},
        {1.1916666666666667, 1497069512u, 3900089794u},
        {1.2, 1477931436u, 2884188217u},
        {1.2083333333333333, 1418215135u, 1056032075u},
        {1.8583333333333334, 855353778u, 417901232u},
        {1.8666666666666667, 3440850286u, 1958964887u},
        {2, 3223121646u, 2972828833u},
        {2.3916666666666666, 847801599u, 1524323027u},
        {2.4, 3778587350u, 3710247504u},
        {2.691666666666667, 568579351u, 1243556579u},
        {2.7, 465155350u, 3457431597u},
        {2.7583333333333333, 1998620867u, 3637423421u},
        {2.908333333333333, 1953569634u, 2701581497u},
        {3, 2717474641u, 931068869u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.max") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 3297919319u, 931068869u},
        {0.008333333333333333, 3297919319u, 931068869u},
        {0.09166666666666666, 3297919319u, 931068869u},
        {0.1, 3297919319u, 931068869u},
        {0.10833333333333334, 88590599u, 1621028827u},
        {0.2, 1810243468u, 1852724162u},
        {0.25833333333333336, 874693517u, 2798434264u},
        {0.2916666666666667, 4027307413u, 1912934908u},
        {0.3, 3513423939u, 4271410169u},
        {0.30833333333333335, 3826638643u, 1518428613u},
        {0.575, 3175994330u, 1283242464u},
        {0.5833333333333334, 2454247299u, 828758228u},
        {0.5916666666666667, 493772734u, 3275418150u},
        {0.6166666666666667, 219192739u, 1052653044u},
        {0.625, 2993386019u, 1537957067u},
        {1, 1346106135u, 2627750505u},
        {1.1916666666666667, 1848347811u, 879079082u},
        {1.2, 2814383752u, 749427800u},
        {1.2083333333333333, 4252772852u, 3893629905u},
        {1.8583333333333334, 3770887950u, 2033397054u},
        {1.8666666666666667, 3220541326u, 3781014199u},
        {2, 875208349u, 3876919542u},
        {2.3916666666666666, 531546695u, 1955449251u},
        {2.4, 2898039430u, 2101089063u},
        {2.691666666666667, 480590496u, 2265543289u},
        {2.7, 2042502172u, 440779345u},
        {2.7583333333333333, 3181989590u, 3990172901u},
        {2.908333333333333, 3119925803u, 931068869u},
        {3, 3770036879u, 931068869u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.single") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 1156031677u, 931068869u},
        {0.008333333333333333, 1156031677u, 931068869u},
        {0.09166666666666666, 1156031677u, 931068869u},
        {0.1, 1156031677u, 931068869u},
        {0.10833333333333334, 67146538u, 2860599925u},
        {0.2, 2626907112u, 2680778080u},
        {0.25833333333333336, 1348983694u, 2812150000u},
        {0.2916666666666667, 1920718503u, 2049764039u},
        {0.3, 670857627u, 1671869805u},
        {0.30833333333333335, 266093292u, 3986503379u},
        {0.575, 386066814u, 1862383725u},
        {0.5833333333333334, 3485927354u, 656962520u},
        {0.5916666666666667, 1476174703u, 1352691881u},
        {0.6166666666666667, 3152899808u, 1318982199u},
        {0.625, 408882489u, 3219541495u},
        {1, 1852371099u, 1070542450u},
        {1.1916666666666667, 4249841327u, 3954148075u},
        {1.2, 2483356594u, 3837079727u},
        {1.2083333333333333, 4131114710u, 2832370179u},
        {1.8583333333333334, 1887335477u, 2835933590u},
        {1.8666666666666667, 2832166877u, 2580643168u},
        {2, 1666946823u, 442636623u},
        {2.3916666666666666, 1501232626u, 3192058929u},
        {2.4, 199201007u, 3329644005u},
        {2.691666666666667, 1054297850u, 3495068041u},
        {2.7, 4247690793u, 1459183602u},
        {2.7583333333333333, 2307525240u, 3627668861u},
        {2.908333333333333, 665114088u, 370106660u},
        {3, 1115929821u, 931068869u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.confetti.many") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 532807477u, 931068869u},
        {0.008333333333333333, 532807477u, 931068869u},
        {0.09166666666666666, 532807477u, 931068869u},
        {0.1, 532807477u, 931068869u},
        {0.10833333333333334, 384264007u, 4114552303u},
        {0.2, 1308805740u, 3482639438u},
        {0.25833333333333336, 643941858u, 750144837u},
        {0.2916666666666667, 3334453388u, 1841994512u},
        {0.3, 2837338977u, 2817446808u},
        {0.30833333333333335, 2215243777u, 1220689457u},
        {0.575, 4292619051u, 276334204u},
        {0.5833333333333334, 3420243617u, 2910730963u},
        {0.5916666666666667, 2687025446u, 415866375u},
        {0.6166666666666667, 3019911935u, 1476172495u},
        {0.625, 3854767374u, 3773748737u},
        {1, 1446277499u, 2048087548u},
        {1.1916666666666667, 2157909321u, 1381216868u},
        {1.2, 566077401u, 2716368786u},
        {1.2083333333333333, 1426331633u, 2233967374u},
        {1.8583333333333334, 1141512798u, 693326614u},
        {1.8666666666666667, 1083542997u, 3136413328u},
        {2, 2664991543u, 1000597613u},
        {2.3916666666666666, 1322857281u, 2610837086u},
        {2.4, 3034013210u, 1323990120u},
        {2.691666666666667, 966129042u, 1307191806u},
        {2.7, 3972757348u, 2300759177u},
        {2.7583333333333333, 495507388u, 1601018138u},
        {2.908333333333333, 130381820u, 2740291563u},
        {3, 3589552294u, 931068869u},
        {3.341666666666667, 931068869u, 931068869u},
        {3.4, 931068869u, 931068869u},
        {5, 931068869u, 931068869u},
    };
    PixelFx fx(PixelFxKind::Confetti);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Confetti, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_0") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 4149702173u, 2163692465u},
        {0.008333333333333333, 2519733390u, 4032733079u},
        {0.09166666666666666, 3266422112u, 2266274713u},
        {0.1, 4247976803u, 1027546881u},
        {0.10833333333333334, 1322277522u, 2895885984u},
        {0.2, 4192585677u, 3141564775u},
        {0.25833333333333336, 913252147u, 1571805399u},
        {0.2916666666666667, 4028202703u, 262097594u},
        {0.3, 1379527421u, 3014022371u},
        {0.30833333333333335, 1868821126u, 3717375287u},
        {0.575, 704280004u, 3890765799u},
        {0.5833333333333334, 2913598473u, 2528389471u},
        {0.5916666666666667, 2094091148u, 3857420321u},
        {0.6166666666666667, 489472187u, 2477861879u},
        {0.625, 2401125302u, 3937533863u},
        {1, 1316977021u, 2462263511u},
        {1.1916666666666667, 3701730644u, 3329181514u},
        {1.2, 1234113508u, 416850078u},
        {1.2083333333333333, 2889734498u, 4197777604u},
        {1.8583333333333334, 1981626821u, 1981626821u},
        {1.8666666666666667, 1981626821u, 1981626821u},
        {2, 1981626821u, 1981626821u},
        {2.3916666666666666, 1981626821u, 1981626821u},
        {2.4, 1981626821u, 1981626821u},
        {2.691666666666667, 1981626821u, 1981626821u},
        {2.7, 1981626821u, 1981626821u},
        {2.7583333333333333, 1981626821u, 1981626821u},
        {2.908333333333333, 1981626821u, 1981626821u},
        {3, 1981626821u, 1981626821u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_1") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 4149702173u, 2163692465u},
        {0.008333333333333333, 2519733390u, 4032733079u},
        {0.09166666666666666, 3266422112u, 2266274713u},
        {0.1, 4247976803u, 1027546881u},
        {0.10833333333333334, 1322277522u, 2895885984u},
        {0.2, 4192585677u, 3141564775u},
        {0.25833333333333336, 913252147u, 1571805399u},
        {0.2916666666666667, 4028202703u, 262097594u},
        {0.3, 1379527421u, 3014022371u},
        {0.30833333333333335, 1868821126u, 3717375287u},
        {0.575, 704280004u, 3890765799u},
        {0.5833333333333334, 2913598473u, 2528389471u},
        {0.5916666666666667, 2094091148u, 3857420321u},
        {0.6166666666666667, 489472187u, 2477861879u},
        {0.625, 2401125302u, 3937533863u},
        {1, 1316977021u, 2462263511u},
        {1.1916666666666667, 3701730644u, 3329181514u},
        {1.2, 1234113508u, 416850078u},
        {1.2083333333333333, 2889734498u, 4197777604u},
        {1.8583333333333334, 1981626821u, 1981626821u},
        {1.8666666666666667, 1981626821u, 1981626821u},
        {2, 1981626821u, 1981626821u},
        {2.3916666666666666, 1981626821u, 1981626821u},
        {2.4, 1981626821u, 1981626821u},
        {2.691666666666667, 1981626821u, 1981626821u},
        {2.7, 1981626821u, 1981626821u},
        {2.7583333333333333, 1981626821u, 1981626821u},
        {2.908333333333333, 1981626821u, 1981626821u},
        {3, 1981626821u, 1981626821u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(1.49)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_2") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 949634016u, 2163692465u},
        {0.008333333333333333, 1686773193u, 4032733079u},
        {0.09166666666666666, 1389351485u, 2266274713u},
        {0.1, 770103926u, 1027546881u},
        {0.10833333333333334, 3572392927u, 2895885984u},
        {0.2, 1141824133u, 3141564775u},
        {0.25833333333333336, 1015815998u, 1571805399u},
        {0.2916666666666667, 2268399415u, 262097594u},
        {0.3, 1251791649u, 3014022371u},
        {0.30833333333333335, 2371879237u, 3717375287u},
        {0.575, 3022131348u, 3890765799u},
        {0.5833333333333334, 274652333u, 2528389471u},
        {0.5916666666666667, 72187912u, 3857420321u},
        {0.6166666666666667, 3382358502u, 2477861879u},
        {0.625, 1657049147u, 2377278112u},
        {1, 541868046u, 1368509220u},
        {1.1916666666666667, 3035649124u, 867538281u},
        {1.2, 3382440558u, 1314793137u},
        {1.2083333333333333, 2053096060u, 4027401080u},
        {1.8583333333333334, 3963418755u, 451175167u},
        {1.8666666666666667, 1109827662u, 602364574u},
        {2, 2253963923u, 236315875u},
        {2.3916666666666666, 1981626821u, 1383233072u},
        {2.4, 1981626821u, 394313163u},
        {2.691666666666667, 1981626821u, 1981626821u},
        {2.7, 1981626821u, 1981626821u},
        {2.7583333333333333, 1981626821u, 1981626821u},
        {2.908333333333333, 1981626821u, 1981626821u},
        {3, 1981626821u, 1981626821u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(1.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_3") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 949634016u, 2163692465u},
        {0.008333333333333333, 1686773193u, 4032733079u},
        {0.09166666666666666, 1389351485u, 2266274713u},
        {0.1, 770103926u, 1027546881u},
        {0.10833333333333334, 3572392927u, 2895885984u},
        {0.2, 1141824133u, 3141564775u},
        {0.25833333333333336, 1015815998u, 1571805399u},
        {0.2916666666666667, 2268399415u, 262097594u},
        {0.3, 1251791649u, 3014022371u},
        {0.30833333333333335, 2371879237u, 3717375287u},
        {0.575, 3022131348u, 3890765799u},
        {0.5833333333333334, 274652333u, 2528389471u},
        {0.5916666666666667, 72187912u, 3857420321u},
        {0.6166666666666667, 3382358502u, 2477861879u},
        {0.625, 1657049147u, 2377278112u},
        {1, 541868046u, 1368509220u},
        {1.1916666666666667, 3035649124u, 867538281u},
        {1.2, 3382440558u, 1314793137u},
        {1.2083333333333333, 2053096060u, 4027401080u},
        {1.8583333333333334, 3963418755u, 451175167u},
        {1.8666666666666667, 1109827662u, 602364574u},
        {2, 2253963923u, 236315875u},
        {2.3916666666666666, 1981626821u, 1383233072u},
        {2.4, 1981626821u, 394313163u},
        {2.691666666666667, 1981626821u, 1981626821u},
        {2.7, 1981626821u, 1981626821u},
        {2.7583333333333333, 1981626821u, 1981626821u},
        {2.908333333333333, 1981626821u, 1981626821u},
        {3, 1981626821u, 1981626821u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_4") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 949634016u, 2163692465u},
        {0.008333333333333333, 1686773193u, 4032733079u},
        {0.09166666666666666, 1389351485u, 2266274713u},
        {0.1, 770103926u, 1027546881u},
        {0.10833333333333334, 3572392927u, 2895885984u},
        {0.2, 1141824133u, 3141564775u},
        {0.25833333333333336, 1015815998u, 1571805399u},
        {0.2916666666666667, 2268399415u, 262097594u},
        {0.3, 1251791649u, 3014022371u},
        {0.30833333333333335, 2371879237u, 3717375287u},
        {0.575, 3022131348u, 3890765799u},
        {0.5833333333333334, 274652333u, 2528389471u},
        {0.5916666666666667, 72187912u, 3857420321u},
        {0.6166666666666667, 3382358502u, 2477861879u},
        {0.625, 1657049147u, 2377278112u},
        {1, 541868046u, 1368509220u},
        {1.1916666666666667, 3035649124u, 867538281u},
        {1.2, 3382440558u, 1314793137u},
        {1.2083333333333333, 2053096060u, 4027401080u},
        {1.8583333333333334, 3963418755u, 451175167u},
        {1.8666666666666667, 1109827662u, 602364574u},
        {2, 2253963923u, 236315875u},
        {2.3916666666666666, 1981626821u, 1383233072u},
        {2.4, 1981626821u, 394313163u},
        {2.691666666666667, 1981626821u, 1981626821u},
        {2.7, 1981626821u, 1981626821u},
        {2.7583333333333333, 1981626821u, 1981626821u},
        {2.908333333333333, 1981626821u, 1981626821u},
        {3, 1981626821u, 1981626821u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2.49)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_5") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 2816838882u, 2175147826u},
        {2, 3578915591u, 3909335267u},
        {2.3916666666666666, 2356669497u, 231083870u},
        {2.4, 3376815015u, 937827570u},
        {2.691666666666667, 712630487u, 2843829021u},
        {2.7, 2589407407u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.from_6") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 2816838882u, 2175147826u},
        {2, 3578915591u, 3909335267u},
        {2.3916666666666666, 2356669497u, 231083870u},
        {2.4, 3376815015u, 937827570u},
        {2.691666666666667, 712630487u, 2843829021u},
        {2.7, 2589407407u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(3)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_0") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1365130886u, 2175147826u},
        {2, 2070443865u, 3909335267u},
        {2.3916666666666666, 4254543639u, 231083870u},
        {2.4, 1083324885u, 937827570u},
        {2.691666666666667, 1780024619u, 2843829021u},
        {2.7, 642089145u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json("")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_1") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1365130886u, 2175147826u},
        {2, 2070443865u, 3909335267u},
        {2.3916666666666666, 4254543639u, 231083870u},
        {2.4, 1083324885u, 937827570u},
        {2.691666666666667, 1780024619u, 2843829021u},
        {2.7, 642089145u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(" ")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_2") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 3539955936u, 2175147826u},
        {2, 4242406475u, 3909335267u},
        {2.3916666666666666, 3051924713u, 231083870u},
        {2.4, 3867523179u, 937827570u},
        {2.691666666666667, 2678482983u, 2843829021u},
        {2.7, 704837335u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json("start")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_3") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1686702908u, 2175147826u},
        {2, 356647022u, 3909335267u},
        {2.3916666666666666, 4032969168u, 231083870u},
        {2.4, 991534501u, 937827570u},
        {2.691666666666667, 2156885157u, 2843829021u},
        {2.7, 4258380989u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json("I1.MW!?")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_4") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1598366632u, 2175147826u},
        {2, 1811283301u, 3909335267u},
        {2.3916666666666666, 3855077362u, 231083870u},
        {2.4, 2563198588u, 937827570u},
        {2.691666666666667, 2650516907u, 2843829021u},
        {2.7, 1245728491u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json("ıſß中🙂")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_5") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 3547644226u, 2175147826u},
        {2, 2548543578u, 3909335267u},
        {2.3916666666666666, 678738575u, 231083870u},
        {2.4, 304830119u, 937827570u},
        {2.691666666666667, 128736003u, 2843829021u},
        {2.7, 2557816429u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input =
            eve::Value::fromJson(R"json("MMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMMM")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.word_6") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1365130886u, 2175147826u},
        {2, 2070443865u, 3909335267u},
        {2.3916666666666666, 4254543639u, 231083870u},
        {2.4, 1083324885u, 937827570u},
        {2.691666666666667, 1780024619u, 2843829021u},
        {2.7, 642089145u, 2238060247u},
        {2.7583333333333333, 2762063011u, 3310841117u},
        {2.908333333333333, 1258389743u, 581621539u},
        {3, 3047181617u, 2738258560u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json("🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂🙂")json");
        REQUIRE(input);
        value.find("options")->set("word", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.go_0") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 2755402992u, 1197684954u},
        {2, 4097711537u, 2471377474u},
        {2.3916666666666666, 4129949249u, 2009911386u},
        {2.4, 269616269u, 4019860836u},
        {2.691666666666667, 3732554917u, 2790778391u},
        {2.7, 385705709u, 1409592232u},
        {2.7583333333333333, 3922583621u, 1743911462u},
        {2.908333333333333, 3065183233u, 614415055u},
        {3, 4152723665u, 1825472176u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("go", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.go_1") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 1038739780u, 704676402u},
        {2, 2360497640u, 3698384502u},
        {2.3916666666666666, 3261025941u, 2924834725u},
        {2.4, 1526954213u, 2663270269u},
        {2.691666666666667, 2583490177u, 4291979482u},
        {2.7, 4232864468u, 1791665504u},
        {2.7583333333333333, 4119693285u, 4150872966u},
        {2.908333333333333, 699519233u, 1931345128u},
        {3, 238905625u, 2717103100u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4278190080, 4278219537, 4278248994, 4278278451, 4278307908])json");
        REQUIRE(input);
        value.find("options")->set("go", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.countdown.go_2") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 458834997u, 2163692465u},
        {0.008333333333333333, 5699537u, 4032733079u},
        {0.09166666666666666, 942020312u, 2266274713u},
        {0.1, 3910159336u, 1027546881u},
        {0.10833333333333334, 1072348533u, 2895885984u},
        {0.2, 596741837u, 3141564775u},
        {0.25833333333333336, 1387255808u, 1571805399u},
        {0.2916666666666667, 4009681741u, 262097594u},
        {0.3, 1889223147u, 3014022371u},
        {0.30833333333333335, 3204050291u, 3717375287u},
        {0.575, 57579231u, 3890765799u},
        {0.5833333333333334, 1736581094u, 2528389471u},
        {0.5916666666666667, 998566579u, 3857420321u},
        {0.6166666666666667, 3600579568u, 2477861879u},
        {0.625, 2929159363u, 2377278112u},
        {1, 4039504027u, 1368509220u},
        {1.1916666666666667, 576050665u, 867538281u},
        {1.2, 3565963267u, 1314793137u},
        {1.2083333333333333, 4240908985u, 4027401080u},
        {1.8583333333333334, 920202811u, 2094343495u},
        {1.8666666666666667, 3913525735u, 2401837913u},
        {2, 4076443500u, 1441898616u},
        {2.3916666666666666, 2618047819u, 2052911580u},
        {2.4, 141889206u, 3112909938u},
        {2.691666666666667, 245060716u, 2760793104u},
        {2.7, 2489736540u, 1415991737u},
        {2.7583333333333333, 4123689728u, 1145789942u},
        {2.908333333333333, 3496847592u, 1868313147u},
        {3, 2007188993u, 1672357468u},
        {3.341666666666667, 1981626821u, 1981626821u},
        {3.4, 1981626821u, 1981626821u},
        {5, 1981626821u, 1981626821u},
    };
    PixelFx fx(PixelFxKind::Countdown);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("go", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.count_0") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 2992427726u, 522665708u},
        {0.30833333333333335, 400210493u, 3557147770u},
        {0.575, 3031685191u, 2565803331u},
        {0.5833333333333334, 1314311466u, 4278322991u},
        {0.5916666666666667, 3814213672u, 1025416116u},
        {0.6166666666666667, 2456380164u, 474772590u},
        {0.625, 3423596720u, 889783147u},
        {1, 535036649u, 478515625u},
        {1.1916666666666667, 3598176556u, 2941747958u},
        {1.2, 3598176556u, 4038097157u},
        {1.2083333333333333, 3598176556u, 4038097157u},
        {1.8583333333333334, 3598176556u, 4038097157u},
        {1.8666666666666667, 3598176556u, 4038097157u},
        {2, 3598176556u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(1)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.count_1") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 2614516830u, 1373286888u},
        {0.30833333333333335, 3740754234u, 517497751u},
        {0.575, 1852131924u, 2850371059u},
        {0.5833333333333334, 353847849u, 1963581074u},
        {0.5916666666666667, 966121072u, 1990055902u},
        {0.6166666666666667, 3786911261u, 4185790632u},
        {0.625, 3130481794u, 3364582826u},
        {1, 2837931869u, 3323044919u},
        {1.1916666666666667, 2452926295u, 1113057907u},
        {1.2, 2364231464u, 282577005u},
        {1.2083333333333333, 769435469u, 3606350981u},
        {1.8583333333333334, 1114315171u, 4038097157u},
        {1.8666666666666667, 1114315171u, 4038097157u},
        {2, 1114315171u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(1.5)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.count_2") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 3192429286u, 4249197569u},
        {0.30833333333333335, 678217856u, 3489002005u},
        {0.575, 2969320574u, 3884754962u},
        {0.5833333333333334, 2014916827u, 2732926249u},
        {0.5916666666666667, 3592384308u, 783634150u},
        {0.6166666666666667, 1614508258u, 2806830623u},
        {0.625, 2160772513u, 1029558907u},
        {1, 1976575034u, 1171546u},
        {1.1916666666666667, 906708923u, 3404793012u},
        {1.2, 2372820400u, 891852205u},
        {1.2083333333333333, 3455045539u, 317974408u},
        {1.8583333333333334, 3656939952u, 4038097157u},
        {1.8666666666666667, 3656939952u, 4038097157u},
        {2, 3656939952u, 4038097157u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(6.5)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.count_3") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1546647399u, 4077835337u},
        {0.10833333333333334, 947457949u, 1953791927u},
        {0.2, 2601052924u, 528618451u},
        {0.25833333333333336, 1142486464u, 4175946440u},
        {0.2916666666666667, 2368680320u, 296022315u},
        {0.3, 448024493u, 3457436734u},
        {0.30833333333333335, 280003011u, 3937775624u},
        {0.575, 3044362127u, 1583566419u},
        {0.5833333333333334, 2916463998u, 1282791745u},
        {0.5916666666666667, 2834627606u, 4022305855u},
        {0.6166666666666667, 4167308709u, 119261030u},
        {0.625, 1711699933u, 790194813u},
        {1, 3185662534u, 3518750069u},
        {1.1916666666666667, 805648343u, 4046263818u},
        {1.2, 4137808574u, 1021266412u},
        {1.2083333333333333, 1259889904u, 612656220u},
        {1.8583333333333334, 3274282268u, 2186329149u},
        {1.8666666666666667, 3274282268u, 2500469795u},
        {2, 2712590671u, 4010308565u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.from_0") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1711183660u, 4077835337u},
        {0.10833333333333334, 3915339165u, 1953791927u},
        {0.2, 2865745955u, 528618451u},
        {0.25833333333333336, 204490767u, 4175946440u},
        {0.2916666666666667, 1664778895u, 296022315u},
        {0.3, 1822370098u, 3457436734u},
        {0.30833333333333335, 2207955896u, 3937775624u},
        {0.575, 3273223152u, 1583566419u},
        {0.5833333333333334, 2578053061u, 1282791745u},
        {0.5916666666666667, 1569546353u, 4022305855u},
        {0.6166666666666667, 830029814u, 119261030u},
        {0.625, 1598148598u, 790194813u},
        {1, 100200922u, 3686224591u},
        {1.1916666666666667, 2044332542u, 4008125098u},
        {1.2, 3989270495u, 1540567576u},
        {1.2083333333333333, 1004562566u, 3353293056u},
        {1.8583333333333334, 1651238029u, 2186329149u},
        {1.8666666666666667, 1651238029u, 2500469795u},
        {2, 1788941862u, 4010308565u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(-999999)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.from_1") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 1417757032u, 4077835337u},
        {0.10833333333333334, 4151412098u, 1953791927u},
        {0.2, 1342037715u, 528618451u},
        {0.25833333333333336, 1661226639u, 4175946440u},
        {0.2916666666666667, 777228047u, 296022315u},
        {0.3, 2575044018u, 3457436734u},
        {0.30833333333333335, 1065344568u, 3937775624u},
        {0.575, 3193925928u, 1583566419u},
        {0.5833333333333334, 552434269u, 1282791745u},
        {0.5916666666666667, 3948685521u, 4022305855u},
        {0.6166666666666667, 1506206714u, 119261030u},
        {0.625, 10732206u, 790194813u},
        {1, 555656926u, 138620183u},
        {1.1916666666666667, 983302353u, 2923632574u},
        {1.2, 1361985841u, 3557292396u},
        {1.2083333333333333, 2103120891u, 1896688660u},
        {1.8583333333333334, 2733123434u, 2186329149u},
        {1.8666666666666667, 2733123434u, 2500469795u},
        {2, 463341301u, 4010308565u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(-0.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.from_2") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 686808629u, 4077835337u},
        {0.10833333333333334, 2122515727u, 1953791927u},
        {0.2, 966445850u, 528618451u},
        {0.25833333333333336, 3717970702u, 4175946440u},
        {0.2916666666666667, 1836425094u, 296022315u},
        {0.3, 257122347u, 3457436734u},
        {0.30833333333333335, 1356808293u, 3937775624u},
        {0.575, 3073053269u, 1583566419u},
        {0.5833333333333334, 1970672856u, 1282791745u},
        {0.5916666666666667, 1410362696u, 4022305855u},
        {0.6166666666666667, 1692972447u, 119261030u},
        {0.625, 3095487599u, 790194813u},
        {1, 2294124754u, 138620183u},
        {1.1916666666666667, 3444377960u, 2923632574u},
        {1.2, 3614426052u, 3557292396u},
        {1.2083333333333333, 2032349722u, 1896688660u},
        {1.8583333333333334, 3666341175u, 2186329149u},
        {1.8666666666666667, 3666341175u, 2500469795u},
        {2, 3598176556u, 4010308565u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0.5)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.coincollect.from_3") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    constexpr Frame frames[] = {
        {0, 2865745249u, 900853325u},
        {0.008333333333333333, 2241429363u, 1762221640u},
        {0.09166666666666666, 3883770913u, 859130717u},
        {0.1, 442655799u, 4077835337u},
        {0.10833333333333334, 789643650u, 1953791927u},
        {0.2, 2786494632u, 528618451u},
        {0.25833333333333336, 377541404u, 4175946440u},
        {0.2916666666666667, 1274023148u, 296022315u},
        {0.3, 2462139865u, 3457436734u},
        {0.30833333333333335, 3191697919u, 3937775624u},
        {0.575, 2400130799u, 1583566419u},
        {0.5833333333333334, 1571813030u, 1282791745u},
        {0.5916666666666667, 1987416142u, 4022305855u},
        {0.6166666666666667, 2931437681u, 119261030u},
        {0.625, 741899813u, 790194813u},
        {1, 2868960583u, 1425036747u},
        {1.1916666666666667, 3408255257u, 1131195246u},
        {1.2, 2373824475u, 1777322360u},
        {1.2083333333333333, 3669988942u, 390795800u},
        {1.8583333333333334, 3973590521u, 2186329149u},
        {1.8666666666666667, 3973590521u, 2500469795u},
        {2, 2539048818u, 4010308565u},
        {2.3916666666666666, 3446172101u, 3446172101u},
        {2.4, 3446172101u, 3446172101u},
        {2.691666666666667, 3446172101u, 3446172101u},
        {2.7, 3446172101u, 3446172101u},
        {2.7583333333333333, 3446172101u, 3446172101u},
        {2.908333333333333, 3446172101u, 3446172101u},
        {3, 3446172101u, 3446172101u},
        {3.341666666666667, 3446172101u, 3446172101u},
        {3.4, 3446172101u, 3446172101u},
        {5, 3446172101u, 3446172101u},
    };
    PixelFx fx(PixelFxKind::CoinCollect);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(999999)json");
        REQUIRE(input);
        value.find("options")->set("from", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(12)json");
        REQUIRE(input);
        value.find("options")->set("count", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::CoinCollect, value);
    REQUIRE(prepared);
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
TEST_CASE("stylize.pixelfx.ui.counters.reverseSeekLayersPurityAndStopRestore") {
    for (auto kind : {PixelFxKind::Countdown, PixelFxKind::CoinCollect, PixelFxKind::Confetti}) {
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
TEST_CASE("stylize.pixelfx.ui.counters.partitionedClocksPreserveEmissionOrder") {
    for (auto kind : {PixelFxKind::Countdown, PixelFxKind::CoinCollect, PixelFxKind::Confetti})
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
TEST_CASE("stylize.pixelfx.ui.counters.materialOwnershipRoundTripBoundsAndAtomicFailure") {
    for (auto kind : {PixelFxKind::Countdown, PixelFxKind::CoinCollect, PixelFxKind::Confetti}) {
        PixelFx fx(kind);
        auto    s      = fx.settings();
        s.palette      = {0xff123456u};
        auto candidate = PixelFx::prepareConfiguration(kind, s);
        REQUIRE(candidate);
        s.palette.clear();
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().palette.size() == (kind == PixelFxKind::Confetti ? 7 : 8));
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
TEST_CASE("stylize.pixelfx.ui.counters.ownedGoWordAndNumericBounds") {
    PixelFx fx(PixelFxKind::Countdown);
    auto    settings = fx.settings();
    settings.ui.go   = {0xff123456u};
    settings.ui.word = "START ıſ🙂";
    auto candidate   = PixelFx::prepareConfiguration(PixelFxKind::Countdown, settings);
    REQUIRE(candidate);
    settings.ui.go.clear();
    settings.ui.word.clear();
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    REQUIRE(fx.settings().ui.word == "START ıſ🙂");
    REQUIRE(fx.settings().ui.go.size() == 8);
    for (auto c : fx.settings().ui.go) REQUIRE(c == 0xff123456u);
    const auto before = fx.configurationValue();
    for (size_t size : {size_t(0), size_t(65)}) {
        auto bad = fx.settings();
        bad.ui.go.assign(size, 0xff123456u);
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Countdown, bad));
    }
    {
        auto bad  = fx.settings();
        bad.ui.go = {0x80123456u};
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Countdown, bad));
    }
    for (const auto& text : std::vector<std::string>{std::string(65, 'M'), std::string("\xc0\xaf", 2)}) {
        auto bad    = fx.settings();
        bad.ui.word = text;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Countdown, bad));
    }
    for (double value :
         {0., 3.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad             = fx.settings();
        bad.ui.countdownFrom = value;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Countdown, bad));
    }
    for (const char* key : {"word", "go", "from"}) {
        auto value = before;
        value.find("options")->set(key, eve::Value{});
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::Countdown, value));
        REQUIRE(fx.configurationValue() == before);
    }
    PixelFx    coins(PixelFxKind::CoinCollect);
    const auto initial = coins.configurationValue();
    for (double value :
         {0., 12.001, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad     = coins.settings();
        bad.ui.count = value;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinCollect, bad));
    }
    for (double value :
         {-1000000., 1000000., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        auto bad        = coins.settings();
        bad.ui.coinFrom = value;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CoinCollect, bad));
    }
    REQUIRE(coins.configurationValue() == initial);
}

}  // namespace test_pixelfx_ui_counters_unit
