#include "tensor/GpuBackend.h"
#include "agent/Agent.h"
#include "agent/tensor/GpuGraph.h"
#include "agent/tensor/TensorBackend.h"
#include "gpgpu/Gpgpu.h"
#include "tensor/Optimizer.h"

#include <cmath>
#include <exception>
#include <limits>

namespace eve::agent {
namespace {
template <class T>
Result<T> gpuError(const std::string& message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::Unsupported, message, {}, {}, "agent.gpu"));
}
class GpuBackend final : public IGpuPolicyBackend {
public:
    std::string  name() const override { return "tensor-gpu"; }
    Result<void> available() const override {
        try {
            auto* device = gpgpu::Gpgpu::create();
            if (device && device->isAvailable()) return Result<void>::success();
            return gpuError<void>("GPU requires an initialized compute-capable Graphics device");
        } catch (const std::exception& e) {
            return gpuError<void>(e.what());
        }
    }
    Result<std::vector<double>> evaluate(const Policy& p, const Observation& o) override {
        return execute(p, o, 0, 0, false);
    }
    Result<Policy> train(const Policy& p, const Observation& o, std::uint32_t action, double rate) override {
        auto result = execute(p, o, action, rate, true);
        if (!result) return Result<Policy>::failure(result.status());
        Policy next    = p;
        next.weights   = std::move(result).takeValue();
        auto validated = validatePolicy(next);
        if (!validated) return Result<Policy>::failure(validated.status());
        return Result<Policy>::success(std::move(next));
    }

private:
    Result<std::vector<double>> execute(const Policy& p, const Observation& o, std::uint32_t action, double rate,
                                        bool training) {
        auto ready = available();
        if (!ready) return Result<std::vector<double>>::failure(ready.status());
        try {
            // Cache only the two shapes, never caller-owned weights or observation pointers.
            if (f_ != p.featureCount || h_ != p.hiddenWidth || a_ != p.actionCount) {
                inference_.reset();
                training_.reset();
                f_ = p.featureCount;
                h_ = p.hiddenWidth;
                a_ = p.actionCount;
            }
            auto& program = training ? training_ : inference_;
            if (!program) {
                auto recipe    = detail::makePolicyGraph(int(f_), int(h_), int(a_), training);
                auto optimized = tensor::optimizeGraph(recipe.graph, recipe.output);
                program.reset(tensor::GpuProgram::tryBuild(recipe.graph, optimized, recipe.output));
                if (!program)
                    return gpuError<std::vector<double>>("Policy graph cannot execute on this GPU; no CPU fallback");
            }
            std::vector<float> weights(p.weights.begin(), p.weights.end());
            std::vector<float> mask(a_, -std::numeric_limits<float>::infinity()), target(a_, 0);
            for (auto legal : o.legalActions) mask[legal] = 0;
            std::vector<const float*> feeds{weights.data(), o.features.data(), mask.data()};
            float                     learningRate = float(rate);
            if (training) {
                target[action] = 1;
                feeds.push_back(target.data());
                feeds.push_back(&learningRate);
            }
            auto output = program->run(feeds);
            if (output.size() != (training ? p.weights.size() : p.actionCount))
                return gpuError<std::vector<double>>("GPU returned an invalid output shape");
            for (auto value : output)
                if (!std::isfinite(value)) return gpuError<std::vector<double>>("GPU returned non-finite values");
            return Result<std::vector<double>>::success({output.begin(), output.end()});
        } catch (const std::exception& e) {
            inference_.reset();
            training_.reset();
            return gpuError<std::vector<double>>(e.what());
        }
    }
    std::uint32_t                       f_ = 0, h_ = 0, a_ = 0;
    std::unique_ptr<tensor::GpuProgram> inference_, training_;
};
}  // namespace
Result<std::unique_ptr<IGpuPolicyBackend>> makeGpuBackend() {
    return Result<std::unique_ptr<IGpuPolicyBackend>>::success(std::make_unique<GpuBackend>());
}
}  // namespace eve::agent
