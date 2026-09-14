#include "Environments.h"

#include <iostream>

int main() {
    using namespace eve::agent;
    agent_examples::GridGame game;
    agent_examples::UiLayout ui;
    for (int domain = 0; domain < 2; ++domain) {
        for (const auto strategy : {Strategy::Random, Strategy::EvolutionLearning}) {
            Config config;
            config.featureCount       = domain == 0 ? 2 : 4;
            config.actionCount        = domain == 0 ? 4 : 5;
            config.strategy           = strategy;
            config.coverageWeight     = domain == 1 ? 1.0 : 0.0;
            IEnvironment& environment = domain == 0 ? static_cast<IEnvironment&>(game) : static_cast<IEnvironment&>(ui);
            auto          result      = run(config, environment);
            if (!result) {
                std::cerr << result.status().describe() << '\n';
                return 1;
            }
            const auto& report   = result.value();
            auto        replayed = replay(report.best, environment);
            if (!replayed) {
                std::cerr << replayed.status().describe() << '\n';
                return 1;
            }
            std::cout << (domain == 0 ? "grid-game" : "ui-layout") << " backend=" << report.backend
                      << " episodes=" << report.episodes << " steps=" << report.steps
                      << " coverage=" << report.coverage.size() << " failures=" << report.failures
                      << " bestScore=" << report.bestScore << " trainingSamples=" << report.trainingSamples
                      << " replay=PASS\n";
        }
    }
}
