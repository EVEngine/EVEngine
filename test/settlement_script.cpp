#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "settlement/SettlementModule.h"
#include "settlement/SettlementRules.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <simplesquirrel/simplesquirrel.hpp>

TEST_CASE("settlementScript.runtimeOwnsRulesAndLedger") {
    ssq::VM vm(1024, ssq::Libs::STRING | ssq::Libs::MATH);
    auto    eve = vm.addTable("eve");
    eve::script::exposeResultBindings(eve);
    eve::settlement::Settlement::expose(eve);

    // Build a minimal settlement.rules document in C++ then feed it to script.
    eve::settlement::SettlementRuleSet rules;
    eve::settlement::SettlementRule    multiply;
    multiply.id        = "buff.attack";
    multiply.source    = "effect:test";
    multiply.stage     = eve::settlement::StageKind::SourceModifiers;
    multiply.operation = eve::settlement::RuleOperation::Multiply;
    multiply.value     = 1.5;
    multiply.filter.kinds = {"damage"};
    REQUIRE(rules.configure({multiply}).ok());
    auto rulesJson = rules.canonicalJson();
    REQUIRE(rulesJson.ok());

    const std::string rulesLiteral = rulesJson.value();
    eve.addFunc("rulesJson", [rulesLiteral]() { return rulesLiteral; });
    vm.run(vm.compileSource(R"(
        settlement <- eve.Settlement();
        created <- settlement.newRuntime();
        runtime <- created.value;
        configured <- runtime.configureRulesJson(eve.rulesJson());
        validated <- runtime.validateRulesJson(eve.rulesJson());
        source <- "01020304-0506-0708-890a-0b0c0d0e0f10";
        target <- "11121314-1516-1718-991a-1b1c1d1e1f20";
        upserted <- runtime.upsertResource(target, "hp", 100.0, 100.0);
        settled <- runtime.settle(source, target, "damage", "hp", 20.0, "", "{}", 1);
        after <- runtime.getResource(target, "hp");
        missing <- runtime.settle(source, "22222222-2222-2222-2222-222222222222", "damage", "hp", 1.0, "", "{}", 2);
        digest <- runtime.rulesDigest();
        applied <- settled.value.applied;
        current <- after.value.current;
        disposition <- settled.value.disposition;
        ruleCount <- runtime.ruleCount();
        ownership <- runtime.ownership();
    )"));
    CHECK(vm.find("created").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("created").toTable().get<std::string>("ownership"), std::string("owned"));
    CHECK_EQ(vm.find("ownership").toString(), std::string("owned"));
    CHECK(vm.find("configured").toTable().get<bool>("ok"));
    CHECK(vm.find("validated").toTable().get<bool>("ok"));
    CHECK(vm.find("upserted").toTable().get<bool>("ok"));
    CHECK(vm.find("settled").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("applied").toFloat(), 30.0f);  // 20 * 1.5
    CHECK_EQ(vm.find("current").toFloat(), 70.0f);
    CHECK_EQ(vm.find("disposition").toString(), std::string("applied"));
    CHECK(!vm.find("missing").toTable().get<bool>("ok"));
    CHECK(vm.find("digest").toTable().get<bool>("ok"));
    CHECK_EQ(vm.find("ruleCount").toInt(), 1);
}
