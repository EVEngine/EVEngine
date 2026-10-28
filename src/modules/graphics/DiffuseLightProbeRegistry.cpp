#include "graphics/DiffuseLightProbeRegistry.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace eve::graphics {

namespace {

constexpr float kGridEpsilon = 1e-4f;

bool sameCoordinate(float a, float b) { return std::abs(a - b) <= kGridEpsilon; }

void appendCoordinate(std::vector<float>& values, float value) {
    if (std::none_of(values.begin(), values.end(), [value](float existing) {
            return sameCoordinate(existing, value);
        }))
        values.push_back(value);
}

void writeProbe(DiffuseLightProbeVolumeSample& volume, int index, const eve::ProcgenProbeDesc& probe) {
    volume.positions[static_cast<size_t>(index)] = glm::vec4(probe.x, probe.y, probe.z, 1.f);
    volume.extents[static_cast<size_t>(index)] = glm::vec4(probe.extentX, probe.extentY, probe.extentZ, 0.f);
    if (probe.hasSphericalHarmonics) {
        for (int coefficient = 0; coefficient < 9; ++coefficient) {
            const size_t source = static_cast<size_t>(coefficient * 3);
            volume.coefficients[static_cast<size_t>(index * 9 + coefficient)] = glm::vec4(
                probe.sphericalHarmonics[source], probe.sphericalHarmonics[source + 1],
                probe.sphericalHarmonics[source + 2], 0.f);
        }
    } else {
        constexpr float kY00 = 0.2820947918f;
        volume.coefficients[static_cast<size_t>(index * 9)] = glm::vec4(
            probe.irradianceR / kY00, probe.irradianceG / kY00, probe.irradianceB / kY00, 0.f);
    }
}

bool bracketAxis(const std::vector<float>& sorted, float position, float singletonExtent,
                 std::array<float, 2>& bounds, int& count) {
    if (sorted.empty()) return false;
    if (sorted.size() == 1) {
        if (std::abs(position - sorted.front()) > singletonExtent + kGridEpsilon) return false;
        bounds[0] = sorted.front();
        count = 1;
        return true;
    }
    auto upper = std::lower_bound(sorted.begin(), sorted.end(), position - kGridEpsilon);
    if (upper == sorted.end()) return false;
    if (sameCoordinate(*upper, position)) {
        bounds[0] = *upper;
        count = 1;
        return true;
    }
    if (upper == sorted.begin()) return false;
    bounds[0] = *(upper - 1);
    bounds[1] = *upper;
    count = 2;
    return true;
}

}  // namespace

DiffuseLightProbeRegistry& DiffuseLightProbeRegistry::instance() {
    static DiffuseLightProbeRegistry registry;
    return registry;
}

void DiffuseLightProbeRegistry::replaceBatch(const std::string& batchId,
                                             const std::vector<eve::ProcgenProbeDesc>& probes) {
    batches_.insert_or_assign(batchId, probes);
}

void DiffuseLightProbeRegistry::removeBatch(const std::string& batchId) { batches_.erase(batchId); }

Result<DiffuseLightProbeSample> DiffuseLightProbeRegistry::sample(const glm::vec3& position, int maxSamples) const {
    struct Candidate {
        float weight;
        DiffuseLightProbeSample sample;
    };
    std::vector<Candidate> candidates;
    for (const auto& [_, probes] : batches_) {
        for (const auto& probe : probes) {
            const glm::vec3 extent(probe.extentX, probe.extentY, probe.extentZ);
            const glm::vec3 delta = glm::abs(position - glm::vec3(probe.x, probe.y, probe.z));
            if (delta.x > extent.x || delta.y > extent.y || delta.z > extent.z) continue;
            const float normalized = glm::length(delta / extent);
            Candidate candidate{1.f / std::max(0.01f, normalized), {}};
            if (probe.hasSphericalHarmonics) {
                for (size_t coefficient = 0; coefficient < 9; ++coefficient) {
                    const size_t base = coefficient * 3;
                    candidate.sample.coefficients[coefficient] = glm::vec4(
                        probe.sphericalHarmonics[base], probe.sphericalHarmonics[base + 1],
                        probe.sphericalHarmonics[base + 2], 0.f);
                }
            } else {
                constexpr float kY00 = 0.2820947918f;
                candidate.sample.coefficients[0] = glm::vec4(
                    probe.irradianceR / kY00, probe.irradianceG / kY00, probe.irradianceB / kY00, 0.f);
            }
            candidates.push_back(candidate);
        }
    }
    if (candidates.empty())
        return Result<DiffuseLightProbeSample>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "no diffuse light probe influences the sample position", "graphics.lightProbe"));
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.weight > b.weight;
    });
    const size_t count = std::min(candidates.size(), static_cast<size_t>(std::max(1, maxSamples)));
    DiffuseLightProbeSample blended;
    float weightSum = 0.f;
    for (size_t i = 0; i < count; ++i) {
        for (size_t coefficient = 0; coefficient < blended.coefficients.size(); ++coefficient)
            blended.coefficients[coefficient] +=
                candidates[i].sample.coefficients[coefficient] * candidates[i].weight;
        weightSum += candidates[i].weight;
    }
    for (auto& coefficient : blended.coefficients) coefficient /= weightSum;
    return Result<DiffuseLightProbeSample>::success(blended);
}

Result<DiffuseLightProbeVolumeSample> DiffuseLightProbeRegistry::selectVolume(const glm::vec3& position) const {
    struct GridCandidate {
        float distanceSquared = 0.f;
        DiffuseLightProbeVolumeSample volume;
    };
    std::vector<GridCandidate> grids;
    for (const auto& [_, probes] : batches_) {
        if (probes.empty()) continue;
        std::array<std::vector<float>, 3> axes;
        glm::vec3 singletonExtent(0.f);
        for (const auto& probe : probes) {
            appendCoordinate(axes[0], probe.x);
            appendCoordinate(axes[1], probe.y);
            appendCoordinate(axes[2], probe.z);
            singletonExtent = glm::max(singletonExtent, glm::vec3(probe.extentX, probe.extentY, probe.extentZ));
        }
        for (auto& axis : axes) std::sort(axis.begin(), axis.end());
        std::array<std::array<float, 2>, 3> bounds{};
        std::array<int, 3> axisCounts{};
        bool bracketed = true;
        for (int axis = 0; axis < 3; ++axis)
            bracketed = bracketed && bracketAxis(axes[axis], position[axis], singletonExtent[axis],
                                                 bounds[axis], axisCounts[axis]);
        if (!bracketed) continue;

        DiffuseLightProbeVolumeSample cell;
        bool complete = true;
        for (int z = 0; z < axisCounts[2] && complete; ++z)
            for (int y = 0; y < axisCounts[1] && complete; ++y)
                for (int x = 0; x < axisCounts[0] && complete; ++x) {
                    const auto found = std::find_if(probes.begin(), probes.end(), [&](const auto& probe) {
                        return sameCoordinate(probe.x, bounds[0][x]) &&
                               sameCoordinate(probe.y, bounds[1][y]) &&
                               sameCoordinate(probe.z, bounds[2][z]);
                    });
                    if (found == probes.end()) {
                        complete = false;
                        break;
                    }
                    writeProbe(cell, cell.count++, *found);
                }
        if (!complete || cell.count < 2) continue;
        cell.trilinearCell = true;
        glm::vec3 center;
        for (int axis = 0; axis < 3; ++axis)
            center[axis] = axisCounts[axis] == 2 ? (bounds[axis][0] + bounds[axis][1]) * 0.5f : bounds[axis][0];
        grids.push_back({glm::dot(position - center, position - center), cell});
    }
    if (!grids.empty()) {
        const auto selected = std::min_element(grids.begin(), grids.end(), [](const auto& a, const auto& b) {
            return a.distanceSquared < b.distanceSquared;
        });
        return Result<DiffuseLightProbeVolumeSample>::success(selected->volume);
    }

    struct Candidate {
        float distanceSquared;
        const eve::ProcgenProbeDesc* probe;
    };
    std::vector<Candidate> candidates;
    for (const auto& [_, probes] : batches_) {
        for (const auto& probe : probes) {
            const glm::vec3 extent(probe.extentX, probe.extentY, probe.extentZ);
            const glm::vec3 probePosition(probe.x, probe.y, probe.z);
            const glm::vec3 delta = glm::abs(position - probePosition);
            if (delta.x <= extent.x && delta.y <= extent.y && delta.z <= extent.z)
                candidates.push_back({glm::dot(delta, delta), &probe});
        }
    }
    if (candidates.empty())
        return Result<DiffuseLightProbeVolumeSample>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "no diffuse light probe volume contains the object", "graphics.lightProbe"));
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.distanceSquared < b.distanceSquared;
    });
    DiffuseLightProbeVolumeSample volume;
    volume.count = static_cast<int>(std::min(candidates.size(), static_cast<size_t>(volume.kMaxProbes)));
    for (int index = 0; index < volume.count; ++index) {
        writeProbe(volume, index, *candidates[static_cast<size_t>(index)].probe);
    }
    return Result<DiffuseLightProbeVolumeSample>::success(volume);
}

}  // namespace eve::graphics
