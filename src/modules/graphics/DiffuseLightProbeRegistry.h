#pragma once

#include "common/ProcgenProbeSink.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

struct DiffuseLightProbeSample {
    std::array<glm::vec4, 9> coefficients{};
};

struct DiffuseLightProbeVolumeSample {
    static constexpr int kMaxProbes = 8;
    std::array<glm::vec4, kMaxProbes> positions{};
    std::array<glm::vec4, kMaxProbes> extents{};
    std::array<glm::vec4, kMaxProbes * 9> coefficients{};
    int count = 0;
    bool trilinearCell = false;
};

/** @brief Owns generated diffuse probes and samples their baked L0 irradiance. */
class DiffuseLightProbeRegistry {
public:
    /** @brief Return the process-wide graphics-owned registry. */
    static DiffuseLightProbeRegistry& instance();

    /** @brief Atomically replace the diffuse probes owned by one generated batch. */
    void replaceBatch(const std::string& batchId, const std::vector<eve::ProcgenProbeDesc>& probes);
    /** @brief Remove every diffuse probe owned by one generated batch. */
    void removeBatch(const std::string& batchId);
    /** @brief Blend up to maxSamples probes whose influence volumes contain position. */
    [[nodiscard]] Result<DiffuseLightProbeSample> sample(const glm::vec3& position, int maxSamples) const;
    /** @brief Select an object-local proxy volume; fragment shading performs the spatial interpolation. */
    [[nodiscard]] Result<DiffuseLightProbeVolumeSample> selectVolume(const glm::vec3& position) const;

private:
    std::unordered_map<std::string, std::vector<eve::ProcgenProbeDesc>> batches_;
};

}  // namespace eve::graphics
