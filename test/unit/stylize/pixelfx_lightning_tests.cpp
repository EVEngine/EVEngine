// Lightning owns local priority inking and loop trajectories; the core stylize suite exceeds5000 lines.
#include <algorithm>
#include <array>
#include "stylize/PixelFx.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace test_pixelfx_lightning_unit {
using namespace eve::stylize;
uint32_t checksum(std::span<const uint32_t> pixels) {
    uint32_t h = 2166136261u;
    for (auto c : pixels)
        for (int shift = 0; shift < 32; shift += 8) h = (h ^ ((c >> shift) & 255u)) * 16777619u;
    return h;
}
TEST_CASE("stylize.pixelfx.lightning.arcBallReferenceAndPrivateInking") {
    constexpr std::array<std::array<int, 9>, 2> times{
        {{{0, 1, 14, 24, 42, 43, 44, 58, 60}}, {{0, 15, 30, 60, 62, 63, 65, 90, 120}}}};
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{2795063845u, 2509788171u, 1308085406u, 3333595058u, 1002783783u, 3359394479u, 4235362325u, 4097532413u,
           410086868u, 4219361185u, 1943335517u, 2081878508u, 827302381u, 3590467445u, 2270672283u, 3357681311u,
           2795063845u, 2509788171u}},
         {{3008668477u, 964459586u, 3883035787u, 4045720486u, 845111038u, 4127667753u, 2098811961u, 1793244077u,
           1443832359u, 2339411136u, 3981129072u, 2973773231u, 2498807712u, 3232707300u, 29737647u, 415963202u,
           3008668477u, 964459586u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::SparkArc, PixelFxKind::BallLightning}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (size_t i = 0; i < 9; ++i) {
            fx.render(times[row][i] / 30.);
            REQUIRE(checksum(fx.color()) == expected[row][i * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][i * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       old      = fx.prepareSeek(.6);
        REQUIRE(old);
        auto snapshot = fx.configurationValue();
        snapshot.find("options")->set("seed", eve::Value(int64_t(123)));
        snapshot.find("options")->set(
            "sparks", eve::Value::array({eve::Value(int64_t(0xff403020)), eve::Value(int64_t(0xff806040)),
                                         eve::Value(int64_t(0xffc0a080))}));
        auto candidate = PixelFx::prepareConfigurationValue(kind, snapshot);
        REQUIRE(candidate);
        REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
        REQUIRE(fx.settings().lightning.sparks.size() == 4u);
        fx.render(.6);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
        split.render(.6);
        REQUIRE(checksum(split.color()) == original);
        fx.render(.6);
        REQUIRE(checksum(fx.color()) == changed);
        snapshot.find("options")->set("sparks", eve::Value::array({}));
        auto invalid = PixelFx::prepareConfigurationValue(kind, snapshot);
        REQUIRE(!invalid);
        REQUIRE(invalid.status().diagnostics().front().path() == "/options/sparks");
        REQUIRE(checksum(fx.color()) == changed);
        snapshot.find("options")->set("sparks", eve::Value::array({eve::Value(int64_t(0x00ffffff))}));
        REQUIRE(!PixelFx::prepareConfigurationValue(kind, snapshot));
        REQUIRE(checksum(fx.color()) == changed);
        snapshot.find("options")->set("sparks", eve::Value::array({eve::Value(int64_t(0xffffffff))}));
        auto single = PixelFx::prepareConfigurationValue(kind, snapshot);
        REQUIRE(single);
        REQUIRE(fx.commitConfiguration(std::move(single).takeValue()));
        for (auto c : fx.settings().lightning.sparks) REQUIRE(c == 0xffffffffu);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}

void sample(PixelFx& fx, double t) {
    auto candidate = fx.prepareSeek(t);
    REQUIRE(candidate);
    REQUIRE(fx.commitSeek(std::move(candidate).takeValue()));
    fx.draw();
}
TEST_CASE("stylize.pixelfx.lightning.plasmaThunderReferenceAndParameters") {
    constexpr std::array<std::array<int, 9>, 2> times{
        {{{0, 1, 14, 30, 45, 60, 70, 89, 90}}, {{0, 3, 4, 5, 6, 9, 18, 42, 57}}}};
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{3864281835u, 1892998166u, 2808934961u, 3459968423u, 62492857u, 3548268145u, 3864200333u, 1873204298u,
           38070208u, 1838341817u, 2291624310u, 3857492454u, 2916596037u, 2598168706u, 2964770947u, 1003329219u,
           3864281835u, 1892998166u}},
         {{1649489349u, 1649489349u, 919683321u, 2683578210u, 2009575253u, 2276507637u, 580597408u, 3447199169u,
           2191230675u, 1217504014u, 2568566103u, 2131627852u, 507796753u, 3784940313u, 2713494463u, 1649489349u,
           1649489349u, 1649489349u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::PlasmaBall, PixelFxKind::Thunderclap}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(fx.isSimulated() == (kind == PixelFxKind::Thunderclap));
        for (size_t i = 0; i < 9; ++i) {
            sample(fx, times[row][i] / 30.);
            REQUIRE(checksum(fx.color()) == expected[row][i * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][i * 2 + 1]);
        }
        sample(fx, 0);
        REQUIRE(fx.advance(.5));
        for (int i = 0; i < 60; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       old      = fx.prepareSeek(.5);
        REQUIRE(old);
        auto settings = fx.settings();
        settings.seed = 123;
        if (kind == PixelFxKind::PlasmaBall)
            settings.lightning.tendrils = 5;
        else
            settings.lightning.reach = 72.5;
        auto config = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        sample(fx, .5);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
        split.draw();
        REQUIRE(checksum(split.color()) == original);
        if (kind == PixelFxKind::PlasmaBall) {
            settings.lightning.tendrils = 65;
            REQUIRE(!PixelFx::prepareConfiguration(kind, settings));
            settings.lightning.tendrils = 0;
            auto zero                   = PixelFx::prepareConfiguration(kind, settings);
            REQUIRE(zero);
            REQUIRE(fx.commitConfiguration(std::move(zero).takeValue()));
            sample(fx, .5);
            REQUIRE(checksum(fx.color()) != changed);
            auto far = fx.prepareSeek(1000000000000.);
            REQUIRE(far);
            REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        } else {
            settings.lightning.reach = 0;
            REQUIRE(!PixelFx::prepareConfiguration(kind, settings));
            REQUIRE(checksum(fx.color()) == changed);
            REQUIRE(!fx.prepareSeek(1000000000000.));
            REQUIRE(checksum(fx.color()) == changed);
        }
        sample(fx, .5);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}
TEST_CASE("stylize.pixelfx.lightning.thunderActorAbsentAndOwnedCustomSnapshot") {
    PixelFx fx(PixelFxKind::Thunderclap);
    sample(fx, 4. / 30);
    const auto dummy    = checksum(fx.color());
    auto       settings = fx.settings();
    settings.actor      = {};
    auto absent         = PixelFx::prepareConfiguration(PixelFxKind::Thunderclap, settings);
    REQUIRE(absent);
    REQUIRE(fx.commitConfiguration(std::move(absent).takeValue()));
    sample(fx, 4. / 30);
    REQUIRE(checksum(fx.color()) != dummy);
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 2;
    settings.actor.height = 2;
    settings.actor.x      = -30;
    settings.actor.y      = -65;
    settings.actor.head   = {-29, -65};
    settings.actor.chest  = {-29, -64};
    settings.actor.mask   = {1, 0, 1, 1};
    settings.actor.pixels = {0xff804020u, 0, 0xff804020u, 0xff804020u};
    auto own              = PixelFx::prepareConfiguration(PixelFxKind::Thunderclap, settings);
    REQUIRE(own);
    settings.actor.mask.assign(4, 0);
    settings.actor.pixels.assign(4, 0);
    REQUIRE(fx.commitConfiguration(std::move(own).takeValue()));
    sample(fx, 4. / 30);
    REQUIRE(fx.settings().actor.mask[0] == 1u);
    REQUIRE(fx.settings().actor.pixels[0] == 0xff804020u);
    const auto owned   = checksum(fx.color());
    auto       invalid = fx.settings();
    invalid.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Thunderclap, invalid));
    REQUIRE(checksum(fx.color()) == owned);
    PixelFx other(PixelFxKind::Thunderclap);
    sample(other, 4. / 30);
    REQUIRE(checksum(other.color()) == dummy);
    sample(fx, 4. / 30);
    REQUIRE(checksum(fx.color()) == owned);
}

TEST_CASE("stylize.pixelfx.lightning.zapStrikeReferenceReplayAndTransactions") {
    constexpr std::array<std::array<int, 9>, 2> times{
        {{{0, 1, 2, 3, 6, 9, 14, 24, 27}}, {{0, 14, 18, 22, 23, 32, 38, 60, 90}}}};
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{486767262u, 2957685914u, 1385589801u, 4000477737u, 3234176508u, 2565850636u, 1831927933u, 1240970141u,
           1257469876u, 456160274u, 2481635308u, 1331580086u, 3322534239u, 2121692028u, 2279535265u, 2654787333u,
           2505821637u, 2505821637u}},
         {{589703121u, 2147261141u, 1340470246u, 2370558636u, 1882142772u, 3275611326u, 4051808015u, 3552465463u,
           2480842297u, 547639863u, 88078713u, 109239346u, 1607847928u, 3592308406u, 2245494274u, 602693216u,
           120305093u, 120305093u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::ZapHit, PixelFxKind::LightningStrike}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(fx.isSimulated());
        for (size_t i = 0; i < 9; ++i) {
            sample(fx, times[row][i] / 30.);
            REQUIRE(checksum(fx.color()) == expected[row][i * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][i * 2 + 1]);
        }
        const int    ticks = kind == PixelFxKind::ZapHit ? 48 : 168;
        const double time  = ticks / 120.;
        sample(fx, 0);
        REQUIRE(fx.advance(time));
        for (int i = 0; i < ticks; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       old      = fx.prepareSeek(time);
        REQUIRE(old);
        auto settings    = fx.settings();
        settings.seed    = 123;
        settings.palette = {0xff403020u, 0xff806040u, 0xffc0a080u};
        if (kind == PixelFxKind::LightningStrike) settings.lightning.sparks = {0xffffffffu, 0xfff0b080u, 0xffc08040u};
        auto config = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        sample(fx, time);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
        split.draw();
        REQUIRE(checksum(split.color()) == original);
        sample(fx, time);
        REQUIRE(checksum(fx.color()) == changed);
        settings.palette = {0x00ffffffu};
        REQUIRE(!PixelFx::prepareConfiguration(kind, settings));
        REQUIRE(checksum(fx.color()) == changed);
        REQUIRE(!fx.prepareSeek(1000000000000.));
        REQUIRE(checksum(fx.color()) == changed);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}

TEST_CASE("stylize.pixelfx.lightning.teslaCloudReferencePrivateFieldsAndMaterials") {
    constexpr std::array<std::array<int, 9>, 2> times{
        {{{0, 2, 14, 15, 20, 38, 47, 56, 72}}, {{0, 7, 8, 29, 31, 33, 52, 58, 90}}}};
    constexpr std::array<std::array<uint32_t, 18>, 2> expected{
        {{{1145506420u, 3705239155u, 2376620144u, 2448704287u, 547185569u, 2313288752u, 1516657080u, 1720455956u,
           4242960803u, 2070729748u, 180384729u, 3791378946u, 1163227099u, 3978577055u, 2125019246u, 1396138657u,
           1145506420u, 3705239155u}},
         {{3161885454u, 4093469017u, 2473554702u, 452760699u, 3221739877u, 2690457359u, 1024653748u, 3355654256u,
           1244654986u, 267444223u, 4150764966u, 2765704564u, 2252805811u, 523877401u, 831031442u, 98477456u,
           3161885454u, 4093469017u}}}};
    size_t row = 0;
    for (auto kind : {PixelFxKind::TeslaCoil, PixelFxKind::StormCloud}) {
        PixelFx fx(kind), split(kind);
        REQUIRE(!fx.isSimulated());
        for (size_t i = 0; i < 9; ++i) {
            fx.render(times[row][i] / 30.);
            REQUIRE(checksum(fx.color()) == expected[row][i * 2]);
            REQUIRE(checksum(fx.light()) == expected[row][i * 2 + 1]);
        }
        fx.render(0);
        REQUIRE(fx.advance(.6));
        for (int i = 0; i < 72; ++i) REQUIRE(split.advance(1. / 120));
        fx.draw();
        split.draw();
        REQUIRE(checksum(fx.color()) == checksum(split.color()));
        REQUIRE(checksum(fx.light()) == checksum(split.light()));
        const auto original = checksum(fx.color());
        auto       old      = fx.prepareSeek(.6);
        REQUIRE(old);
        auto settings = fx.settings();
        settings.seed = 123;
        const std::vector<uint32_t> material{0xff403020u, 0xff806040u, 0xffc0a080u};
        if (kind == PixelFxKind::TeslaCoil) {
            settings.lightning.copper = material;
            settings.lightning.steel  = material;
        } else {
            settings.lightning.cloud = material;
            settings.lightning.rain  = material;
        }
        auto config = PixelFx::prepareConfiguration(kind, settings);
        REQUIRE(config);
        REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
        fx.render(.6);
        const auto changed = checksum(fx.color());
        REQUIRE(changed != original);
        REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
        split.draw();
        REQUIRE(checksum(split.color()) == original);
        fx.render(.6);
        REQUIRE(checksum(fx.color()) == changed);
        if (kind == PixelFxKind::TeslaCoil) {
            REQUIRE(fx.settings().lightning.copper.size() == 6u);
            REQUIRE(fx.settings().lightning.steel.size() == 7u);
            settings.lightning.steel = {0x00ffffffu};
        } else {
            REQUIRE(fx.settings().lightning.cloud.size() == 7u);
            REQUIRE(fx.settings().lightning.rain.size() == 4u);
            settings.lightning.rain = {0x00ffffffu};
        }
        REQUIRE(!PixelFx::prepareConfiguration(kind, settings));
        REQUIRE(checksum(fx.color()) == changed);
        auto far = fx.prepareSeek(1000000000000.);
        REQUIRE(far);
        REQUIRE(fx.commitSeek(std::move(far).takeValue()));
        fx.draw();
        REQUIRE(!fx.done());
        fx.render(.6);
        fx.stop();
        REQUIRE(fx.advance(.31));
        fx.draw();
        REQUIRE(fx.done());
        for (auto c : fx.color()) REQUIRE(c == 0u);
        for (auto c : fx.light()) REQUIRE(c == 0u);
        ++row;
    }
}

TEST_CASE("stylize.pixelfx.lightning.electrifiedReferenceAndActorReplacement") {
    constexpr std::array<int, 9>       times{{0, 3, 4, 5, 22, 23, 41, 42, 60}};
    constexpr std::array<uint32_t, 18> expected{{2488387266u, 471098566u, 3565085813u, 2281390725u, 292893539u,
                                                 644272620u, 1452767182u, 3084893536u, 540965205u, 2456712727u,
                                                 1699727701u, 2828092267u, 1957145901u, 6268545u, 4266247252u,
                                                 1789256405u, 2488387266u, 471098566u}};
    PixelFx                            fx(PixelFxKind::Electrified), other(PixelFxKind::Electrified);
    REQUIRE(!fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        fx.render(times[i] / 30.);
        REQUIRE(checksum(fx.color()) == expected[i * 2]);
        REQUIRE(checksum(fx.light()) == expected[i * 2 + 1]);
    }
    fx.render(0);
    REQUIRE(fx.advance(.6));
    for (int i = 0; i < 72; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    REQUIRE(checksum(fx.color()) == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    fx.render(.1);
    other.render(.1);
    const auto stock = checksum(fx.color());
    auto       old   = fx.prepareSeek(.1);
    REQUIRE(old);
    auto settings         = fx.settings();
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 2;
    settings.actor.height = 2;
    settings.actor.scale  = 1;
    settings.actor.x      = -5.5;
    settings.actor.y      = -24.25;
    settings.actor.head   = {-4.5, -24.25};
    settings.actor.chest  = {-4.5, -23.25};
    settings.actor.eyes.clear();
    settings.actor.mask   = {1, 0, 1, 1};
    settings.actor.pixels = {0xff804020u, 0, 0xffc0a080u, 0xff804020u};
    auto custom           = PixelFx::prepareConfiguration(PixelFxKind::Electrified, settings);
    REQUIRE(custom);
    settings.actor.mask.assign(4, 0);
    settings.actor.pixels.assign(4, 0);
    REQUIRE(fx.commitConfiguration(std::move(custom).takeValue()));
    fx.render(.1);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != stock);
    REQUIRE(fx.settings().actor.mask[0] == 1u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    other.render(.1);
    REQUIRE(checksum(other.color()) == stock);
    fx.render(.1);
    REQUIRE(checksum(fx.color()) == changed);
    auto invalid = fx.settings();
    invalid.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::Electrified, invalid));
    REQUIRE(checksum(fx.color()) == changed);
    auto absent  = fx.settings();
    absent.actor = {};
    auto none    = PixelFx::prepareConfiguration(PixelFxKind::Electrified, absent);
    REQUIRE(none);
    REQUIRE(fx.commitConfiguration(std::move(none).takeValue()));
    fx.render(.1);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    settings.actor.source = PixelFxActorSource::Custom;
    auto empty            = PixelFx::prepareConfiguration(PixelFxKind::Electrified, settings);
    REQUIRE(empty);
    REQUIRE(fx.commitConfiguration(std::move(empty).takeValue()));
    fx.render(.1);
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
    auto far = fx.prepareSeek(1000000000000.);
    REQUIRE(far);
    REQUIRE(fx.commitSeek(std::move(far).takeValue()));
    fx.draw();
    REQUIRE(!fx.done());
    other.stop();
    REQUIRE(other.advance(.31));
    other.draw();
    REQUIRE(other.done());
    for (auto c : other.color()) REQUIRE(c == 0u);
    for (auto c : other.light()) REQUIRE(c == 0u);
}

TEST_CASE("stylize.pixelfx.lightning.chargeReferenceDurationAndTransactions") {
    constexpr std::array<int, 9>       times{{0, 12, 30, 45, 47, 48, 49, 54, 81}};
    constexpr std::array<uint32_t, 18> expected{{319519005u, 3654018877u, 2340808044u, 3451825923u, 3491898419u,
                                                 2108823778u, 2072723130u, 870354174u, 3742026298u, 1726750531u,
                                                 2020036897u, 438133881u, 4012831347u, 87460956u, 3532727559u,
                                                 3729887234u, 1030845893u, 1030845893u}};
    PixelFx                            fx(PixelFxKind::ChargeUp), split(PixelFxKind::ChargeUp);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i] / 30.);
        REQUIRE(checksum(fx.color()) == expected[i * 2]);
        REQUIRE(checksum(fx.light()) == expected[i * 2 + 1]);
    }
    sample(fx, 0);
    REQUIRE(fx.advance(1.8));
    for (int i = 0; i < 216; ++i) REQUIRE(split.advance(1. / 120));
    fx.draw();
    split.draw();
    REQUIRE(checksum(fx.color()) == checksum(split.color()));
    REQUIRE(checksum(fx.light()) == checksum(split.light()));
    const auto original = checksum(fx.color());
    auto       old      = fx.prepareSeek(1.8);
    REQUIRE(old);
    auto settings             = fx.settings();
    settings.seed             = 123;
    settings.lightning.charge = .95;
    settings.lightning.sparks = {0xffffffffu, 0xfff0b080u, 0xffc08040u};
    auto candidate            = PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, settings);
    REQUIRE(candidate);
    REQUIRE(fx.commitConfiguration(std::move(candidate).takeValue()));
    sample(fx, 1.8);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    split.draw();
    REQUIRE(checksum(split.color()) == original);
    settings.lightning.charge = 0;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, settings));
    REQUIRE(checksum(fx.color()) == changed);
    settings.lightning.charge = 8.1;
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, settings));
    auto malformed = fx.configurationValue();
    malformed.find("options")->set("charge", eve::Value(true));
    auto invalid = PixelFx::prepareConfigurationValue(PixelFxKind::ChargeUp, malformed);
    REQUIRE(!invalid);
    REQUIRE(invalid.status().diagnostics().front().path() == "/options/charge");
    auto longSettings             = split.settings();
    longSettings.lightning.charge = 8;
    auto longCharge               = PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, longSettings);
    REQUIRE(longCharge);
    REQUIRE(fx.commitConfiguration(std::move(longCharge).takeValue()));
    sample(fx, 964. / 120);
    REQUIRE(fx.done());
    constexpr std::array<uint32_t, 2> late{{3799949412u, 2690367233u}};
    REQUIRE(checksum(fx.color()) == late[0]);
    REQUIRE(checksum(fx.light()) == late[1]);
    REQUIRE(!fx.prepareSeek(1000000000000.));
    REQUIRE(checksum(fx.color()) == late[0]);
    sample(fx, 1.8);
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
TEST_CASE("stylize.pixelfx.lightning.chargeActorOwnershipAndMissingBody") {
    PixelFx fx(PixelFxKind::ChargeUp);
    auto    settings      = fx.settings();
    settings.actor.source = PixelFxActorSource::Custom;
    settings.actor.width  = 2;
    settings.actor.height = 2;
    settings.actor.scale  = 1;
    settings.actor.x      = -30;
    settings.actor.y      = -65;
    settings.actor.head   = {-29, -65};
    settings.actor.chest  = {-29, -64};
    settings.actor.eyes.clear();
    settings.actor.mask   = {1, 0, 1, 1};
    settings.actor.pixels = {0xff804020u, 0, 0xffc0a080u, 0xff804020u};
    auto prepared         = PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, settings);
    REQUIRE(prepared);
    settings.actor.mask.assign(4, 0);
    settings.actor.pixels.assign(4, 0);
    REQUIRE(fx.commitConfiguration(std::move(prepared).takeValue()));
    sample(fx, 49. / 30);
    const auto owned = checksum(fx.color());
    REQUIRE(fx.settings().actor.mask[0] == 1u);
    auto bad = fx.settings();
    bad.actor.mask.pop_back();
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, bad));
    REQUIRE(checksum(fx.color()) == owned);
    auto absent  = fx.settings();
    absent.actor = {};
    auto none    = PixelFx::prepareConfiguration(PixelFxKind::ChargeUp, absent);
    REQUIRE(none);
    REQUIRE(fx.commitConfiguration(std::move(none).takeValue()));
    sample(fx, 49. / 30);
    REQUIRE(checksum(fx.color()) != owned);
    REQUIRE(std::any_of(fx.color().begin(), fx.color().end(), [](uint32_t c) { return c != 0; }));
}

TEST_CASE("stylize.pixelfx.lightning.chainReferenceHopsAndTransactions") {
    constexpr std::array<int, 9>       times{{0, 3, 4, 5, 7, 10, 24, 30, 54}};
    constexpr std::array<uint32_t, 18> expected{{3728570821u, 3728570821u, 1405090077u, 3269395004u, 2645858067u,
                                                 3024514806u, 2584721969u, 2112036407u, 352422283u, 2588810382u,
                                                 2835421215u, 1578138736u, 898662778u, 2825797430u, 2841475053u,
                                                 1878405694u, 3728570821u, 3728570821u}};
    PixelFx                            fx(PixelFxKind::ChainLightning), other(PixelFxKind::ChainLightning);
    REQUIRE(fx.isSimulated());
    for (size_t i = 0; i < times.size(); ++i) {
        sample(fx, times[i] / 30.);
        REQUIRE(checksum(fx.color()) == expected[i * 2]);
        REQUIRE(checksum(fx.light()) == expected[i * 2 + 1]);
    }
    sample(fx, 0);
    REQUIRE(fx.advance(.6));
    for (int i = 0; i < 72; ++i) REQUIRE(other.advance(1. / 120));
    fx.draw();
    other.draw();
    REQUIRE(checksum(fx.color()) == checksum(other.color()));
    REQUIRE(checksum(fx.light()) == checksum(other.light()));
    const auto original = checksum(fx.color());
    auto       old      = fx.prepareSeek(.6);
    REQUIRE(old);
    auto settings             = fx.settings();
    settings.seed             = 123;
    settings.palette          = {0xff403020u, 0xff806040u, 0xffc0a080u};
    settings.lightning.sparks = {0xffffffffu, 0xfff0b080u, 0xffc08040u};
    auto config               = PixelFx::prepareConfiguration(PixelFxKind::ChainLightning, settings);
    REQUIRE(config);
    REQUIRE(fx.commitConfiguration(std::move(config).takeValue()));
    sample(fx, .6);
    const auto changed = checksum(fx.color());
    REQUIRE(changed != original);
    REQUIRE(fx.settings().lightning.sparks.size() == 4u);
    REQUIRE(!fx.commitSeek(std::move(old).takeValue()));
    other.draw();
    REQUIRE(checksum(other.color()) == original);
    sample(fx, .6);
    REQUIRE(checksum(fx.color()) == changed);
    settings.lightning.sparks = {0x00ffffffu};
    REQUIRE(!PixelFx::prepareConfiguration(PixelFxKind::ChainLightning, settings));
    REQUIRE(checksum(fx.color()) == changed);
    REQUIRE(!fx.prepareSeek(1000000000000.));
    REQUIRE(checksum(fx.color()) == changed);
    auto none   = fx.settings();
    none.actor  = {};
    auto absent = PixelFx::prepareConfiguration(PixelFxKind::ChainLightning, none);
    REQUIRE(absent);
    REQUIRE(fx.commitConfiguration(std::move(absent).takeValue()));
    sample(fx, .6);
    REQUIRE(checksum(fx.color()) == changed);
    REQUIRE(fx.settings().actor.mask.empty());
    fx.stop();
    REQUIRE(fx.advance(.31));
    fx.draw();
    REQUIRE(fx.done());
    for (auto c : fx.color()) REQUIRE(c == 0u);
    for (auto c : fx.light()) REQUIRE(c == 0u);
}
}  // namespace test_pixelfx_lightning_unit
