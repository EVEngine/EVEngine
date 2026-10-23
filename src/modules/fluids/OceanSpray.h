#pragma once

#include "common/Export.h"

#include <cstdint>
#include <glm/glm.hpp>
#include <span>
#include <vector>
#include "common/Result.h"

namespace eve::fluids {
class OceanWaterView;
struct OceanBoatReceipt;
/** @brief Bounded visual droplets in SI units, driven by the ocean simulation clock. */
struct OceanSpraySettings {
    std::uint64_t seed                   = 1;
    unsigned      capacity               = 2048;
    double        gravity                = 9.81;
    double        lifetime               = 1.0;
    double        radius                 = 0.04;
    double        emissionPerVolumeSpeed = 16.0;
};
/** @brief Owning particle value; ID is local to the spray stream, never a body handle. */
struct OceanSprayParticle {
    std::uint64_t id = 0;
    glm::dvec3    position{0.0}, velocity{0.0};
    double        age = 0.0, lifetime = 1.0, radius = 0.04;
};
/** @brief One borrowed receipt, alive and immutable until its step builder finishes build(). */
struct OceanSpraySource {
    std::uint64_t           id;
    const OceanBoatReceipt& receipt;
};
/** @brief Capacity and lifecycle evidence for the most recently built step. */
struct OceanSprayReport {
    unsigned emitted = 0, dropped = 0, expired = 0, returnedToWater = 0;
};
class OceanSprayStepBuilder;
/**
 * @brief Immutable owning visual spray state. The application owns the current frame;
 * replacing it with a successful step is publication. No body, provider or borrowed
 * input survives build. Concurrent reads are safe; no callbacks or wall-clock reads.
 * Determinism is same-input repeatability, with numerical cross-platform tolerance.
 */
class OceanSprayFrame {
public:
    /** @brief Validate settings and create an empty state at time zero. */
    [[nodiscard]] static EVENGINE_API_DOMAINS Result<OceanSprayFrame> create(const OceanSpraySettings& settings);
    /** @brief Borrow particles until this frame is destroyed or moved; never retain across replacement. */
    std::span<const OceanSprayParticle> particles() const { return particles_; }
    /** @brief Simulation seconds represented by this frame. */
    double time() const { return time_; }
    /** @brief Owning counters for accepted, dropped and retired particles. */
    OceanSprayReport report() const { return report_; }
    /** @brief Copy the authored settings; particles retain their own spawn lifetime and radius. */
    OceanSpraySettings settings() const { return settings_; }

private:
    friend class OceanSprayStepBuilder;
    OceanSprayFrame() = default;
    struct Stream {
        std::uint64_t id;
        double        remainder;
    };
    OceanSpraySettings              settings_;
    double                          time_   = 0.0;
    std::uint64_t                   nextId_ = 1, tick_ = 0;
    std::vector<OceanSprayParticle> particles_;
    std::vector<Stream>             streams_;
    OceanSprayReport                report_;
};
/**
 * @brief Cheap borrowed preparation for one spray step. Frame, water, source span and
 * receipts must remain alive and unchanged until build returns. Do not retain this
 * builder across owner mutation/destruction or asynchronous work. No callbacks.
 */
class OceanSprayStepBuilder {
public:
    /** @brief Borrow step inputs; at most 32 distinct named sources, at most 64 contacts each. */
    OceanSprayStepBuilder(const OceanSprayFrame& previous, const OceanWaterView& water,
                          std::span<const OceanSpraySource> sources, double dt)
        : previous_(previous), water_(water), sources_(sources), dt_(dt) {}
    /**
     * @brief Validate the whole batch and construct a new owning state without input mutation.
     * @return State or input/surface-query diagnostic. Water/receipts must match previous.time().
     * @cost Linear in live droplets and contacts plus surface queries; once per fixed step.
     * Sources are ordered by explicit ID, independent of caller traversal order. Gravity is
     * integrated ballistically; re-entry uses the supplied step-start water, a dt-bounded
     * visual approximation. Droplets do not feed momentum back into the fluid or rigid body.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSprayFrame> build() const;

private:
    const OceanSprayFrame&            previous_;
    const OceanWaterView&             water_;
    std::span<const OceanSpraySource> sources_;
    double                            dt_;
};
}  // namespace eve::fluids
