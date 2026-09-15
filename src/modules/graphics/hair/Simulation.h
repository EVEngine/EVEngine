#pragma once

#include "common/Result.h"
#include "graphics/hair/StrandsDatas.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

namespace eve::graphics::hair {

/**
 * @brief Parameters for CPU guide-strand dynamics (UE groom sim lite analogue).
 *
 * Verlet integration + XPBD distance constraints along each guide. Roots are
 * pinned. No dependency on the physics module — a SoftBody3D bridge can be
 * added later via capability without upward includes.
 */
struct GuideSimParams {
    glm::vec3 gravity{0.f, -9.81f, 0.f};
    /** @brief Velocity retention per second (0 = freeze, 1 = no damping). */
    float damping = 0.95f;
    /** @brief XPBD solver iterations per substep (clamped to [1, 16]). */
    int iterations = 4;
    /**
     * @brief XPBD distance compliance (m/N); 0 ≈ hard constraint.
     * Larger values allow more stretch.
     */
    float compliance = 0.f;
    /** @brief Clamp for a single step (seconds). */
    float maxDt = 1.f / 30.f;
    glm::vec3 wind{0.f, 0.f, 0.f};
    /**
     * @brief Infinite ground plane y = collisionY; particles below are pushed up.
     * Set very low (default) to disable.
     */
    float collisionY = -1.0e6f;
};

/**
 * @brief Lightweight Verlet + XPBD simulator for guide curves only.
 *
 * Owns mutable particle state derived from a rest `StrandsDatas`. Call
 * `snapshot()` to obtain a deformed guides buffer for `interpolateStrands`.
 *
 * @thread Affine to the caller; not synchronized.
 * @reentrancy Does not invoke callbacks.
 * @determinism Tolerance-bounded for identical params/seeded inputs.
 */
class GuideSimulator {
public:
    GuideSimulator() = default;

    /**
     * @brief Capture rest topology and pin every curve root.
     * @ownership Copies `guides`; caller retains the source.
     */
    [[nodiscard]] Result<void> reset(const StrandsDatas &guides,
                                     const GuideSimParams &params = {});

    void clear();

    void setParams(const GuideSimParams &params);
    [[nodiscard]] const GuideSimParams &params() const { return params_; }

    [[nodiscard]] bool isReady() const { return !curves_.empty(); }
    [[nodiscard]] size_t particleCount() const { return pos_.size(); }
    [[nodiscard]] size_t curveCount() const { return curves_.size(); }

    /**
     * @brief Teleport pinned roots (e.g. after skin binding deform).
     * @param rootPositionsXYZ Packed xyz, length = 3 * curveCount().
     */
    [[nodiscard]] Result<void> setPinnedRoots(const float *rootPositionsXYZ, int rootCount);

    /**
     * @brief Advance simulation by `dt` seconds (clamped by params.maxDt).
     */
    [[nodiscard]] Result<void> step(float dt);

    /**
     * @brief Export current particle positions as `StrandsDatas` (same topology).
     * @ownership Returned buffer uniquely owned by the caller.
     */
    [[nodiscard]] Result<StrandsDatas> snapshot() const;

private:
    struct CurveSpan {
        uint32_t pointOffset = 0;
        uint32_t pointCount = 0;
        float length = 0.f;
    };

    struct Segment {
        uint32_t i0 = 0;
        uint32_t i1 = 0;
        float restLength = 0.f;
    };

    void solveDistances(float dt);
    void resolveCollisions();

    GuideSimParams params_{};
    std::vector<StrandPoint> restPoints_;
    std::vector<CurveSpan> curves_;
    std::vector<Segment> segments_;
    std::vector<glm::vec3> pos_;
    std::vector<glm::vec3> prev_;
    std::vector<uint8_t> pinned_;  // 1 = immovable
};

}  // namespace eve::graphics::hair
