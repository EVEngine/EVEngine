// CPU example host: real RPG registries, Battle, SkillSystem, equipment projections and settlement.
// Only definition registration is exposed here; the game renderer is not needed for offline balancing.
#include "agent/Balance.h"
#include "common/SquirrelBinding.h"
#include "rpg/BattleSystem.h"
#include "rpg/Class.h"
#include "rpg/Effect.h"
#include "rpg/Skill.h"
#include "rpg/Trait.h"
#include "rpg/balance/RPGBalance.h"
#include "rpg/balance/RPGBalanceModule.h"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string_view>

int main(int argc, char** argv) {
    try {
        const bool probe = argc > 1 && std::string_view(argv[1]) == "--probe";
        if (probe && argc != 7) throw std::invalid_argument("--probe requires output path and four numeric parameters");
        const auto outputPath =
            argc > (probe ? 2 : 1) ? std::filesystem::absolute(argv[probe ? 2 : 1]) : std::filesystem::path{};
        std::array<double, 4> probeValues{};
        if (probe)
            for (int i = 0; i < 4; ++i) {
                probeValues[std::size_t(i)] = std::stod(argv[i + 3]);
                if (!std::isfinite(probeValues[std::size_t(i)]))
                    throw std::invalid_argument("Nonfinite probe parameter");
            }
        std::filesystem::current_path(EVE_RPG_BALANCE_EXAMPLE);
        ssq::VM vm(16384, ssq::Libs::ALL);
        auto    eve = vm.addTable("eve");
        eve::agent::balance::BalanceSearch::expose(eve);
        eve::rpg::balance::RPGBalance::expose(eve);
        // Bootstrap forwards to the same production registry loaders as eve.RPG(); no combat implementation here.
        auto definitions = vm.addTable("productionRpgDefinitions");
        definitions.addFunc("registerSkillsFromJson",
                            [](std::string json) { return eve::rpg::SkillRegistry::loadFromJson(json); });
        definitions.addFunc("registerClassesFromJson",
                            [](std::string json) { return eve::rpg::ClassRegistry::loadFromJson(json); });
        definitions.addFunc("registerEffectsFromJson",
                            [](std::string json) { return eve::rpg::EffectRegistry::loadFromJson(json); });
        definitions.addFunc("registerTraitsFromJson",
                            [](std::string json) { return eve::rpg::TraitRegistry::loadFromJson(json); });
        definitions.addFunc("registerSkillDamage", [](std::string id, std::string type, std::string formula,
                                                      std::string element, float critical, int hit) {
            eve::rpg::SkillDamageSpec spec{type, formula, element, double(critical), hit};
            auto                      result = eve::rpg::BattleSystem::registerSkillDamageChecked(id, spec);
            return result.ok() ? 1 : 0;
        });
        eve.addFunc("RPG", [&vm] { return vm.find("productionRpgDefinitions"); });
        if (probe) {
            eve.addFunc("probeParameter",
                        [probeValues](int index) { return float(probeValues.at(std::size_t(index))); });
            vm.run(vm.compileSource(
                "dofile(\"main.nut\"); balanceRegisterDefinitions();"
                "local suite=eve.RPGBalanceSuite(); local configured=suite.configure(balanceSuiteSpec());"
                "if(!configured.ok) throw configured.diagnostics[0].message;"
                "local p={}; p[\"warrior.power\"]<-eve.probeParameter(0);"
                "p[\"mage.power\"]<-eve.probeParameter(1);"
                "p[\"ranger.power\"]<-eve.probeParameter(2);"
                "p[\"mage.weapon\"]<-eve.probeParameter(3);"
                                    "local search=balanceSearchSpec(balanceValue(suite.targets()));"
                                    "probeEvidence<-balanceValue(suite.measure(p,search.validationSeeds,search.dt,search.steps));"));
        } else {
            vm.run(vm.compileSource("dofile(\"main.nut\"); eve_init(); balanceVerify();"));
        }
        if (!outputPath.empty()) {
            // Writing a report is explicit; the example never writes game definitions.
            auto value = eve::script::valueFromSquirrel(vm.find(probe ? "probeEvidence" : "balanceEvidence"));
            if (!value) throw std::runtime_error(value.status().describe());
            auto json = value.value().toJson();
            if (!json) throw std::runtime_error(json.status().describe());
            std::ofstream output(outputPath, std::ios::binary);
            if (!output || !(output << json.value())) throw std::runtime_error("Cannot write report");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RPG balance example failed: " << error.what() << '\n';
        return 1;
    }
}
