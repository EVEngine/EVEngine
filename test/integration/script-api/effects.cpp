#include "effects/Effects.h"
#include <cmath>
#include <cstdint>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// effects.cpp
using namespace eve::effects;


TEST_CASE("effects.script.containerLifecycle") {
    ssq::VM vm(1024, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        result <- "fail";
        local module = eve.Effects();
        local effectsResult = module.newContainer();
        local effects = effectsResult.ok ? effectsResult.value : null;
        local idResult = { ok = false, value = "" };
        if (effects != null) {
            idResult = effects.apply("base:4", "policy.production", "leader:8", 5, 1.0,
                                     "production-policy", "refresh");
        }
        local id = idResult.ok ? idResult.value : "";
        local effect = effects != null && idResult.ok ? effects.find(id) : null;
        local tagResult = { ok = false };
        local payloadResult = { ok = false };
        if (effect != null) {
            tagResult = effect.addTag("economic");
            payloadResult = effect.getPayload().setJson("rate", "1.25");
        }
        local sameResult = { ok = false, value = "" };
        if (effects != null) {
            sameResult = effects.apply("base:4", "ignored", "ignored", 7, 2.0,
                                       "production-policy", "refresh");
        }
        local same = sameResult.ok ? sameResult.value : "";
        if (effectsResult.ok && idResult.ok && tagResult.ok && payloadResult.ok && sameResult.ok && same == id &&
            effect != null && effect.getType() == "policy.production" &&
            effect.getSource() == "leader:8" && effect.hasTag("economic") &&
            effect.getPayload().getJson("rate") == "1.25" &&
            effects.eventAt(1).getKind() == "refreshed") {
            result = "ok";
        }
    )"));
    CHECK_EQ(vm.find("result").toString(), std::string("ok"));
}

// effects_lifecycle.cpp
using namespace eve::effects;
