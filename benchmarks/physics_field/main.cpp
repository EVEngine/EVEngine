#include "physics/field/GravityField.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace eve::physics::field;
using Clock = std::chrono::steady_clock;

double milliseconds(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

// Specified integer stream and conversion: independent of stdlib distributions.
double unit(std::uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    auto z = state;
    z      = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z      = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<double>((z ^ (z >> 31)) >> 11) * 0x1p-53;
}

struct ReferenceReport {
    std::size_t samples              = 0;
    double      maxAccelerationError = 0, maxPotentialError = 0, maxExcess = 0;
    bool        passed = true;
};

ReferenceReport compareReference(std::span<const Particle> sources, std::span<const FieldTarget> targets,
                                 std::span<const GravitySample> samples, bool independent, double errorBudget) {
    ReferenceReport report;
    report.samples = std::min<std::size_t>(64, samples.size());
    for (std::size_t k = 0; k < report.samples; ++k) {
        const auto target   = report.samples == 1 ? 0 : k * (samples.size() - 1) / (report.samples - 1);
        const auto position = independent ? targets[target].position : sources[target].position;
        const auto excluded = independent ? targets[target].excludedSource : std::optional<std::size_t>{target};
        std::array<double, 4> sum{}, correction{};
        for (std::size_t source = 0; source < sources.size(); ++source) {
            if (excluded == source) continue;
            std::array<double, 3> d{};
            for (unsigned axis = 0; axis < 3; ++axis) d[axis] = sources[source].position[axis] - position[axis];
            const double                r         = std::hypot(d[0], d[1], d[2]);
            const double                potential = -sources[source].mass / r;
            const double                strength  = -potential / r;
            const std::array<double, 4> terms{(d[0] / r) * strength, (d[1] / r) * strength, (d[2] / r) * strength,
                                              potential};
            for (unsigned component = 0; component < 4; ++component) {
                const double value    = terms[component] - correction[component];
                const double next     = sum[component] + value;
                correction[component] = (next - sum[component]) - value;
                sum[component]        = next;
            }
        }
        const auto&  sample         = samples[target];
        const double error          = std::hypot(sample.acceleration[0] - sum[0], sample.acceleration[1] - sum[1],
                                                 sample.acceleration[2] - sum[2]);
        const double potentialError = std::abs(sample.potential - sum[3]);
        const double allowance      = 1e-10 * std::max(1.0, std::hypot(sum[0], sum[1], sum[2]));
        const double excess         = error - sample.truncationErrorBound - allowance;
        report.passed               = report.passed && std::isfinite(error) && std::isfinite(potentialError) &&
                        std::isfinite(excess) && excess <= 0 && sample.truncationErrorBound >= 0 &&
                        sample.truncationErrorBound <= errorBudget;
        report.maxAccelerationError = std::max(report.maxAccelerationError, error);
        report.maxPotentialError    = std::max(report.maxPotentialError, potentialError);
        report.maxExcess            = std::max(report.maxExcess, excess);
    }
    return report;
}
}  // namespace

int main(int argc, char** argv) {
    std::size_t count = 10000, repeats = 3, order = 4, leafCapacity = 16, targetCount = 0;
    const bool  independent = argc > 6 && std::string_view(argv[6]) != "-";
    const bool  verify      = argc > 7 && std::string_view(argv[7]) == "verify";
    const auto  parse       = [](std::string_view text, std::size_t& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    if (argc > 8 || (argc > 7 && !verify) || (independent && (!parse(argv[6], targetCount) || targetCount > 1000000)) ||
        (argc > 1 && !parse(argv[1], count)) || (argc > 2 && !parse(argv[2], repeats)) ||
        (argc > 4 && !parse(argv[4], order)) || (argc > 5 && !parse(argv[5], leafCapacity)) || order < 2 || order > 6 ||
        leafCapacity == 0 || leafCapacity > 1024 || count == 0 || count > 1000000 || repeats == 0 || repeats > 1000) {
        std::cerr << "usage: field_benchmark [1..1000000 sources] [1..1000 repeats] [uniform|clusters|sheet] [2..6 "
                     "order] [1..1024 leaf capacity] [0..1000000 targets|-] [verify]\n";
        return 2;
    }
    const std::string_view distribution = argc > 3 ? argv[3] : "uniform";
    if (distribution != "uniform" && distribution != "clusters" && distribution != "sheet") return 2;
    std::uint64_t         seed = 20261026;
    std::vector<Particle> points(count);
    const auto            generatePosition = [&](std::size_t i, std::uint64_t& stream) {
        std::array<double, 3> position{};
        for (unsigned axis = 0; axis < 3; ++axis) {
            double value = unit(stream) * 2 - 1;
            if (distribution == "clusters") value = value * 0.02 + ((i >> axis) & 1 ? 0.7 : -0.7);
            if (distribution == "sheet" && axis == 2) value *= 0.001;
            position[axis] = value;
        }
        return position;
    };
    for (std::size_t i = 0; i < count; ++i) {
        points[i].position = generatePosition(i, seed);
        points[i].mass     = 1.0 / static_cast<double>(count);
    }
    std::vector<FieldTarget> targets(independent ? targetCount : 0);
    std::uint64_t            targetSeed = 20261027;
    for (std::size_t i = 0; i < targets.size(); ++i) targets[i].position = generatePosition(i, targetSeed);
    GravityPolicy policy;
    policy.absoluteAccelerationError = 1e-3;
    policy.expansionOrder            = static_cast<unsigned>(order);
    policy.leafCapacity              = leafCapacity;
    // These are explicit resource admission limits, not a relaxation of the accuracy contract.
    const auto rows        = count + targets.size();
    policy.maxParticles    = rows;
    policy.maxDirectPairs  = rows * std::size_t{1024};
    policy.maxInteractions = rows * std::size_t{512};
    std::cout << "scope,distribution,N,repeat,seed,order,leaf_capacity,absolute_error,stage_a_ms,stage_b_ms,total_ms,"
                 "nodes,partition_visits,traversal_pairs,far_interactions,direct_pairs,depth,max_bound,checksum";
    if (independent) std::cout << ",target_count,target_seed";
    std::cout << '\n';
    std::cout << std::setprecision(12);
    for (std::size_t repetition = 0; repetition < repeats; ++repetition) {
        const FieldStamp stamp{eve::SimulationTick{repetition}, eve::Revision{1}};
        const auto       start = Clock::now();
        auto             field = independent ? GravityField::buildAtTargets(points, targets, stamp, policy)
                                             : GravityField::build(points, stamp, policy);
        const auto       built = Clock::now();
        if (!field.ok()) {
            std::cerr << "stage A rejected after " << milliseconds(start, built) << " ms: " << field.status().describe()
                      << '\n';
            return 1;
        }
        auto       samples   = field.value().evaluate(stamp);
        const auto evaluated = Clock::now();
        if (!samples.ok()) {
            std::cerr << "stage B rejected: " << samples.status().describe() << '\n';
            return 1;
        }
        double checksum = 0, bound = 0;
        for (const auto& sample : samples.value()) {
            checksum += sample.potential;
            bound = std::max(bound, sample.truncationErrorBound);
        }
        const auto m = field.value().metrics();
        std::cout << (independent ? "field_only_independent," : "field_only,") << distribution << ',' << count << ','
                  << repetition << ",20261026," << policy.expansionOrder << ',' << policy.leafCapacity << ','
                  << policy.absoluteAccelerationError << ',' << milliseconds(start, built) << ','
                  << milliseconds(built, evaluated) << ',' << milliseconds(start, evaluated) << ',' << m.nodes << ','
                  << m.partitionVisits << ',' << m.traversalPairs << ',' << m.farInteractions << ',' << m.directPairs
                  << ',' << m.treeDepth << ',' << bound << ',' << checksum;
        if (independent) std::cout << ',' << m.targets << ",20261027";
        std::cout << std::endl;
        if (verify) {
            const auto referenceStart = Clock::now();
            const auto report =
                compareReference(points, targets, samples.value(), independent, policy.absoluteAccelerationError);
            const auto referenceEnd = Clock::now();
            std::cerr << std::setprecision(17) << "sampled_reference count=" << report.samples
                      << " max_acceleration_error=" << report.maxAccelerationError
                      << " max_potential_error=" << report.maxPotentialError << " max_excess=" << report.maxExcess
                      << " verification_ms=" << milliseconds(referenceStart, referenceEnd)
                      << " passed=" << report.passed << '\n';
            if (!report.passed) return 3;
        }
    }
    return 0;
}
