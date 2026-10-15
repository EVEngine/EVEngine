#include "fluids/OceanBindings.h"
#include "fluids/OceanSpectrum.h"
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("fluids.ocean.scriptOwnsSpectrumAndFrameBuilders") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    eve::fluids::exposeOceanTypes(table);
    vm.run(vm.compileSource(R"(
        function checked(result) {
            if (!result.ok) throw "ocean operation failed";
            return result.hasValue ? result.value : null;
        }
        local builder = eve.OceanSpectrumBuilder();
        local settings = checked(builder.settings());
        settings.seed = "18446744073709551615";
        settings.bands = [{ resolution = 16, length = 64.0,
                           minimumWavelength = 8.0, maximumWavelength = 64.0 }];
        checked(builder.configure(settings));
        seedPreserved <- checked(builder.settings()).seed == settings.seed;
        local invalid = clone settings;
        invalid.typo <- 1;
        rejected <- !builder.configure(invalid).ok;
        preserved <- checked(builder.settings()).seed == settings.seed;
        local spectrum = checked(builder.build());
        variance <- spectrum.variance();
        invalidTickRejected <- !spectrum.frameBuilder(-1, 60).ok;
        invalidRateRejected <- !spectrum.frameBuilder(1, 0).ok;
        wideRateRejected <- !spectrum.frameBuilder(1, 4294967356).ok;
        longFrame <- checked(checked(spectrum.frameBuilder(3000000001, 60)).build());
        local next = checked(spectrum.frameBuilder(75, 60));
        spectrum = null;
        builder = null;
        collectgarbage();
        local frame = checked(next.build());
        next = null;
        collectgarbage();
        frameTime <- frame.time();
        local sample = checked(frame.sample(2.0, 3.0));
        validSample <- sample.normal.len() == 3 && sample.velocity.len() == 3
                       && sample.height > -30.0 && sample.height < 30.0;
        invalidPointRejected <- !frame.sample(1.0e12, 0.0).ok;
    )"));
    CHECK(vm.find("seedPreserved").toBool());
    CHECK(vm.find("rejected").toBool());
    CHECK(vm.find("preserved").toBool());
    CHECK(vm.find("variance").toFloat() == 0.25);
    CHECK(vm.find("frameTime").toFloat() == 1.25);
    CHECK(vm.find("validSample").toBool());
    CHECK(vm.find("invalidPointRejected").toBool());
    CHECK(vm.find("invalidTickRejected").toBool());
    CHECK(vm.find("invalidRateRejected").toBool());
    CHECK(vm.find("wideRateRejected").toBool());
    const auto* longFrame = vm.find("longFrame").to<eve::fluids::OceanSpectrumFrame*>();
    REQUIRE(longFrame != nullptr);
    CHECK(longFrame->time() == 3000000001.0 / 60.0);
}

TEST_CASE("fluids.ocean.scriptBoatWakeUsesOneFixedClock") {
    auto  world = std::make_unique<eve::physics::World3D>(0.0F, -9.81F, 0.0F, false);
    auto* body  = world->newBody("dynamic", 0.0F, 0.0F, 0.0F);
    REQUIRE(body != nullptr);
    REQUIRE(body->newBoxShape(1.0F, 1.0F, 3.0F, 102.5F) != nullptr);
    ssq::VM vm(4096, ssq::Libs::ALL);
    auto    table = vm.addTable("eve");
    table.addClass<eve::physics::Body3D>("Body3D", std::function<eve::physics::Body3D*()>([] { return nullptr; }),
                                         false);
    vm.addFunc("testBody", [body] { return body; });
    vm.addFunc("testStepWorld", [&world] { world->update(1.0F / 60.0F); });
    eve::fluids::exposeOceanTypes(table);
    vm.run(vm.compileSource(R"(
        function checked(r) { if (!r.ok) throw "ocean operation failed"; return r.hasValue ? r.value : null; }
        local sb = eve.OceanSpectrumBuilder();
        local s = checked(sb.settings()); s.significantWaveHeight = 0.0;
        s.bands = [{ resolution = 8, length = 32.0, minimumWavelength = 8.0, maximumWavelength = 32.0 }];
        checked(sb.configure(s)); sea <- checked(sb.build());
        local wb = eve.OceanWakeBuilder(); local w = checked(wb.settings());
        w.resolution = 16; w.length = 32.0; w.absorptionWidth = 8.0;
        checked(wb.configure(w)); wake <- checked(wb.build());
        local bb = eve.OceanBoatBuilder(); local h = checked(bb.settings());
        h.thrust = 150.0; h.propeller = [0.0,-0.3,-1.2];
        h.cells = [];
        foreach (x in [-0.4,0.4]) foreach (z in [-1.0,1.0])
            h.cells.append({ center = [x,0.0,z], radius = 0.5, volume = 0.15 });
        checked(bb.configure(h)); boat <- checked(bb.build(testBody()));
        local bad = clone h; bad.unrecognized <- 1;
        configRejected <- !bb.configure(bad).ok;
        configPreserved <- checked(bb.settings()).cells.len() == 4;
        badRateRejected <- !wake.prepareStep(checked(checked(sea.frameBuilder(0,60)).build()),1, 0).ok;
        for (local tick = 0; tick < 120; ++tick) {
            local frame = checked(checked(sea.frameBuilder(tick,60)).build());
            local receipt = checked(boat.apply(frame,wake,60,1.0,0.0));
            if (tick == 0) {
                duplicateRejected <- !boat.apply(frame,wake,60,1.0,0.0).ok;
                wet <- checked(receipt.summary()).submergedVolume > 0.0;
            }
            checked(wake.injectReceipt(receipt,60));
            local pending = checked(wake.prepareStep(frame,tick+1,60));
            frame = null; collectgarbage();
            testStepWorld();
            checked(wake.commitStep(pending));
            if (wake.commitStep(pending).ok) throw "Consumed wake step accepted";
        }
        currentFrame <- checked(checked(sea.frameBuilder(120,60)).build());
        checked(wake.sampleSurface(currentFrame,0.0,0.0));
        local stale = checked(checked(sea.frameBuilder(119,60)).build());
        staleWaterRejected <- !wake.sampleSurface(stale,0.0,0.0).ok;
        foam <- checked(wake.sample(0.0,0.0)).foam;
        checked(wake.recenter(2.0,0.0));
        checked(boat.apply(currentFrame,null,60,0.0,0.0));
        nextFrame <- checked(checked(sea.frameBuilder(121,60)).build());
        collectgarbage();
    )"));
    CHECK(vm.find("configRejected").toBool());
    CHECK(vm.find("configPreserved").toBool());
    CHECK(vm.find("badRateRejected").toBool());
    CHECK(vm.find("duplicateRejected").toBool());
    CHECK(vm.find("wet").toBool());
    CHECK(vm.find("staleWaterRejected").toBool());
    CHECK(vm.find("foam").toFloat() > 0.0);
    CHECK(body->getZ() > 0.05F);
    CHECK(std::abs(body->getY()) < 0.5F);
    world.reset();
    vm.run(vm.compileSource(R"(
        local failure = boat.apply(nextFrame,null,60,1.0,0.0);
        staleBodyRejected <- !failure.ok && failure.diagnostics[0].code == "stale_handle";
    )"));
    CHECK(vm.find("staleBodyRejected").toBool());
}
