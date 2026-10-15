#pragma once

#include <array>
#include "common/GpuResidentBufferView.h"
#include "fluids/OceanSpectrum.h"

namespace eve::graphics {
class Graphics;
}
namespace eve::gpgpu {
class Gpgpu;
}
namespace eve::fluids {
class OceanSeaTransition;
class OceanGpuSpectrum;
class OceanGpuFrameBuilder;
class OceanGpuReadbackBuilder;

/** @brief Opaque revision of a completed GPU ocean; copying does not extend resource lifetime. */
class OceanGpuFrameToken {
public:
    /** @brief Simulation timestamp of this completed projection. */
    double time() const { return time_; }

private:
    friend class OceanGpuSpectrum;
    friend class OceanGpuFrameBuilder;
    friend class OceanGpuReadbackBuilder;
    std::uint64_t owner_ = 0, revision_ = 0;
    double        time_ = 0.0;
};

/** @brief Call-only borrowed planar float buffer: displacement XYZ then velocity XYZ, each N*N floats.
 * Consume synchronously before the next submit or owner/provider destruction; never retain for pending draws.
 */
struct OceanGpuBandView {
    GpuResidentBufferView buffer;
    unsigned              resolution = 0;
    double                length     = 0.0;
};
/** @brief One band's XYZ displacement planes in the resident rendering atlas. */
struct OceanGpuAtlasBand {
    unsigned resolution = 0, firstRow = 0;
    double   length = 0.0;
};
/** @brief Call-only borrowed packed float atlas; each four RGBA8 bytes preserve one float's bits.
 * Consume before the next submit or owner/provider destruction. No CPU displacement readback is needed.
 */
struct OceanGpuAtlasView {
    GpuResidentBufferView            buffer;
    unsigned                         width = 0, height = 0, bandCount = 0;
    double                           seaLevel = 0.0;
    std::array<OceanGpuAtlasBand, 4> bands{};
};

/**
 * @brief Render-thread-owned GPU projection of immutable spectral coefficients, with no independent clock.
 * Vulkan compute reference parity is numerical, not bitwise. No callbacks during submission and no locks.
 * Graphics retirement releases compute resources before the device; either destruction order is safe.
 * A new submission invalidates all earlier tokens, including after a failed partially executed submission.
 * Backend failure poisons the projection; rebuild it instead of appending commands to a failed recording.
 */
class OceanGpuSpectrum {
public:
    ~OceanGpuSpectrum();
    OceanGpuSpectrum(const OceanGpuSpectrum&)            = delete;
    OceanGpuSpectrum& operator=(const OceanGpuSpectrum&) = delete;
    /** @brief Prepare a cheap call-only builder; owner must outlive submit(). */
    [[nodiscard]] OceanGpuFrameBuilder frameBuilder(double time);
    /** @brief Validate revision and borrow one band for immediate same-device consumption; no allocation. */
    [[nodiscard]] Result<OceanGpuBandView> band(const OceanGpuFrameToken& token, unsigned index) const;
    /** @brief Validate revision and borrow the complete rendering atlas for immediate same-device copying. */
    [[nodiscard]] Result<OceanGpuAtlasView> atlas(const OceanGpuFrameToken& token) const;
    /** @brief Prepare explicit diagnostic readback; no readback occurs in normal GPU rendering. */
    [[nodiscard]] OceanGpuReadbackBuilder readbackBuilder(const OceanGpuFrameToken& token) const;

private:
    friend class OceanGpuSpectrumBuilder;
    friend class OceanGpuFrameBuilder;
    friend class OceanGpuReadbackBuilder;
    OceanGpuSpectrum();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/** @brief Prepare GPU allocation from an owning immutable spectrum lease; providers borrowed through build(). */
class OceanGpuSpectrumBuilder {
public:
    /** @brief Graphics must be the active same-device provider used by the injected Gpgpu service. */
    OceanGpuSpectrumBuilder(const OceanSpectrum& spectrum, graphics::Graphics& graphics, gpgpu::Gpgpu& compute);
    /** @brief Own both immutable transition endpoints; providers borrowed through build(). */
    OceanGpuSpectrumBuilder(const OceanSeaTransition& transition, graphics::Graphics& graphics, gpgpu::Gpgpu& compute);
    /** @brief Allocate compiled pipelines and band buffers; Unsupported explicitly selects a caller-chosen CPU path.
     * @cost Pipeline creation plus O(sum N*N) allocation; amortize over sea-state lifetime.
     * @return Owning projection or diagnostic; render-thread-only, no partial publication or retained provider owners.
     */
    [[nodiscard]] Result<std::unique_ptr<OceanGpuSpectrum>> build() const;

private:
    std::shared_ptr<const OceanSpectrumData> data_, target_;
    double                                   start_ = 0.0, duration_ = 1.0;
    graphics::Graphics&                      graphics_;
    gpgpu::Gpgpu&                            compute_;
};

/** @brief Synchronous borrowed submission builder; do not retain across owner destruction. */
class OceanGpuFrameBuilder {
public:
    /** @brief Evolve all six spectral fields and inverse FFT in one command sequence; no CPU readback.
     * @cost O(sum N*N*log(N)) GPU work and one synchronous submission; reuse output across every consumer this frame.
     * @return Completed revision or diagnostic; old revisions are invalidated before GPU work starts.
     * @thread Render-thread only; no concurrent provider retirement or callbacks.
     */
    [[nodiscard]] Result<OceanGpuFrameToken> submit() const;

private:
    friend class OceanGpuSpectrum;
    OceanGpuFrameBuilder(OceanGpuSpectrum& owner, double time) : owner_(owner), time_(time) {}
    OceanGpuSpectrum& owner_;
    double            time_;
};

/** @brief Borrowed diagnostic readback builder; revision checked again at build, never silently reads a newer frame. */
class OceanGpuReadbackBuilder {
public:
    /** @brief Download a completed projection into an owning CPU frame for parity tests and diagnostics.
     * @cost O(sum N*N) synchronous GPU readback and allocation; diagnostic operation, avoid in the render loop.
     * @return Independent frame or StaleHandle/backend diagnostic; render-thread only, no callbacks.
     */
    [[nodiscard]] Result<OceanSpectrumFrame> build() const;

private:
    friend class OceanGpuSpectrum;
    OceanGpuReadbackBuilder(const OceanGpuSpectrum& owner, OceanGpuFrameToken token) : owner_(owner), token_(token) {}
    const OceanGpuSpectrum& owner_;
    OceanGpuFrameToken      token_;
};
}  // namespace eve::fluids
