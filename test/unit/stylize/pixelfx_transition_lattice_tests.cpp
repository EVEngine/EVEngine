// Pinned-source analytic lattice and torn-band transition contracts.
#include <algorithm>
#include <array>
#include <limits>
#include "stylize/PixelFx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_transition_lattice_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> data) {
    uint32_t h = 2166136261u;
    for (auto c : data)
        for (int s = 0; s < 32; s += 8) h = (h ^ ((c >> s) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.denseSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 518446832u, 224172012u},
        {0.075, 4208344236u, 542781104u},
        {0.125, 452546413u, 2751754846u},
        {0.15, 2121513181u, 3668906862u},
        {0.2, 3701586795u, 985920092u},
        {0.2833333333333333, 1624485388u, 1084062579u},
        {0.3333333333333333, 4120571027u, 379210350u},
        {0.4166666666666667, 126786165u, 4161831163u},
        {0.5, 3817915851u, 813280972u},
        {0.6166666666666667, 3557744228u, 179333889u},
        {0.65, 3899688616u, 1679925387u},
        {0.6666666666666666, 178091136u, 2517702900u},
        {0.75, 4031262055u, 2215809783u},
        {0.775, 1655462775u, 4165568549u},
        {0.7833333333333333, 2978826371u, 3701571972u},
        {0.8, 88806917u, 966016466u},
        {0.8333333333333334, 792036725u, 1472656507u},
        {0.9, 2323650043u, 2690197609u},
        {0.975, 34419501u, 3996234637u},
        {0.9833333333333333, 4101090607u, 1001291329u},
        {1, 3573658611u, 3733215609u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1815136084u, 4222399661u},
        {1.2, 3206586537u, 3079292341u},
        {1.3, 1072259473u, 2936162225u},
        {1.4, 2001745588u, 135420880u},
        {1.5, 1354982547u, 1266084204u},
        {1.6, 4173024792u, 3311438466u},
        {1.7, 2527695968u, 4069830716u},
        {1.8, 2195846277u, 3547146063u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.denseSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 177367363u, 901326213u},
        {0.075, 177367363u, 901326213u},
        {0.125, 80091517u, 1428795820u},
        {0.15, 80091517u, 1428795820u},
        {0.2, 897762817u, 3087158595u},
        {0.2833333333333333, 2811256054u, 221596209u},
        {0.3333333333333333, 2092735600u, 2749869645u},
        {0.4166666666666667, 1515112231u, 366094980u},
        {0.5, 2567816616u, 1113124703u},
        {0.6166666666666667, 3081791806u, 3832149545u},
        {0.65, 1309830898u, 1328887444u},
        {0.6666666666666666, 965835510u, 4054523156u},
        {0.75, 2170800036u, 3918227370u},
        {0.775, 2226729318u, 1509191612u},
        {0.7833333333333333, 2084714110u, 2236050885u},
        {0.8, 2084714110u, 2236050885u},
        {0.8333333333333334, 2829491861u, 2236050885u},
        {0.9, 2256288910u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 1532814700u, 3849632648u},
        {1.3, 2065036498u, 2224052938u},
        {1.4, 845144919u, 4066913797u},
        {1.5, 4077737986u, 3205131565u},
        {1.6, 3817894957u, 2400393679u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    REQUIRE(!fx.isSimulated());
    for (const auto& f : frames) {
        auto seek = fx.prepareSeek(f.time);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(checksum(fx.color()) == f.color);
        REQUIRE(checksum(fx.light()) == f.light);
    }
}
TEST_CASE("stylize.pixelfx.transitionLattice.reverseLayersPurityClocksAndStop") {
    for (auto kind : {PixelFxKind::DiamondWipe, PixelFxKind::GlitchWipe}) {
        PixelFx  fx(kind);
        uint32_t initialColor = 0, initialLight = 0;
        for (double t : {.3, 5., .05, .15, .5, .9, 1., 1.04, 1.1, 1.3, 1.5, 1.7, .3, 0., .3}) {
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
            if (t == .3) {
                if (initialColor) {
                    REQUIRE(checksum(c) == initialColor);
                    REQUIRE(checksum(l) == initialLight);
                } else {
                    initialColor = checksum(c);
                    initialLight = checksum(l);
                }
            }
        }
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        REQUIRE(std::all_of(fx.color().begin(), fx.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(fx.light().begin(), fx.light().end(), [](auto c) { return c == 0; }));
        auto seek = fx.prepareSeek(.3);
        REQUIRE(seek);
        REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        REQUIRE(checksum(fx.color()) == initialColor);
        REQUIRE(checksum(fx.light()) == initialLight);
        for (double step : {1. / 30, 1. / 60, 1. / 144, .007}) {
            PixelFx live(kind), reference(kind);
            double  elapsed = 0;
            for (double target : {.01, .034, .05, .075, .12, .13, .14, .5, .9, 1.5, 2.5}) {
                while (elapsed < target) {
                    const double dt = std::min(step, target - elapsed);
                    REQUIRE(live.advance(dt));
                    elapsed += dt;
                }
                auto position = reference.prepareSeek(live.effectTime());
                REQUIRE(position);
                REQUIRE(reference.commitSeek(std::move(position).takeValue()));
                reference.draw();
                live.draw();
                REQUIRE(std::equal(live.color().begin(), live.color().end(), reference.color().begin()));
                REQUIRE(std::equal(live.light().begin(), live.light().end(), reference.light().begin()));
                REQUIRE(live.done() == (live.effectTime() >= live.info().duration));
            }
        }
    }
}
TEST_CASE("stylize.pixelfx.transitionLattice.preparedGeometryOwnershipAndCoverage") {
    for (auto kind : {PixelFxKind::DiamondWipe, PixelFxKind::GlitchWipe}) {
        PixelFx first(kind);
        first.render(.3);
        const auto color = std::vector<uint32_t>(first.color().begin(), first.color().end()),
                   light = std::vector<uint32_t>(first.light().begin(), first.light().end());
        PixelFx    other(kind);
        auto       settings = other.settings();
        settings.seed       = 123;
        auto prepared       = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(prepared);
        settings.seed = 1;
        REQUIRE(other.commitConfiguration(std::move(prepared).takeValue()));
        other.render(.3);
        REQUIRE(!std::equal(color.begin(), color.end(), other.color().begin()));
        first.render(.3);
        REQUIRE(std::equal(color.begin(), color.end(), first.color().begin()));
        REQUIRE(std::equal(light.begin(), light.end(), first.light().begin()));
        first.render(0);
        REQUIRE(std::all_of(first.color().begin(), first.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(first.light().begin(), first.light().end(), [](auto c) { return c == 0; }));
        first.render(.95);
        REQUIRE(std::all_of(first.color().begin(), first.color().end(), [](auto c) { return (c >> 24) == 255; }));
        first.render(first.info().duration);
        REQUIRE(std::all_of(first.color().begin(), first.color().end(), [](auto c) { return c == 0; }));
        REQUIRE(std::all_of(first.light().begin(), first.light().end(), [](auto c) { return c == 0; }));
    }
}
TEST_CASE("stylize.pixelfx.transitionLattice.splitRawColorsRoundTripAndAtomicRejection") {
    using eve::Value;
    PixelFx fx(PixelFxKind::GlitchWipe);
    for (uint32_t color : {0u, 0x00010203u, 0x80112233u, 0xffffffffu}) {
        auto settings             = fx.settings();
        settings.transition.split = color;
        auto candidate            = PixelFx::prepareConfiguration(PixelFxKind::GlitchWipe, settings);
        REQUIRE(candidate);
        settings.transition.split = 0;
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().transition.split == color);
        const auto config = fx.configurationValue();
        auto       rt     = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, config);
        REQUIRE(rt);
        PixelFx other(PixelFxKind::GlitchWipe);
        REQUIRE(other.commitConfiguration(std::move(rt).takeValue()));
        REQUIRE(other.configurationValue() == config);
    }
    fx.render(.3);
    const auto before = fx.configurationValue();
    const auto c = checksum(fx.color()), l = checksum(fx.light());
    for (const char* json : {"null", "true", "[]", "\"#123456\"", "-1", "4294967296", "1.5"}) {
        auto input = Value::fromJson(json);
        REQUIRE(input);
        auto config = before;
        config.find("options")->set("split", input.value());
        REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, config));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(checksum(fx.color()) == c);
        REQUIRE(checksum(fx.light()) == l);
        REQUIRE(fx.effectTime() == .3);
    }
    PixelFx diamond(PixelFxKind::DiamondWipe);
    auto    unknown = diamond.configurationValue();
    unknown.find("options")->set("split", Value(int64_t(0xffffffffu)));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, unknown));
    auto metadata = PixelFx::describeParameters(PixelFxKind::GlitchWipe);
    REQUIRE(metadata);
    bool found = false;
    for (const auto& p : *metadata.value().find("parameters")->getIf<Value::Array>())
        if (p.find("name")->asString() == "split") {
            found = true;
            REQUIRE(p.find("type")->asString() == "color");
            REQUIRE(p.find("minimum")->asInt() == 0);
            REQUIRE(p.find("maximum")->asInt() == 4294967295LL);
        }
    REQUIRE(found);
}
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.varSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 1716029551u},
        {0.06666666666666667, 1346058661u, 929400070u},
        {0.075, 2272098212u, 2475722313u},
        {0.125, 296591271u, 3390132171u},
        {0.15, 3889676577u, 4169851257u},
        {0.2, 572667322u, 3665235776u},
        {0.2833333333333333, 1395565161u, 2140341408u},
        {0.3333333333333333, 254482341u, 2201243113u},
        {0.4166666666666667, 278841196u, 1456341162u},
        {0.5, 1971116469u, 3578543015u},
        {0.6166666666666667, 646906974u, 2926201355u},
        {0.65, 996345751u, 11448585u},
        {0.6666666666666666, 1993184608u, 1426437628u},
        {0.75, 154992617u, 3116109301u},
        {0.775, 2925243728u, 2349752743u},
        {0.7833333333333333, 3174735412u, 277398811u},
        {0.8, 2852451968u, 2805457551u},
        {0.8333333333333334, 2980502830u, 4070858365u},
        {0.9, 2230747288u, 685256880u},
        {0.975, 2381694149u, 2375289465u},
        {0.9833333333333333, 3676130245u, 3548636773u},
        {1, 1677592005u, 2236050885u},
        {1.0416666666666667, 1677592005u, 2236050885u},
        {1.05, 1677592005u, 2236050885u},
        {1.0666666666666667, 1677592005u, 1336204075u},
        {1.1, 1882262789u, 534300844u},
        {1.2, 211782708u, 3185114647u},
        {1.3, 4101181990u, 683990970u},
        {1.4, 1348437394u, 886404490u},
        {1.5, 3513233296u, 2633356094u},
        {1.6, 415967350u, 1846658355u},
        {1.7, 246695600u, 718540642u},
        {1.8, 1746426861u, 3350766292u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
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
        auto input = eve::Value::fromJson(R"json(4293980400)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.zeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 3716844278u},
        {0.06666666666666667, 1806051317u, 1323944507u},
        {0.075, 80632926u, 4226273668u},
        {0.125, 3078798041u, 3034783051u},
        {0.15, 2308218074u, 3571680005u},
        {0.2, 3365802365u, 2326024190u},
        {0.2833333333333333, 3690633902u, 1509064580u},
        {0.3333333333333333, 1668822874u, 2553035318u},
        {0.4166666666666667, 534836806u, 2622211017u},
        {0.5, 2760592717u, 1910226892u},
        {0.6166666666666667, 4006323155u, 2537908017u},
        {0.65, 1525515911u, 2566445753u},
        {0.6666666666666666, 3291560591u, 402037463u},
        {0.75, 49485551u, 2244728611u},
        {0.775, 4234349565u, 3304221381u},
        {0.7833333333333333, 1504819809u, 4038105279u},
        {0.8, 3507450576u, 3217571629u},
        {0.8333333333333334, 2503407318u, 1435950128u},
        {0.9, 1319664677u, 4256241926u},
        {0.975, 2393848763u, 1144838482u},
        {0.9833333333333333, 1484348765u, 3344985152u},
        {1, 2332576191u, 1386855005u},
        {1.0416666666666667, 4051789829u, 2340578725u},
        {1.05, 2924933915u, 2761320609u},
        {1.0666666666666667, 1491770821u, 2754288710u},
        {1.1, 1703759170u, 1792745346u},
        {1.2, 2175385350u, 4130119279u},
        {1.3, 1550827603u, 2763825570u},
        {1.4, 515946920u, 2435939364u},
        {1.5, 3268506936u, 3164139311u},
        {1.6, 2799787508u, 1750248214u},
        {1.7, 839315352u, 2983039904u},
        {1.8, 3837321499u, 511947924u},
        {1.9, 64586407u, 1027921945u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.maxSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 1597254207u, 1663370707u},
        {0.075, 985907291u, 179292760u},
        {0.125, 4039809431u, 434628550u},
        {0.15, 2671897295u, 768013201u},
        {0.2, 3019963787u, 2075102551u},
        {0.2833333333333333, 3602064156u, 1707420360u},
        {0.3333333333333333, 2609637051u, 4230538885u},
        {0.4166666666666667, 83841131u, 1708712317u},
        {0.5, 2906435971u, 4285433311u},
        {0.6166666666666667, 2832642308u, 3116663212u},
        {0.65, 3428060153u, 550641689u},
        {0.6666666666666666, 507781966u, 4277894671u},
        {0.75, 116378392u, 2962758443u},
        {0.775, 3480417711u, 1751697698u},
        {0.7833333333333333, 608484636u, 295660920u},
        {0.8, 361380219u, 1876029233u},
        {0.8333333333333334, 2009479244u, 725763183u},
        {0.9, 3735167115u, 1500225848u},
        {0.975, 2389427485u, 2566374120u},
        {0.9833333333333333, 1440129859u, 2648445146u},
        {1, 2892547857u, 4291462234u},
        {1.0416666666666667, 2526700393u, 702394453u},
        {1.05, 2777634617u, 2279150487u},
        {1.0666666666666667, 4075588325u, 2158066285u},
        {1.1, 4015029652u, 984061629u},
        {1.2, 444258380u, 1783918875u},
        {1.3, 131413164u, 1777261271u},
        {1.4, 3283634252u, 4016479623u},
        {1.5, 2971627617u, 1921950745u},
        {1.6, 1946441702u, 468912833u},
        {1.7, 948556754u, 137177311u},
        {1.8, 839801617u, 1744636462u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.singleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 203263121u, 770384374u},
        {0.075, 352734989u, 1502187734u},
        {0.125, 3044195533u, 857404978u},
        {0.15, 15281521u, 2798377416u},
        {0.2, 3720756193u, 304901698u},
        {0.2833333333333333, 2382923117u, 654612500u},
        {0.3333333333333333, 1925609861u, 1218108436u},
        {0.4166666666666667, 860926561u, 216683533u},
        {0.5, 1331799433u, 1172933887u},
        {0.6166666666666667, 84446461u, 3862433192u},
        {0.65, 2286540669u, 2244280672u},
        {0.6666666666666666, 3653499697u, 371879023u},
        {0.75, 979950305u, 4044878398u},
        {0.775, 2623994909u, 3430578943u},
        {0.7833333333333333, 3048980425u, 1652564469u},
        {0.8, 890650385u, 2340674259u},
        {0.8333333333333334, 1941166101u, 2216913743u},
        {0.9, 3205218921u, 382584803u},
        {0.975, 3755815757u, 1687746349u},
        {0.9833333333333333, 3433664297u, 3096429253u},
        {1, 1205749225u, 3613784813u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 3799690333u, 2807062606u},
        {1.2, 3787180637u, 2800469721u},
        {1.3, 3953482389u, 817233248u},
        {1.4, 559086897u, 1017258547u},
        {1.5, 1842653665u, 1352827291u},
        {1.6, 2137402737u, 2331011663u},
        {1.7, 3026164889u, 3833296933u},
        {1.8, 299893129u, 2722202395u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.manySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 3744739688u, 2779017686u},
        {0.075, 1817791369u, 1176525085u},
        {0.125, 4191126315u, 1119266388u},
        {0.15, 1541004585u, 3026160516u},
        {0.2, 4144753546u, 700213509u},
        {0.2833333333333333, 3180089680u, 2744462277u},
        {0.3333333333333333, 2891037074u, 2195588397u},
        {0.4166666666666667, 510474233u, 3161488257u},
        {0.5, 2600874051u, 3269247210u},
        {0.6166666666666667, 2409946253u, 3340592557u},
        {0.65, 3920206092u, 1882497732u},
        {0.6666666666666666, 4016240932u, 515280191u},
        {0.75, 1242186177u, 2223369585u},
        {0.775, 1786135177u, 1746956442u},
        {0.7833333333333333, 1931549337u, 2955503594u},
        {0.8, 1594643387u, 1533184060u},
        {0.8333333333333334, 1629043241u, 940808783u},
        {0.9, 733835602u, 1758200954u},
        {0.975, 3928094397u, 2643902689u},
        {0.9833333333333333, 161031194u, 2862447285u},
        {1, 1361077319u, 2957219213u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 856839099u, 3220018225u},
        {1.2, 3597279247u, 1385924620u},
        {1.3, 4078488568u, 2495204020u},
        {1.4, 1609941475u, 705485905u},
        {1.5, 194017286u, 1914375275u},
        {1.6, 957606790u, 1592112902u},
        {1.7, 2362057624u, 3678472576u},
        {1.8, 2044451669u, 2414765495u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.colorzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 518446832u, 224172012u},
        {0.075, 4208344236u, 542781104u},
        {0.125, 452546413u, 2751754846u},
        {0.15, 2121513181u, 3668906862u},
        {0.2, 3701586795u, 985920092u},
        {0.2833333333333333, 4034447416u, 1084062579u},
        {0.3333333333333333, 616737991u, 379210350u},
        {0.4166666666666667, 1119390392u, 4161831163u},
        {0.5, 1306436713u, 813280972u},
        {0.6166666666666667, 675262031u, 179333889u},
        {0.65, 1938587689u, 1679925387u},
        {0.6666666666666666, 1373535767u, 2517702900u},
        {0.75, 1279791092u, 2215809783u},
        {0.775, 359307278u, 4165568549u},
        {0.7833333333333333, 161448596u, 3701571972u},
        {0.8, 637172325u, 966016466u},
        {0.8333333333333334, 2735204170u, 1472656507u},
        {0.9, 2798325204u, 2690197609u},
        {0.975, 2797688573u, 3996234637u},
        {0.9833333333333333, 75257311u, 1001291329u},
        {1, 2830080935u, 3733215609u},
        {1.0416666666666667, 2236050885u, 2236050885u},
        {1.05, 2236050885u, 2236050885u},
        {1.0666666666666667, 2236050885u, 2236050885u},
        {1.1, 601097077u, 4222399661u},
        {1.2, 2030835112u, 3079292341u},
        {1.3, 2690516887u, 2936162225u},
        {1.4, 3498589698u, 135420880u},
        {1.5, 4162130791u, 1266084204u},
        {1.6, 4173024792u, 3311438466u},
        {1.7, 2527695968u, 4069830716u},
        {1.8, 2195846277u, 3547146063u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.colortransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 518446832u, 224172012u},
        {0.075, 4208344236u, 542781104u},
        {0.125, 452546413u, 2751754846u},
        {0.15, 2121513181u, 3668906862u},
        {0.2, 3701586795u, 985920092u},
        {0.2833333333333333, 2452469716u, 1084062579u},
        {0.3333333333333333, 3685069039u, 379210350u},
        {0.4166666666666667, 1038454477u, 4161831163u},
        {0.5, 1474711303u, 813280972u},
        {0.6166666666666667, 1641250376u, 179333889u},
        {0.65, 260793736u, 1679925387u},
        {0.6666666666666666, 1129993880u, 2517702900u},
        {0.75, 2713926115u, 2215809783u},
        {0.775, 3533821551u, 4165568549u},
        {0.7833333333333333, 2494756127u, 3701571972u},
        {0.8, 2178534493u, 966016466u},
        {0.8333333333333334, 3671554109u, 1472656507u},
        {0.9, 70669639u, 2690197609u},
        {0.975, 3292161117u, 3996234637u},
        {0.9833333333333333, 2628129283u, 1001291329u},
        {1, 2456470675u, 3733215609u},
        {1.0416666666666667, 1021096389u, 2236050885u},
        {1.05, 1021096389u, 2236050885u},
        {1.0666666666666667, 1021096389u, 2236050885u},
        {1.1, 791969052u, 4222399661u},
        {1.2, 2410486097u, 3079292341u},
        {1.3, 3856260757u, 2936162225u},
        {1.4, 844673408u, 135420880u},
        {1.5, 3954101143u, 1266084204u},
        {1.6, 4173024792u, 3311438466u},
        {1.7, 2527695968u, 4069830716u},
        {1.8, 2195846277u, 3547146063u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.colorpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 518446832u, 224172012u},
        {0.075, 4208344236u, 542781104u},
        {0.125, 452546413u, 2751754846u},
        {0.15, 2121513181u, 3668906862u},
        {0.2, 3701586795u, 985920092u},
        {0.2833333333333333, 2015310484u, 1084062579u},
        {0.3333333333333333, 3425400591u, 379210350u},
        {0.4166666666666667, 1757557517u, 4161831163u},
        {0.5, 702703751u, 813280972u},
        {0.6166666666666667, 720055848u, 179333889u},
        {0.65, 2352508488u, 1679925387u},
        {0.6666666666666666, 3010913208u, 2517702900u},
        {0.75, 1211310083u, 2215809783u},
        {0.775, 2156087023u, 4165568549u},
        {0.7833333333333333, 1206963455u, 3701571972u},
        {0.8, 3802322461u, 966016466u},
        {0.8333333333333334, 2106558749u, 1472656507u},
        {0.9, 1485144647u, 2690197609u},
        {0.975, 236772541u, 3996234637u},
        {0.9833333333333333, 867093667u, 1001291329u},
        {1, 2310402739u, 3733215609u},
        {1.0416666666666667, 1441378757u, 2236050885u},
        {1.05, 1441378757u, 2236050885u},
        {1.0666666666666667, 1441378757u, 2236050885u},
        {1.1, 880040252u, 4222399661u},
        {1.2, 118036273u, 3079292341u},
        {1.3, 2823182549u, 2936162225u},
        {1.4, 286134848u, 135420880u},
        {1.5, 4080071127u, 1266084204u},
        {1.6, 4173024792u, 3311438466u},
        {1.7, 2527695968u, 4069830716u},
        {1.8, 2195846277u, 3547146063u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.diamondwipe.colorwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 518446832u, 224172012u},
        {0.075, 4208344236u, 542781104u},
        {0.125, 452546413u, 2751754846u},
        {0.15, 2121513181u, 3668906862u},
        {0.2, 3701586795u, 985920092u},
        {0.2833333333333333, 778232708u, 1084062579u},
        {0.3333333333333333, 537144075u, 379210350u},
        {0.4166666666666667, 2527618469u, 4161831163u},
        {0.5, 409828759u, 813280972u},
        {0.6166666666666667, 383178168u, 179333889u},
        {0.65, 3711467660u, 1679925387u},
        {0.6666666666666666, 1653860916u, 2517702900u},
        {0.75, 3625501547u, 2215809783u},
        {0.775, 1579556011u, 4165568549u},
        {0.7833333333333333, 1805816283u, 3701571972u},
        {0.8, 1863613601u, 966016466u},
        {0.8333333333333334, 1756009405u, 1472656507u},
        {0.9, 3034872299u, 2690197609u},
        {0.975, 1631271229u, 3996234637u},
        {0.9833333333333333, 827197035u, 1001291329u},
        {1, 2456811859u, 3733215609u},
        {1.0416666666666667, 2123157957u, 2236050885u},
        {1.05, 2123157957u, 2236050885u},
        {1.0666666666666667, 2123157957u, 2236050885u},
        {1.1, 4197561120u, 4222399661u},
        {1.2, 3528004765u, 3079292341u},
        {1.3, 1632878325u, 2936162225u},
        {1.4, 42069776u, 135420880u},
        {1.5, 226580831u, 1266084204u},
        {1.6, 4173024792u, 3311438466u},
        {1.7, 2527695968u, 4069830716u},
        {1.8, 2195846277u, 3547146063u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::DiamondWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::DiamondWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.varSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 2236050885u, 2236050885u},
        {0.075, 2236050885u, 2236050885u},
        {0.125, 2236050885u, 2236050885u},
        {0.15, 2236050885u, 2236050885u},
        {0.2, 2023246330u, 395893593u},
        {0.2833333333333333, 315281829u, 3544969018u},
        {0.3333333333333333, 2948455775u, 1324573530u},
        {0.4166666666666667, 4082122885u, 1415305144u},
        {0.5, 2940800828u, 2293949710u},
        {0.6166666666666667, 1155200754u, 2109380179u},
        {0.65, 1561266809u, 3077786567u},
        {0.6666666666666666, 2018956608u, 416882624u},
        {0.75, 1677592005u, 2236050885u},
        {0.775, 1677592005u, 2236050885u},
        {0.7833333333333333, 1326370932u, 2236050885u},
        {0.8, 1326370932u, 2236050885u},
        {0.8333333333333334, 2899454517u, 2236050885u},
        {0.9, 3674792676u, 2236050885u},
        {0.975, 1677592005u, 2236050885u},
        {0.9833333333333333, 1677592005u, 2236050885u},
        {1, 2907253123u, 2236050885u},
        {1.0416666666666667, 1677592005u, 2236050885u},
        {1.05, 2462197370u, 3128087306u},
        {1.0666666666666667, 3012281919u, 199985967u},
        {1.1, 2616603840u, 884005496u},
        {1.2, 4237131971u, 2065738260u},
        {1.3, 2819032766u, 1848229194u},
        {1.4, 2115342764u, 1705271808u},
        {1.5, 3007271682u, 3099166371u},
        {1.6, 4004939729u, 4135393830u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
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
        auto input = eve::Value::fromJson(R"json(4293980400)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.zeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 601281616u, 2931207749u},
        {0.075, 601281616u, 2931207749u},
        {0.125, 2737044476u, 1289851143u},
        {0.15, 3960255495u, 405830748u},
        {0.2, 4014397321u, 262321090u},
        {0.2833333333333333, 1824860062u, 2555864840u},
        {0.3333333333333333, 2923346543u, 531569369u},
        {0.4166666666666667, 1587862292u, 195977256u},
        {0.5, 2388693587u, 1748879047u},
        {0.6166666666666667, 1065317731u, 88097695u},
        {0.65, 1378454531u, 793573032u},
        {0.6666666666666666, 2140449537u, 2503889848u},
        {0.75, 1318258241u, 2163059429u},
        {0.775, 3205898487u, 3718851314u},
        {0.7833333333333333, 2084714110u, 2236050885u},
        {0.8, 2084714110u, 2236050885u},
        {0.8333333333333334, 2829491861u, 2236050885u},
        {0.9, 2256288910u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 4215478495u, 365253329u},
        {1.1, 1424835364u, 1838815650u},
        {1.2, 10880482u, 3146627512u},
        {1.3, 318690055u, 3995759641u},
        {1.4, 4079011959u, 4133905217u},
        {1.5, 710630567u, 2459111775u},
        {1.6, 2160892916u, 3750033896u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.maxSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 2236050885u, 2236050885u},
        {0.075, 2236050885u, 2236050885u},
        {0.125, 349005461u, 1738161095u},
        {0.15, 349005461u, 1738161095u},
        {0.2, 1775249458u, 11391968u},
        {0.2833333333333333, 628679445u, 3242648290u},
        {0.3333333333333333, 967072912u, 1071831343u},
        {0.4166666666666667, 741254112u, 2931461424u},
        {0.5, 804018582u, 1832339825u},
        {0.6166666666666667, 2676217324u, 2230020287u},
        {0.65, 2084761819u, 3307370565u},
        {0.6666666666666666, 3326916442u, 565170157u},
        {0.75, 1491770821u, 2236050885u},
        {0.775, 1491770821u, 2236050885u},
        {0.7833333333333333, 2084714110u, 2236050885u},
        {0.8, 2084714110u, 2236050885u},
        {0.8333333333333334, 2829491861u, 2236050885u},
        {0.9, 2256288910u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 2531529260u, 3077131864u},
        {1.2, 2865360976u, 2165631440u},
        {1.3, 2017693155u, 1505778489u},
        {1.4, 482136933u, 2433794770u},
        {1.5, 1602467161u, 1501028647u},
        {1.6, 2236050885u, 2236050885u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.singleSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 377511619u, 1058312117u},
        {0.075, 377511619u, 1058312117u},
        {0.125, 2878089585u, 556457039u},
        {0.15, 2878089585u, 556457039u},
        {0.2, 3174327745u, 843553438u},
        {0.2833333333333333, 2046883875u, 2423868534u},
        {0.3333333333333333, 2045357089u, 1468836873u},
        {0.4166666666666667, 3720567447u, 4147141688u},
        {0.5, 544168341u, 2923817650u},
        {0.6166666666666667, 3481434283u, 2406487738u},
        {0.65, 2394252619u, 525276807u},
        {0.6666666666666666, 2944916355u, 2848717143u},
        {0.75, 2018775469u, 782462254u},
        {0.775, 817575527u, 3907552753u},
        {0.7833333333333333, 3335983027u, 2236050885u},
        {0.8, 3335983027u, 2236050885u},
        {0.8333333333333334, 3124608957u, 2236050885u},
        {0.9, 381585627u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2928361889u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 2228468673u, 3782426152u},
        {1.3, 2551768367u, 1260101816u},
        {1.4, 154135187u, 615796999u},
        {1.5, 2780030631u, 189945703u},
        {1.6, 596823893u, 2301189559u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json([4294967295])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.manySource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 4129129470u, 2289682458u},
        {0.075, 4129129470u, 2289682458u},
        {0.125, 2714860012u, 4236825068u},
        {0.15, 2714860012u, 4236825068u},
        {0.2, 404999695u, 90719139u},
        {0.2833333333333333, 3245734230u, 149485194u},
        {0.3333333333333333, 4133446109u, 970243150u},
        {0.4166666666666667, 2801272599u, 2217903372u},
        {0.5, 2050897642u, 1200030978u},
        {0.6166666666666667, 4109303255u, 3111084731u},
        {0.65, 1739273098u, 2056815906u},
        {0.6666666666666666, 2120638899u, 4044240959u},
        {0.75, 772274273u, 1213019421u},
        {0.775, 1857750361u, 4232196142u},
        {0.7833333333333333, 479619118u, 2236050885u},
        {0.8, 479619118u, 2236050885u},
        {0.8333333333333334, 1200830009u, 2236050885u},
        {0.9, 140574434u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 1571157984u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 3844196771u, 1940495326u},
        {1.3, 215542112u, 2775531662u},
        {1.4, 2615351538u, 2670151093u},
        {1.5, 3459756860u, 1525329668u},
        {1.6, 1007446468u, 3220779248u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(
            R"json([4278190080, 4278214037, 4278237994, 4278261951, 4278285908, 4278309865, 4278333822, 4278357779, 4278381736, 4278405693, 4278429650, 4278453607, 4278477564, 4278501521, 4278525478, 4278549435, 4278573392, 4278597349, 4278621306, 4278645263, 4278669220, 4278693177, 4278717134, 4278741091, 4278765048, 4278789005, 4278812962, 4278836919, 4278860876, 4278884833, 4278908790, 4278932747, 4278956704, 4278980661, 4279004618, 4279028575, 4279052532, 4279076489, 4279100446, 4279124403, 4279148360, 4279172317, 4279196274, 4279220231, 4279244188, 4279268145, 4279292102, 4279316059, 4279340016, 4279363973, 4279387930, 4279411887, 4279435844, 4279459801, 4279483758, 4279507715, 4279531672, 4279555629, 4279579586, 4279603543, 4279627500, 4279651457, 4279675414, 4279699371])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.colorzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 2076903907u, 901326213u},
        {0.075, 2076903907u, 901326213u},
        {0.125, 525093981u, 1428795820u},
        {0.15, 525093981u, 1428795820u},
        {0.2, 2904125529u, 3087158595u},
        {0.2833333333333333, 2398364078u, 221596209u},
        {0.3333333333333333, 2167711732u, 2749869645u},
        {0.4166666666666667, 2297818819u, 366094980u},
        {0.5, 2116246040u, 1113124703u},
        {0.6166666666666667, 1178771702u, 3832149545u},
        {0.65, 3048289786u, 1328887444u},
        {0.6666666666666666, 1261691946u, 4054523156u},
        {0.75, 3366217904u, 3918227370u},
        {0.775, 4175397570u, 1509191612u},
        {0.7833333333333333, 3738711022u, 2236050885u},
        {0.8, 3738711022u, 2236050885u},
        {0.8333333333333334, 3770728117u, 2236050885u},
        {0.9, 2073416094u, 2236050885u},
        {0.975, 2236050885u, 2236050885u},
        {0.9833333333333333, 2236050885u, 2236050885u},
        {1, 1560254624u, 2236050885u},
        {1.0416666666666667, 2236050885u, 2236050885u},
        {1.05, 2236050885u, 2236050885u},
        {1.0666666666666667, 2236050885u, 2236050885u},
        {1.1, 2236050885u, 2236050885u},
        {1.2, 1662313352u, 3849632648u},
        {1.3, 3448973930u, 2224052938u},
        {1.4, 3395781807u, 4066913797u},
        {1.5, 3951611254u, 3205131565u},
        {1.6, 2902269761u, 2400393679u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.colortransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 1681970755u, 901326213u},
        {0.075, 1681970755u, 901326213u},
        {0.125, 3121982829u, 1428795820u},
        {0.15, 3121982829u, 1428795820u},
        {0.2, 1068300781u, 3087158595u},
        {0.2833333333333333, 3703962734u, 221596209u},
        {0.3333333333333333, 3935534820u, 2749869645u},
        {0.4166666666666667, 1220837331u, 366094980u},
        {0.5, 3220877992u, 1113124703u},
        {0.6166666666666667, 152403530u, 3832149545u},
        {0.65, 563717982u, 1328887444u},
        {0.6666666666666666, 3727509934u, 4054523156u},
        {0.75, 1594127164u, 3918227370u},
        {0.775, 2519903230u, 1509191612u},
        {0.7833333333333333, 3130168462u, 2236050885u},
        {0.8, 3130168462u, 2236050885u},
        {0.8333333333333334, 3389925549u, 2236050885u},
        {0.9, 1942270318u, 2236050885u},
        {0.975, 1021096389u, 2236050885u},
        {0.9833333333333333, 1021096389u, 2236050885u},
        {1, 715584180u, 2236050885u},
        {1.0416666666666667, 1021096389u, 2236050885u},
        {1.05, 1021096389u, 2236050885u},
        {1.0666666666666667, 1021096389u, 2236050885u},
        {1.1, 1021096389u, 2236050885u},
        {1.2, 1793842200u, 3849632648u},
        {1.3, 1081422966u, 2224052938u},
        {1.4, 3683188099u, 4066913797u},
        {1.5, 3136490202u, 3205131565u},
        {1.6, 3346889573u, 2400393679u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.colorpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 387192099u, 901326213u},
        {0.075, 387192099u, 901326213u},
        {0.125, 379027565u, 1428795820u},
        {0.15, 379027565u, 1428795820u},
        {0.2, 4240143789u, 3087158595u},
        {0.2833333333333333, 3894931950u, 221596209u},
        {0.3333333333333333, 941489156u, 2749869645u},
        {0.4166666666666667, 1084281459u, 366094980u},
        {0.5, 1598547048u, 1113124703u},
        {0.6166666666666667, 1308884266u, 3832149545u},
        {0.65, 2064843230u, 1328887444u},
        {0.6666666666666666, 3055503918u, 4054523156u},
        {0.75, 2124286268u, 3918227370u},
        {0.775, 854413150u, 1509191612u},
        {0.7833333333333333, 1718527502u, 2236050885u},
        {0.8, 1718527502u, 2236050885u},
        {0.8333333333333334, 2963924749u, 2236050885u},
        {0.9, 1511174414u, 2236050885u},
        {0.975, 1441378757u, 2236050885u},
        {0.9833333333333333, 1441378757u, 2236050885u},
        {1, 2420526996u, 2236050885u},
        {1.0416666666666667, 1441378757u, 2236050885u},
        {1.05, 1441378757u, 2236050885u},
        {1.0666666666666667, 1441378757u, 2236050885u},
        {1.1, 1441378757u, 2236050885u},
        {1.2, 2455412216u, 3849632648u},
        {1.3, 3589376214u, 2224052938u},
        {1.4, 1849260643u, 4066913797u},
        {1.5, 1181143834u, 3205131565u},
        {1.6, 3343679749u, 2400393679u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.colorwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 1458023219u, 901326213u},
        {0.075, 1458023219u, 901326213u},
        {0.125, 1403889513u, 1428795820u},
        {0.15, 1403889513u, 1428795820u},
        {0.2, 1151107621u, 3087158595u},
        {0.2833333333333333, 3367521906u, 221596209u},
        {0.3333333333333333, 2184311800u, 2749869645u},
        {0.4166666666666667, 2659074907u, 366094980u},
        {0.5, 2766185152u, 1113124703u},
        {0.6166666666666667, 3026555142u, 3832149545u},
        {0.65, 3824720502u, 1328887444u},
        {0.6666666666666666, 1312846154u, 4054523156u},
        {0.75, 1461460544u, 3918227370u},
        {0.775, 3076170894u, 1509191612u},
        {0.7833333333333333, 88775222u, 2236050885u},
        {0.8, 88775222u, 2236050885u},
        {0.8333333333333334, 2488969405u, 2236050885u},
        {0.9, 1664746702u, 2236050885u},
        {0.975, 2123157957u, 2236050885u},
        {0.9833333333333333, 2123157957u, 2236050885u},
        {1, 3762857364u, 2236050885u},
        {1.0416666666666667, 2123157957u, 2236050885u},
        {1.05, 2123157957u, 2236050885u},
        {1.0666666666666667, 2123157957u, 2236050885u},
        {1.1, 2123157957u, 2236050885u},
        {1.2, 547348368u, 3849632648u},
        {1.3, 334542306u, 2224052938u},
        {1.4, 1409487903u, 4066913797u},
        {1.5, 2972493286u, 3205131565u},
        {1.6, 3363864149u, 2400393679u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.splitzeroSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 2495151633u, 3795839340u},
        {0.075, 2495151633u, 3795839340u},
        {0.125, 3867857509u, 1876250329u},
        {0.15, 3867857509u, 1876250329u},
        {0.2, 63813209u, 4071195950u},
        {0.2833333333333333, 3949172692u, 3143542526u},
        {0.3333333333333333, 3602666576u, 1483795413u},
        {0.4166666666666667, 3451362232u, 2255786447u},
        {0.5, 1639301888u, 700432397u},
        {0.6166666666666667, 3916740873u, 831375749u},
        {0.65, 3883270616u, 1112326013u},
        {0.6666666666666666, 3097683588u, 2808390654u},
        {0.75, 3806338828u, 578172341u},
        {0.775, 634870820u, 1460007979u},
        {0.7833333333333333, 2469031140u, 2236050885u},
        {0.8, 2469031140u, 2236050885u},
        {0.8333333333333334, 3080405605u, 2236050885u},
        {0.9, 110706260u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 3444486633u, 4031645997u},
        {1.3, 3793360613u, 2545822525u},
        {1.4, 1378650817u, 1190566469u},
        {1.5, 1547041317u, 1759919592u},
        {1.6, 119818608u, 1087213677u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(0)json");
        REQUIRE(input);
        value.find("options")->set("split", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.splittransparentSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 1205018293u, 2786117776u},
        {0.075, 1205018293u, 2786117776u},
        {0.125, 3038614549u, 3622015929u},
        {0.15, 3038614549u, 3622015929u},
        {0.2, 3254790641u, 2736366372u},
        {0.2833333333333333, 4074753376u, 2218666220u},
        {0.3333333333333333, 2483411120u, 3193991489u},
        {0.4166666666666667, 3698268033u, 2312989536u},
        {0.5, 2317894676u, 3707141108u},
        {0.6166666666666667, 1597366028u, 3603362947u},
        {0.65, 3777040116u, 2198093766u},
        {0.6666666666666666, 3304927168u, 3952353762u},
        {0.75, 3048201028u, 1517784168u},
        {0.775, 2826870888u, 606678275u},
        {0.7833333333333333, 1559442720u, 2236050885u},
        {0.8, 1559442720u, 2236050885u},
        {0.8333333333333334, 3713251381u, 2236050885u},
        {0.9, 187131312u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 524879636u, 1795314939u},
        {1.3, 4283261612u, 2598013362u},
        {1.4, 3467918121u, 739723893u},
        {1.5, 2271628492u, 1862032656u},
        {1.6, 2045149717u, 933246146u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(66051)json");
        REQUIRE(input);
        value.find("options")->set("split", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.splitpartialSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 3639028789u, 475709867u},
        {0.075, 3639028789u, 475709867u},
        {0.125, 2041748405u, 2382599800u},
        {0.15, 2041748405u, 2382599800u},
        {0.2, 2991149201u, 3229352171u},
        {0.2833333333333333, 2138096000u, 3454359257u},
        {0.3333333333333333, 4173910448u, 3127502505u},
        {0.4166666666666667, 3649208897u, 12781532u},
        {0.5, 3902518228u, 175416040u},
        {0.6166666666666667, 872318316u, 933188780u},
        {0.65, 2821815476u, 1233109873u},
        {0.6666666666666666, 4129732448u, 1945035718u},
        {0.75, 674834084u, 3598501592u},
        {0.775, 2174300776u, 2208972918u},
        {0.7833333333333333, 189802656u, 2236050885u},
        {0.8, 189802656u, 2236050885u},
        {0.8333333333333334, 3358057621u, 2236050885u},
        {0.9, 2896901680u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 2066265556u, 3415779364u},
        {1.3, 408660172u, 2502362615u},
        {1.4, 468794377u, 137758007u},
        {1.5, 4160726732u, 928430944u},
        {1.6, 506549941u, 3766422903u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("split", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.splitwhiteSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 773249389u, 333181499u},
        {0.075, 773249389u, 333181499u},
        {0.125, 1404303793u, 2024759032u},
        {0.15, 1404303793u, 2024759032u},
        {0.2, 1321357089u, 157512547u},
        {0.2833333333333333, 1491562264u, 2807899387u},
        {0.3333333333333333, 536051912u, 2613814837u},
        {0.4166666666666667, 62481501u, 2685278384u},
        {0.5, 3631188568u, 3095406978u},
        {0.6166666666666667, 2102004184u, 2504466041u},
        {0.65, 2725862104u, 3575722473u},
        {0.6666666666666666, 2384122924u, 78367666u},
        {0.75, 499498204u, 2437318993u},
        {0.775, 2961099368u, 1386299056u},
        {0.7833333333333333, 25022996u, 2236050885u},
        {0.8, 25022996u, 2236050885u},
        {0.8333333333333334, 4246720085u, 2236050885u},
        {0.9, 836151380u, 2236050885u},
        {0.975, 1491770821u, 2236050885u},
        {0.9833333333333333, 1491770821u, 2236050885u},
        {1, 2212425016u, 2236050885u},
        {1.0416666666666667, 1491770821u, 2236050885u},
        {1.05, 1491770821u, 2236050885u},
        {1.0666666666666667, 1491770821u, 2236050885u},
        {1.1, 1491770821u, 2236050885u},
        {1.2, 3183735424u, 1982194039u},
        {1.3, 4268961404u, 4228006662u},
        {1.4, 2165065485u, 2492890125u},
        {1.5, 2717691568u, 3707070745u},
        {1.6, 205421849u, 4019009905u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("split", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
TEST_CASE("stylize.pixelfx.transitionLattice.glitchwipe.partialbothSource") {
    struct Frame {
        double   time;
        uint32_t color, light;
    };
    const Frame frames[] = {
        {0, 2236050885u, 2236050885u},
        {0.008333333333333333, 2236050885u, 2236050885u},
        {0.025, 2236050885u, 2236050885u},
        {0.03333333333333333, 2236050885u, 2236050885u},
        {0.041666666666666664, 2236050885u, 2236050885u},
        {0.06666666666666667, 2236050885u, 2236050885u},
        {0.075, 2236050885u, 2236050885u},
        {0.125, 2392568998u, 1046874226u},
        {0.15, 2392568998u, 1046874226u},
        {0.2, 148766890u, 2056781136u},
        {0.2833333333333333, 88618948u, 1674053574u},
        {0.3333333333333333, 148612105u, 1508326007u},
        {0.4166666666666667, 687157334u, 2690340254u},
        {0.5, 4276424146u, 3451896369u},
        {0.6166666666666667, 2491168250u, 3153883752u},
        {0.65, 3187100165u, 2056206611u},
        {0.6666666666666666, 2764280809u, 499888010u},
        {0.75, 1896408517u, 2236050885u},
        {0.775, 1896408517u, 2236050885u},
        {0.7833333333333333, 3007283642u, 2236050885u},
        {0.8, 3007283642u, 2236050885u},
        {0.8333333333333334, 1208443541u, 2236050885u},
        {0.9, 1171111802u, 2236050885u},
        {0.975, 1896408517u, 2236050885u},
        {0.9833333333333333, 1896408517u, 2236050885u},
        {1, 1553972354u, 2236050885u},
        {1.0416666666666667, 1896408517u, 2236050885u},
        {1.05, 1896408517u, 2236050885u},
        {1.0666666666666667, 1896408517u, 2236050885u},
        {1.1, 731882882u, 2184626337u},
        {1.2, 2616971259u, 1630907037u},
        {1.3, 3100563988u, 2358261463u},
        {1.4, 2488939544u, 1348954069u},
        {1.5, 837763901u, 4062015087u},
        {1.6, 2236050885u, 2236050885u},
        {1.7, 2236050885u, 2236050885u},
        {1.8, 2236050885u, 2236050885u},
        {1.9, 2236050885u, 2236050885u},
        {2, 2236050885u, 2236050885u},
        {2.2, 2236050885u, 2236050885u},
        {2.5, 2236050885u, 2236050885u},
        {5, 2236050885u, 2236050885u},
    };
    PixelFx fx(PixelFxKind::GlitchWipe);
    auto    value = fx.configurationValue();
    {
        auto input = eve::Value::fromJson(R"json(2148606515)json");
        REQUIRE(input);
        value.find("options")->set("split", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(2148676694)json");
        REQUIRE(input);
        value.find("options")->set("color", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json(4294967295)json");
        REQUIRE(input);
        value.find("options")->set("seed", input.value());
    }
    {
        auto input = eve::Value::fromJson(R"json([4279373632, 4281862768, 4286604976, 4292677584, 4293980400])json");
        REQUIRE(input);
        value.find("options")->set("palette", input.value());
    }
    auto prepared = PixelFx::prepareConfigurationValue(PixelFxKind::GlitchWipe, value);
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
}  // namespace test_pixelfx_transition_lattice_unit
