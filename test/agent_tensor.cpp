#include <cmath>
#include "agent/Agent.h"
#include "agent/tensor/TensorBackend.h"
#include "common/Capability.h"
#include "zeroerr/unittest.h"

TEST_CASE("agent.tensorParityAndProviderLifetime") {
    using namespace eve::agent;
    Policy p;
    p.featureCount = 2;
    p.actionCount  = 3;
    p.hiddenWidth  = 2;
    p.weights.resize(21);
    for (std::size_t i = 0; i < p.weights.size(); ++i) p.weights[i] = std::sin(double(i)) * 0.5;
    Observation o;
    o.features     = {0.4f, -0.2f};
    o.legalActions = {0, 2};
    auto absent    = infer(p, o, Backend::Tensor);
    REQUIRE(!absent.ok());
    REQUIRE(absent.status().code() == eve::StatusCode::Unsupported);
    auto factory = makeTensorBackend();
    REQUIRE(factory.ok());
    auto provider = std::move(factory).takeValue();
    struct Registration {
        IPolicyBackend& backend;
        explicit Registration(IPolicyBackend& b) : backend(b) { eve::cap::provide<IPolicyBackend>(&b); }
        ~Registration() { eve::cap::revoke<IPolicyBackend>(&backend); }
    };
    {
        Registration registration(*provider);
        for (int sample = 0; sample < 32; ++sample) {
            o.features  = {float(std::sin(sample)), float(std::cos(sample))};
            auto cpu    = infer(p, o);
            auto tensor = infer(p, o, Backend::Tensor);
            REQUIRE(cpu.ok());
            REQUIRE(tensor.ok());
            for (std::size_t i = 0; i < cpu.value().size(); ++i)
                REQUIRE(std::abs(cpu.value()[i] - tensor.value()[i]) < 1e-5);
            REQUIRE_EQ(tensor.value()[1], 0.0);
        }
    }
    REQUIRE(!backendName(Backend::Tensor).ok());
}
