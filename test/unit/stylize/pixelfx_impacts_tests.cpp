// Impact-family source fixtures have independent ownership from the near5000-line core suite.
#include <algorithm>
#include <array>
#include <limits>
#include <span>
#include <vector>
#include "PixelFxFrameDiagnostic.h"
#include "stylize/PixelFx.h"
#include "stylize/pixelfx/Impact.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_impacts_unit {
using namespace eve::stylize;
using namespace eve::stylize::pixelfx;
void sample(PixelFx& fx, double time) {
    auto seek = fx.prepareSeek(time);
    REQUIRE(seek);
    REQUIRE(fx.commitSeek(std::move(seek).takeValue()));
    fx.draw();
}
TEST_CASE("stylize.pixelfx.impacts.bluntHitPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.075,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.25,
                                             0.35,
                                             0.5,
                                             0.65,
                                             0.75,
                                             0.8916666666666667,
                                             0.9,
                                             1.25};
    constexpr std::array<uint32_t, 36> sums{
        3482937254u, 3404576073u, 3482937254u, 541205961u,  4001658078u, 2236302849u, 1654562922u, 1126606245u,
        3774381738u, 431899497u,  2633388617u, 3351462005u, 3755730402u, 3216930861u, 2520036517u, 4293814741u,
        248958005u,  257166825u,  2641840742u, 1899818697u, 2177611491u, 3571187033u, 326347985u,  479611413u,
        1842620309u, 1093688773u, 3052327489u, 1093688773u, 3711360285u, 1093688773u, 1093688773u, 1093688773u,
        1093688773u, 1093688773u, 1093688773u, 1093688773u};
    PixelFx fx(PixelFxKind::BluntHit), other(PixelFxKind::BluntHit);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        if (c != sums[i * 2] || l != sums[i * 2 + 1])
            test_pixelfx_diagnostic::dumpSequence("impacts/blunt-hit", fx, times.size(), [&](size_t j) {
                return test_pixelfx_diagnostic::ExpectedFrame{times[j], sums[2 * j], sums[2 * j + 1]};
            });
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::BluntHit, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::BluntHit, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::BluntHit, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.25);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.bluntHitConfigurationPublication") {
    PixelFx fx(PixelFxKind::BluntHit);
    sample(fx, .1);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (double height :
         {-1., 256.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto s          = fx.settings();
        s.impact.height = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BluntHit, s));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    for (auto member : {&PixelFxImpactSettings::warm, &PixelFxImpactSettings::dust, &PixelFxImpactSettings::clod}) {
        auto s           = fx.settings();
        s.impact.*member = 0x00ffffffu;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BluntHit, s));
        REQUIRE(fx.configurationValue() == before);
    }
    auto s          = fx.settings();
    s.impact.height = 32.5;
    s.impact.warm   = 0xff2233eeu;
    s.impact.dust   = 0xff664422u;
    s.impact.clod   = 0xff335577u;
    auto candidate  = PixelFx::prepareConfiguration(PixelFxKind::BluntHit, s);
    REQUIRE(candidate);
    s.impact.warm = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(fx, .1);
    REQUIRE(fx.settings().impact.warm == 0xff2233eeu);
    REQUIRE(fx.settings().impact.height == 32.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::BluntHit, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    for (double height : {0., 256.}) {
        auto p          = fx.settings();
        p.impact.height = height;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::BluntHit, p));
    }
    auto bad = value;
    bad.find("options")->set("speed", eve::Value(1.));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::BluntHit, bad));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.bulletImpactPhasesMaterialsAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.075,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.25,
                                             0.35,
                                             0.5,
                                             0.75,
                                             1,
                                             1.3916666666666666,
                                             1.4,
                                             1.75};
    constexpr std::array<uint32_t, 36> sums{
        4061432978u, 468685585u,  3842500348u, 1381492485u, 2963776112u, 2222639858u, 877546734u,  649237178u,
        920234587u,  1157688462u, 3424315194u, 1738922452u, 477815273u,  720086646u,  2316620785u, 878012731u,
        451193718u,  1126577972u, 4262232073u, 3854263114u, 4043635990u, 1238789767u, 240047369u,  1572576588u,
        3200414851u, 1006259829u, 1595655500u, 778398245u,  1160696826u, 418274245u,  418274245u,  418274245u,
        418274245u,  418274245u,  418274245u,  418274245u};
    PixelFx fx(PixelFxKind::BulletImpact), other(PixelFxKind::BulletImpact);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::BulletImpact, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::BulletImpact, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::BulletImpact, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.75);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.bulletImpactMaterialPublication") {
    PixelFx fx(PixelFxKind::BulletImpact);
    sample(fx, .1);
    auto before    = fx.configurationValue();
    auto s         = fx.settings();
    s.impact.stone = 0x00ffffffu;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BulletImpact, s));
    REQUIRE(fx.configurationValue() == before);
    s               = fx.settings();
    s.impact.height = -1;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::BulletImpact, s));
    s.impact.height = 32.5;
    s.impact.stone  = 0xff335577u;
    auto p          = PixelFx::prepareConfiguration(PixelFxKind::BulletImpact, s);
    REQUIRE(p);
    s.impact.stone = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.settings().impact.stone == 0xff335577u);
    auto v  = fx.configurationValue();
    auto rt = PixelFx::prepareConfigurationValue(PixelFxKind::BulletImpact, v);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == v);
}
TEST_CASE("stylize.pixelfx.impacts.critHitPhasesAndTransactions") {
    constexpr std::array<double, 19>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.15833333333333333,
                                             0.2,
                                             0.25,
                                             0.35,
                                             0.5,
                                             0.75,
                                             0.9,
                                             1.5};
    constexpr std::array<uint32_t, 38> sums{
        3129835273u, 3328537357u, 3129835273u, 3822273273u, 3129835273u, 3891679253u, 2236922784u, 4121854596u,
        2694709597u, 1009857706u, 2978342405u, 997245443u,  912781223u,  2641250751u, 585282873u,  3492598750u,
        1135982040u, 2627169972u, 2882674020u, 1809909457u, 4260001758u, 746293807u,  2782636915u, 4281370080u,
        3259947265u, 2400396230u, 411862943u,  114551716u,  332069703u,  2865564185u, 3871763929u, 402338494u,
        1729483832u, 2904611269u, 2904611269u, 2904611269u, 2904611269u, 2904611269u};
    PixelFx fx(PixelFxKind::CritHit), other(PixelFxKind::CritHit);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::CritHit, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::CritHit, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::CritHit, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.critHitDirectionPublication") {
    PixelFx fx(PixelFxKind::CritHit);
    sample(fx, .05);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (auto member : {&PixelFxImpactSettings::height, &PixelFxImpactSettings::aim}) {
        for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 257.}) {
            auto s           = fx.settings();
            s.impact.*member = value;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::CritHit, s));
            REQUIRE(fx.configurationValue() == before);
            REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
        }
    }
    for (double aim : {-6.283185307179586, 0., 6.283185307179586}) {
        auto s       = fx.settings();
        s.impact.aim = aim;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::CritHit, s));
    }
    auto s          = fx.settings();
    s.impact.aim    = -.5;
    s.impact.height = 32.5;
    auto candidate  = PixelFx::prepareConfiguration(PixelFxKind::CritHit, s);
    REQUIRE(candidate);
    s.impact.aim = 0;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(fx, .05);
    REQUIRE(fx.settings().impact.aim == -.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::CritHit, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.hitSparkPhasesAndTransactions") {
    constexpr std::array<double, 16>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.25,
                                             0.35,
                                             0.5,
                                             0.75};
    constexpr std::array<uint32_t, 32> sums{
        688359632u, 620119998u,  3529427144u, 650339488u,  1721199872u, 3230050546u, 3939391466u, 972784849u,
        573294933u, 750022932u,  1921016979u, 225070145u,  3583724110u, 1840416449u, 1938552566u, 1649679388u,
        841631038u, 3877600337u, 3625147146u, 858270268u,  584537429u,  1039999717u, 626454477u,  1992123899u,
        74480428u,  3251802211u, 1511132676u, 4170553346u, 2443257285u, 2443257285u, 2443257285u, 2443257285u};
    PixelFx fx(PixelFxKind::HitSpark), other(PixelFxKind::HitSpark);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::HitSpark, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HitSpark, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HitSpark, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, .75);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.hitSparkDirectionPublication") {
    PixelFx fx(PixelFxKind::HitSpark);
    sample(fx, .05);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (auto member : {&PixelFxImpactSettings::height, &PixelFxImpactSettings::aim}) {
        for (double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(), 257.}) {
            auto s           = fx.settings();
            s.impact.*member = value;
            REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HitSpark, s));
            REQUIRE(fx.configurationValue() == before);
            REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
        }
    }
    for (double aim : {-6.283185307179586, 0., 6.283185307179586}) {
        auto s       = fx.settings();
        s.impact.aim = aim;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::HitSpark, s));
    }
    auto s          = fx.settings();
    s.impact.aim    = -.5;
    s.impact.height = 32.5;
    auto candidate  = PixelFx::prepareConfiguration(PixelFxKind::HitSpark, s);
    REQUIRE(candidate);
    s.impact.aim = 0;
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(fx, .05);
    REQUIRE(fx.settings().impact.aim == -.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HitSpark, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.hitFlashPhasesAndTransactions") {
    constexpr std::array<double, 15>   times{0,
                                             0.008333333333333333,
                                             0.025,
                                             0.03333333333333333,
                                             0.058333333333333334,
                                             0.06666666666666667,
                                             0.125,
                                             0.13333333333333333,
                                             0.15833333333333333,
                                             0.2,
                                             0.25,
                                             0.35,
                                             0.36666666666666664,
                                             0.5,
                                             0.75};
    constexpr std::array<uint32_t, 30> sums{
        3906658267u, 3859614212u, 441710022u,  3647820946u, 65666768u,   1451248830u, 936401877u,  3271754173u,
        1793601099u, 2101469650u, 1059316584u, 275804725u,  891230819u,  680998117u,  663914876u,  2859156269u,
        1461139856u, 893195917u,  951070232u,  206046901u,  1693302763u, 911236381u,  3357890037u, 1849482693u,
        1849482693u, 1849482693u, 1849482693u, 1849482693u, 1849482693u, 1849482693u};
    PixelFx fx(PixelFxKind::HitFlash), other(PixelFxKind::HitFlash);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .1);
    sample(other, .1);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::HitFlash, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .1);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .1);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HitFlash, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HitFlash, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.1));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, .75);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .1);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.hitFlashActorAndParameterPublication") {
    PixelFx fx(PixelFxKind::HitFlash);
    sample(fx, .1);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (double side :
         {-8.01, 8.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto s        = fx.settings();
        s.impact.side = side;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    for (int lines : {-1, 129}) {
        auto s         = fx.settings();
        s.impact.lines = lines;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s));
    }
    for (int lines : {0, 1, 128}) {
        auto s         = fx.settings();
        s.impact.lines = lines;
        s.impact.side  = -8;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s));
    }
    auto bad = before;
    bad.find("options")->set("lines", eve::Value(1.5));
    REQUIRE(!PixelFx::prepareConfigurationValue(PixelFxKind::HitFlash, bad));
    auto s         = fx.settings();
    s.actor.source = PixelFxActorSource::None;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s));
    s              = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    s.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s));
    s              = fx.settings();
    s.actor.source = PixelFxActorSource::Custom;
    std::fill(s.actor.pixels.begin(), s.actor.pixels.end(), 0xff336699u);
    s.impact.side  = -.5;
    s.impact.lines = 12;
    auto candidate = PixelFx::prepareConfiguration(PixelFxKind::HitFlash, s);
    REQUIRE(candidate);
    std::fill(s.actor.pixels.begin(), s.actor.pixels.end(), 0xff000000u);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(fx, .1);
    REQUIRE(fx.settings().actor.pixels[0] == 0xff336699u);
    REQUIRE(fx.settings().impact.lines == 12);
    REQUIRE(fx.settings().impact.side == -.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HitFlash, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.parryPhasesAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.125,
                                             0.15,
                                             0.2,
                                             0.3,
                                             0.4,
                                             0.6,
                                             0.9,
                                             1,
                                             1.5};
    constexpr std::array<uint32_t, 36> sums{
        1705209073u, 34372961u,   1705209073u, 3039584229u, 1171785856u, 1179968119u, 1684559480u, 946921274u,
        1731596172u, 1776791936u, 4020581656u, 3352682925u, 2939275448u, 3600648507u, 1065401252u, 135364857u,
        4122669082u, 2978793244u, 551179413u,  3527265294u, 241105929u,  665966351u,  771037281u,  1445311735u,
        513189490u,  2813356132u, 376131742u,  295883784u,  2701091033u, 2806801527u, 1848227269u, 1848227269u,
        1848227269u, 1848227269u, 1848227269u, 1848227269u};
    PixelFx fx(PixelFxKind::Parry), other(PixelFxKind::Parry);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Parry, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Parry, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Parry, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.parryHeightPublication") {
    PixelFx fx(PixelFxKind::Parry);
    sample(fx, .1);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (double height :
         {-1., 256.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto s          = fx.settings();
        s.impact.height = height;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Parry, s));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    for (double height : {0., 256.}) {
        auto s          = fx.settings();
        s.impact.height = height;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::Parry, s));
    }
    auto s          = fx.settings();
    s.impact.height = 32.5;
    auto p          = PixelFx::prepareConfiguration(PixelFxKind::Parry, s);
    REQUIRE(p);
    s.impact.height = 0;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .1);
    REQUIRE(fx.settings().impact.height == 32.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Parry, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.ricochetPhasesAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.3,
                                             0.5,
                                             1,
                                             1.6,
                                             3.2,
                                             4.5};
    constexpr std::array<uint32_t, 36> sums{
        3850724975u, 3848108563u, 1930576270u, 625002934u,  1812700974u, 894999130u,  2348590926u, 2703939154u,
        4220731374u, 440600399u,  2187294226u, 3125469645u, 3424200983u, 1781486403u, 3436132826u, 1667064753u,
        3603933422u, 201531629u,  707184011u,  2156544542u, 327771338u,  1619674603u, 1686216509u, 2927181056u,
        4183425438u, 1233978609u, 1322636118u, 808799463u,  4292808597u, 2648897477u, 2648897477u, 2648897477u,
        2648897477u, 2648897477u, 2648897477u, 2648897477u};
    PixelFx fx(PixelFxKind::Ricochet), other(PixelFxKind::Ricochet);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::Ricochet, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Ricochet, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::Ricochet, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.shieldblockPhasesAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.3,
                                             0.5,
                                             1,
                                             1.6,
                                             3.2,
                                             4.5};
    constexpr std::array<uint32_t, 36> sums{
        3116673603u, 3329528931u, 3549967639u, 2171606748u, 3506214266u, 49289537u,   858646966u,  2532397443u,
        67112772u,   4047482523u, 681432700u,  726978809u,  2053931544u, 2838566263u, 716775471u,  3147518019u,
        2589512748u, 674144750u,  567475690u,  2914562562u, 4126919270u, 1596881054u, 521070674u,  3015294431u,
        3618062311u, 932969636u,  2875099303u, 875406409u,  443899837u,  1379755816u, 4237686725u, 4237686725u,
        4237686725u, 4237686725u, 4237686725u, 4237686725u};
    PixelFx fx(PixelFxKind::ShieldBlock), other(PixelFxKind::ShieldBlock);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 8u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ShieldBlock, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 1.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.ricochetStonePublication") {
    PixelFx fx(PixelFxKind::Ricochet);
    sample(fx, .1);
    const auto before = fx.configurationValue();
    auto       s      = fx.settings();
    s.impact.stone    = 0x00ffffffu;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Ricochet, s));
    REQUIRE(fx.configurationValue() == before);
    s              = fx.settings();
    s.impact.stone = 0xff335577u;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::Ricochet, s);
    REQUIRE(p);
    s.impact.stone = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.settings().impact.stone == 0xff335577u);
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::Ricochet, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.shieldBlockRampAndActorPublication") {
    PixelFx fx(PixelFxKind::ShieldBlock);
    sample(fx, .025);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (const auto& ramp :
         std::vector<std::vector<uint32_t>>{{}, std::vector<uint32_t>(65, 0xffffffffu), {0x00ffffffu}}) {
        auto s          = fx.settings();
        s.impact.sparks = ramp;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, s));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    auto s         = fx.settings();
    s.actor.source = PixelFxActorSource::None;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .025);
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    std::vector<uint32_t> body(fx.color().size());
    REQUIRE(fx.drawActor(body));
    for (auto c : body) REQUIRE(c == 0u);
    s              = fx.settings();
    s.actor.source = PixelFxActorSource::Hero;
    p              = PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, s);
    REQUIRE(p);
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    REQUIRE(fx.drawActor(body));
    REQUIRE(std::any_of(body.begin(), body.end(), [](uint32_t c) { return c != 0; }));
    s               = fx.settings();
    s.impact.sparks = {0xff335577u};
    p               = PixelFx::prepareConfiguration(PixelFxKind::ShieldBlock, s);
    REQUIRE(p);
    s.impact.sparks[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .025);
    REQUIRE(fx.settings().impact.sparks.size() == 4u);
    REQUIRE(fx.settings().impact.sparks[0] == 0xff335577u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::ShieldBlock, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
TEST_CASE("stylize.pixelfx.impacts.ownedLayerDrawMatchesCanvasAndRejectsInvalid") {
    for (auto kind : {PixelFxKind::ShieldBlock, PixelFxKind::Ricochet, PixelFxKind::DivineShield, PixelFxKind::Halo}) {
        PixelFx               fx(kind);
        const auto            fxInfo = fx.info();
        std::vector<uint32_t> color(fx.color().size()), light(fx.light().size());
        for (double time : {.025, .2, .6}) {
            sample(fx, time);
            const auto tick  = fx.activeTickCount();
            const auto clock = fx.effectTime();
            for (auto layer : {PixelFxLayer::Back, PixelFxLayer::Front, PixelFxLayer::Both}) {
                std::fill(color.begin(), color.end(), 0u);
                std::fill(light.begin(), light.end(), 0u);
                REQUIRE(fx.drawToCanvas({fxInfo.width, fxInfo.height, fxInfo.originX, fxInfo.originY, color, light},
                                        layer, {}));
                REQUIRE(fx.drawLayer(layer));
                REQUIRE(std::equal(fx.color().begin(), fx.color().end(), color.begin()));
                REQUIRE(std::equal(fx.light().begin(), fx.light().end(), light.begin()));
                REQUIRE(fx.activeTickCount() == tick);
                REQUIRE(fx.effectTime() == clock);
            }
            REQUIRE(!fx.drawLayer(static_cast<PixelFxLayer>(99)));
            REQUIRE(std::equal(fx.color().begin(), fx.color().end(), color.begin()));
            REQUIRE(std::equal(fx.light().begin(), fx.light().end(), light.begin()));
        }
        fx.stop();
        REQUIRE(fx.advance(.31));
        REQUIRE(fx.drawLayer(PixelFxLayer::Back));
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
    }
}
TEST_CASE("stylize.pixelfx.impacts.heavylandingPhasesAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.3,
                                             0.5,
                                             1,
                                             1.6,
                                             3.2,
                                             4.5};
    constexpr std::array<uint32_t, 36> sums{
        2869749003u, 3158701637u, 2643860016u, 1620383365u, 2599652820u, 169225005u,  1960995394u, 2005418421u,
        1967859539u, 3478693509u, 2761834884u, 3068795621u, 4253939921u, 1953994549u, 2916264717u, 3654275941u,
        1455355897u, 548860453u,  3570575480u, 432306293u,  408424002u,  1102400965u, 1480808623u, 1102400965u,
        657744241u,  1102400965u, 4289748414u, 1102400965u, 170150359u,  1102400965u, 1102400965u, 1102400965u,
        1102400965u, 1102400965u, 1102400965u, 1102400965u};
    PixelFx fx(PixelFxKind::HeavyLanding), other(PixelFxKind::HeavyLanding);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::HeavyLanding, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::HeavyLanding, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::HeavyLanding, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 4.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.groundslamPhasesAndTransactions") {
    constexpr std::array<double, 18>   times{0,
                                             0.008333333333333333,
                                             0.016666666666666666,
                                             0.025,
                                             0.03333333333333333,
                                             0.05,
                                             0.058333333333333334,
                                             0.08333333333333333,
                                             0.09166666666666666,
                                             0.1,
                                             0.15,
                                             0.2,
                                             0.3,
                                             0.5,
                                             1,
                                             1.6,
                                             3.2,
                                             4.5};
    constexpr std::array<uint32_t, 36> sums{
        2107966314u, 3065555261u, 1217047161u, 3998382746u, 3047781748u, 3926095108u, 1768154284u, 3202659474u,
        4208292768u, 577256263u,  2018342460u, 3399053994u, 3502293295u, 907350477u,  873229576u,  1422860860u,
        3651229197u, 4155934059u, 2328049374u, 943082417u,  22516121u,   2733015530u, 454309733u,  273182080u,
        4224041656u, 1887716190u, 3122190532u, 2900871105u, 2265449890u, 3165504307u, 945118358u,  661741871u,
        602062277u,  602062277u,  602062277u,  602062277u};
    PixelFx fx(PixelFxKind::GroundSlam), other(PixelFxKind::GroundSlam);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i]);
        uint32_t c = 2166136261u, l = c;
        for (auto b : std::as_bytes(fx.color())) c = (c ^ uint8_t(b)) * 16777619u;
        for (auto b : std::as_bytes(fx.light())) l = (l ^ uint8_t(b)) * 16777619u;
        REQUIRE(c == sums[i * 2]);
        REQUIRE(l == sums[i * 2 + 1]);
    }
    sample(fx, .375);
    sample(other, .375);
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    auto                        stale = fx.prepareSeek(.5);
    REQUIRE(stale);
    auto settings    = fx.settings();
    settings.seed    = 123;
    settings.palette = {0xff403020u, 0xffc0a080u};
    auto p           = PixelFx::prepareConfiguration(PixelFxKind::GroundSlam, settings);
    REQUIRE(p);
    settings.palette[0] = 0xff000000u;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .375);
    REQUIRE(fx.settings().palette.size() == 7u);
    REQUIRE(fx.settings().palette[0] == 0xff403020u);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    REQUIRE(!fx.commitSeek(std::move(stale).takeValue()));
    sample(other, .375);
    REQUIRE(std::equal(other.color().begin(), other.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::GroundSlam, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
    auto defaults = PixelFx::prepareConfiguration(PixelFxKind::GroundSlam, other.settings());
    REQUIRE(defaults);
    REQUIRE(fx.commitConfiguration(std::move(defaults).takeValue()));
    REQUIRE(fx.advance(.375));
    fx.draw();
    REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    sample(fx, 4.5);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    REQUIRE(!fx.prepareSeek(1e12));
    sample(fx, .375);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.impacts.groundMaterialAndReachPublication") {
    for (auto kind : {PixelFxKind::HeavyLanding, PixelFxKind::GroundSlam}) {
        PixelFx fx(kind);
        sample(fx, .2);
        const auto before = fx.configurationValue();
        auto       s      = fx.settings();
        s.impact.stone    = 0x00ffffffu;
        REQUIRE(!PixelFx::prepareConfiguration(kind, s));
        REQUIRE(fx.configurationValue() == before);
        s              = fx.settings();
        s.impact.stone = 0xff335577u;
        auto p         = PixelFx::prepareConfiguration(kind, s);
        REQUIRE(p);
        s.impact.stone = 0xff000000u;
        REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
        REQUIRE(fx.settings().impact.stone == 0xff335577u);
        const auto value = fx.configurationValue();
        auto       rt    = PixelFx::prepareConfigurationValue(kind, value);
        REQUIRE(rt);
        REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
        REQUIRE(fx.configurationValue() == value);
    }
    PixelFx fx(PixelFxKind::GroundSlam);
    sample(fx, .2);
    const auto                  before = fx.configurationValue();
    const std::vector<uint32_t> original(fx.color().begin(), fx.color().end());
    for (double reach :
         {-.01, 512.01, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
        auto s         = fx.settings();
        s.impact.reach = reach;
        REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::GroundSlam, s));
        REQUIRE(fx.configurationValue() == before);
        REQUIRE(std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    }
    for (double reach : {0., 512.}) {
        auto s         = fx.settings();
        s.impact.reach = reach;
        REQUIRE(PixelFx::prepareConfiguration(PixelFxKind::GroundSlam, s));
    }
    auto s         = fx.settings();
    s.impact.reach = 72.5;
    auto p         = PixelFx::prepareConfiguration(PixelFxKind::GroundSlam, s);
    REQUIRE(p);
    s.impact.reach = 0;
    REQUIRE(fx.commitConfiguration(std::move(p).takeValue()));
    sample(fx, .2);
    REQUIRE(fx.settings().impact.reach == 72.5);
    REQUIRE(!std::equal(fx.color().begin(), fx.color().end(), original.begin()));
    const auto value = fx.configurationValue();
    auto       rt    = PixelFx::prepareConfigurationValue(PixelFxKind::GroundSlam, value);
    REQUIRE(rt);
    REQUIRE(fx.commitConfiguration(std::move(rt).takeValue()));
    REQUIRE(fx.configurationValue() == value);
}
}  // namespace test_pixelfx_impacts_unit
