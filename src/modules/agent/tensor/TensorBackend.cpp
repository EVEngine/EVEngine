#include "agent/tensor/TensorBackend.h"
#include "agent/Agent.h"
#include "tensor/CpuKernels.h"
#include "tensor/Tensor.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::agent {
namespace {
class TensorBackend final : public IPolicyBackend {
public:
    std::string                 name() const override { return "tensor-cpu"; }
    Result<std::vector<double>> evaluate(const Policy& p, const Observation& o) override {
        // The public dispatcher validates shape, finiteness and legal masks first.
        tensor::Tensor x(1, int(p.featureCount));
        for (std::size_t i = 0; i < o.features.size(); ++i) x.set(int(i), o.features[i]);
        std::size_t offset = 0;
        auto        layer  = [&](const tensor::Tensor& input, std::uint32_t outputs, bool activate) {
            const int      inputs = input.getDim1();
            tensor::Tensor weights(inputs, int(outputs));
            for (std::uint32_t j = 0; j < outputs; ++j)
                for (int i = 0; i < inputs; ++i)
                    weights.set2(i, int(j), float(p.weights[offset + j * (inputs + 1) + i]));
            std::unique_ptr<tensor::Tensor> out(input.matmul(&weights));
            for (std::uint32_t j = 0; j < outputs; ++j)
                out->set(int(j), out->get(int(j)) + float(p.weights[offset + j * (inputs + 1) + inputs]));
            offset += outputs * (inputs + 1);
            if (activate) {
                std::unique_ptr<tensor::Tensor> activated(out->tanh());
                return activated;
            }
            return out;
        };
        auto               h1     = layer(x, p.hiddenWidth, true);
        auto               h2     = layer(*h1, p.hiddenWidth, true);
        auto               logits = layer(*h2, p.actionCount, false);
        std::vector<float> masked(p.actionCount, -std::numeric_limits<float>::infinity()), probabilities(p.actionCount);
        for (auto action : o.legalActions) masked[action] = logits->get(int(action));
        const int dims[] = {1, int(p.actionCount)};
        tensor::kernels::softmax(masked.data(), dims, 2, 1, false, probabilities.data());
        return Result<std::vector<double>>::success({probabilities.begin(), probabilities.end()});
    }
};
}  // namespace
Result<std::unique_ptr<IPolicyBackend>> makeTensorBackend() {
    return Result<std::unique_ptr<IPolicyBackend>>::success(std::make_unique<TensorBackend>());
}
}  // namespace eve::agent
