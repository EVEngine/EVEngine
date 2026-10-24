#pragma once

#include "common/Export.h"

#include "fluids/OceanSurface.h"

#include <cstdint>
#include <memory>

namespace eve::fluids {

/** @brief A periodic spectral tile; disjoint wavelength bands prevent duplicated energy. */
struct OceanSpectrumBand {
    unsigned resolution        = 64;
    double   length            = 256.0;
    double   minimumWavelength = 8.0;
    double   maximumWavelength = 64.0;
};

/** @brief Authored JONSWAP wind sea in SI units; seed names the ocean phase stream. */
struct OceanSpectrumSettings {
    std::uint64_t                  seed                  = 1;
    double                         significantWaveHeight = 2.0;
    double                         peakPeriod            = 7.0;
    double                         peakEnhancement       = 3.3;
    double                         gravity               = 9.81;
    double                         seaLevel              = 0.0;
    double                         choppiness            = 0.5;
    glm::dvec2                     windDirection{1.0, 0.0};
    glm::dvec2                     current{0.0};
    std::vector<OceanSpectrumBand> bands{{64, 64.0, 2.0, 8.0}, {64, 256.0, 8.0, 64.0}, {64, 1024.0, 64.0, 1024.0}};
};

/** @brief Owning periodic displacement and particle velocity arrays; index = z * resolution + x. */
struct OceanSpectrumGrid {
    unsigned                resolution = 0;
    double                  length     = 0.0;
    std::vector<glm::dvec3> displacement;
    std::vector<glm::dvec3> velocity;
};

/** @brief Owning parametric surface geometry; derivatives are with respect to undisplaced world X and Z. */
struct OceanSurfaceGeometry {
    /** @brief Local persistent foam coverage, composed by OceanWaterView from its wake owner. */
    double     foam = 0.0;
    glm::dvec3 position{0.0};
    glm::dvec3 tangentX{1.0, 0.0, 0.0};
    glm::dvec3 tangentZ{0.0, 0.0, 1.0};
    /** @brief Compute the oriented unit normal from the same derivatives used by world-space queries. */
    glm::dvec3 normal() const { return glm::normalize(glm::cross(tangentZ, tangentX)); }
};

class OceanSpectrum;
class OceanSpectrumFrameBuilder;
struct OceanSpectrumData;

/**
 * @brief Immutable spatial projection at one simulation timestamp, independent of its spectrum owner.
 * Concurrent const reads are safe; no borrowed input or GPU resource is retained.
 */
class OceanSpectrumFrame {
public:
    /** @brief Borrow immutable grids until this frame is destroyed or moved; do not retain across mutation. */
    std::span<const OceanSpectrumGrid> grids() const { return grids_; }
    /** @brief Simulation seconds used to generate every grid in this frame. */
    double time() const { return time_; }
    /**
     * @brief Sample displaced geometry using periodic C1-continuous Catmull-Rom interpolation.
     * @return Owning world position or InvalidArgument for nonfinite/out-of-range inputs.
     * @thread Const/reentrant; synchronous, no callbacks or retained references.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<glm::dvec3> position(glm::dvec2 parameterXZ) const;
    /** @brief Evaluate position and exact interpolation derivatives without world-space inversion.
     * @return Owning geometry or InvalidArgument; const/reentrant, no callbacks or retained input.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSurfaceGeometry> geometry(glm::dvec2 parameterXZ) const;
    /**
     * @brief Invert horizontal displacement at world XZ and return the matching water sample.
     * @return Owning sample, InvalidArgument, or Unsupported if the wave surface cannot be inverted.
     * @thread Const/reentrant; no callbacks or retained input. Bounded iteration, no allocation.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSurfaceSample> sample(glm::dvec2 worldXZ) const;

private:
    friend class OceanSeaTransitionFrameBuilder;
    friend class OceanSpectrumFrameBuilder;
    friend class OceanGpuReadbackBuilder;
    OceanSpectrumFrame()                     = default;
    double                         time_     = 0.0;
    double                         seaLevel_ = 0.0;
    glm::dvec2                     current_{0.0};
    std::vector<OceanSpectrumGrid> grids_;
};

/** @brief Immutable initial spectrum; data is shared only with owning frame builders. */
class OceanSpectrum {
public:
    /** @brief Prepare a cheap owning builder; time validation and FFT work occur at build(). */
    [[nodiscard]] EVENGINE_API_DOMAINS OceanSpectrumFrameBuilder frameBuilder(double time) const;
    /** @brief Expected ensemble surface variance, normalized to significantWaveHeight squared / 16. */
    EVENGINE_API_DOMAINS double variance() const;

private:
    friend class OceanSeaTransitionBuilder;
    friend class OceanSpectrumBuilder;
    friend class OceanGpuSpectrumBuilder;
    explicit OceanSpectrum(std::shared_ptr<const OceanSpectrumData> data) : data_(std::move(data)) {}
    std::shared_ptr<const OceanSpectrumData> data_;
};

/** @brief Authored construction stage; build copies settings and never retains builder references. */
class OceanSpectrumBuilder {
public:
    OceanSpectrumSettings settings;
    /**
     * @brief Validate band coverage and build seeded JONSWAP coefficients atomically.
     * @return Immutable spectrum or InvalidArgument; allocation failure propagates without publication.
     * @cost Quadratic in each band resolution; amortize over the sea state's lifetime.
     * @thread Caller-owned builder, no callbacks; settings may change after build returns.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSpectrum> build() const;
};

/** @brief Owns a shared immutable spectrum lease and timestamp; safe after the original spectrum is destroyed. */
class OceanSpectrumFrameBuilder {
public:
    /**
     * @brief Evaluate six fields through inverse FFT and publish an owning frame.
     * @return Immutable spatial frame or InvalidArgument for invalid time.
     * @cost O(sum N*N*log(N)) compute and O(sum N*N) storage; reuse one frame across all consumers per tick.
     * @thread Reentrant const operation with private scratch; no callbacks or GPU synchronization.
     */
    [[nodiscard]] EVENGINE_API_DOMAINS Result<OceanSpectrumFrame> build() const;

private:
    friend class OceanSpectrum;
    OceanSpectrumFrameBuilder(std::shared_ptr<const OceanSpectrumData> data, double time)
        : data_(std::move(data)), time_(time) {}
    std::shared_ptr<const OceanSpectrumData> data_;
    double                                   time_;
};
}  // namespace eve::fluids
