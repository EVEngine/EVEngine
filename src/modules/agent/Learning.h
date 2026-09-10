#pragma once

#include "agent/Agent.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace eve::agent::detail {

// SplitMix64: local streams, with specified integer-to-double conversion.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    double unit() {
        auto z = (state_ += 0x9e3779b97f4a7c15ULL);
        z      = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z      = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return double((z ^ (z >> 31)) >> 11) * 0x1.0p-53;
    }
    std::size_t index(std::size_t count) { return std::size_t(unit() * double(count)); }

private:
    std::uint64_t state_;
};

inline std::size_t weightCount(std::size_t inputs, std::size_t hidden, std::size_t actions) {
    return hidden * (inputs + 1) + hidden * (hidden + 1) + actions * (hidden + 1);
}

inline std::vector<double> layer(const std::vector<double>& x, const Policy& p, std::size_t offset, std::size_t outputs,
                                 bool activate) {
    std::vector<double> y(outputs);
    for (std::size_t j = 0; j < outputs; ++j) {
        const auto start = offset + j * (x.size() + 1);
        double     sum   = p.weights[start + x.size()];
        for (std::size_t i = 0; i < x.size(); ++i) sum += x[i] * p.weights[start + i];
        y[j] = activate ? std::tanh(sum) : sum;
    }
    return y;
}

inline std::vector<double> softmax(std::vector<double> logits, const Observation& o) {
    double maximum = -std::numeric_limits<double>::infinity();
    for (auto a : o.legalActions) maximum = std::max(maximum, logits[a]);
    std::vector<double> probabilities(logits.size());
    double              total = 0;
    for (auto a : o.legalActions) total += probabilities[a] = std::exp(logits[a] - maximum);
    for (auto a : o.legalActions) probabilities[a] /= total;
    return probabilities;
}

inline std::vector<double> forward(const Policy& p, const Observation& o) {
    std::vector<double> x(o.features.begin(), o.features.end());
    auto                h1     = layer(x, p, 0, p.hiddenWidth, true);
    const auto          second = p.hiddenWidth * (p.featureCount + 1);
    auto                h2     = layer(h1, p, second, p.hiddenWidth, true);
    const auto          third  = second + p.hiddenWidth * (p.hiddenWidth + 1);
    return softmax(layer(h2, p, third, p.actionCount, false), o);
}

inline Policy makePolicy(const Config& c) {
    Policy p;
    p.featureCount = c.featureCount;
    p.actionCount  = c.actionCount;
    p.hiddenWidth  = c.hiddenWidth;
    p.weights.resize(weightCount(c.featureCount, c.hiddenWidth, c.actionCount));
    Random random(c.learningSeed);
    for (auto& w : p.weights) w = (random.unit() * 2 - 1) / std::sqrt(double(c.hiddenWidth));
    return p;
}

inline void train(Policy& p, const Observation& o, std::uint32_t action, double rate) {
    std::vector<double> x(o.features.begin(), o.features.end());
    auto                h1     = layer(x, p, 0, p.hiddenWidth, true);
    const auto          second = p.hiddenWidth * (p.featureCount + 1);
    auto                h2     = layer(h1, p, second, p.hiddenWidth, true);
    const auto          third  = second + p.hiddenWidth * (p.hiddenWidth + 1);
    auto                d3     = softmax(layer(h2, p, third, p.actionCount, false), o);
    d3[action] -= 1;
    std::vector<double> d2(p.hiddenWidth), d1(p.hiddenWidth);
    for (std::size_t i = 0; i < p.hiddenWidth; ++i) {
        for (std::size_t j = 0; j < p.actionCount; ++j) d2[i] += d3[j] * p.weights[third + j * (p.hiddenWidth + 1) + i];
        d2[i] *= 1 - h2[i] * h2[i];
    }
    for (std::size_t i = 0; i < p.hiddenWidth; ++i) {
        for (std::size_t j = 0; j < p.hiddenWidth; ++j)
            d1[i] += d2[j] * p.weights[second + j * (p.hiddenWidth + 1) + i];
        d1[i] *= 1 - h1[i] * h1[i];
    }
    auto update = [&](std::size_t offset, const auto& inputs, const auto& gradient) {
        for (std::size_t j = 0; j < gradient.size(); ++j) {
            auto start = offset + j * (inputs.size() + 1);
            for (std::size_t i = 0; i < inputs.size(); ++i)
                p.weights[start + i] -= rate * std::clamp(gradient[j] * inputs[i], -1.0, 1.0);
            p.weights[start + inputs.size()] -= rate * std::clamp(gradient[j], -1.0, 1.0);
        }
    };
    update(third, h2, d3);
    update(second, h1, d2);
    update(0, x, d1);
}

}  // namespace eve::agent::detail
