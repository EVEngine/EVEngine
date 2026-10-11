#pragma once

#include "common/PcgPhotoModeApply.h"
#include "procgen/heightmap/TerrainStreaming.h"

namespace eve::procgen {

/** @brief Runtime representation selected by Pcg's regular and impostor ranges. */
enum class PcgTerrainStreamingTier { Regular = 0, Impostor = 1, Unloaded = 2 };

/** @brief Pcg world-space terrain streaming ranges and their chunk-space projection. */
struct PcgTerrainStreamingState {
    float regularRange = 1000.f;
    float impostorRange = 2000.f;
    float worldUnitsPerSample = 1.f;
};

/**
 * @brief Explicit PhotoMode owner for a borrowed terrain streaming cache.
 * @lifetime The target cache must outlive this authority or be detached with setTarget(nullptr, scale).
 * @thread Game thread only; callbacks and scripts are not invoked.
 */
class PcgTerrainStreamingAuthority final : public IPhotoModeFieldSink {
public:
    ~PcgTerrainStreamingAuthority() override;

    /** @brief Attach a borrowed cache and define the world size of one terrain sample. */
    [[nodiscard]] Result<void> setTarget(TerrainStreamingCache* cache, float worldUnitsPerSample);
    /** @brief Register or revoke the unique Streaming-domain PhotoMode authority. */
    void setAuthority(bool enabled);
    /** @brief Return retained world-space range state. */
    const PcgTerrainStreamingState& state() const noexcept { return state_; }
    /** @brief Return the regular residency radius after conversion to chunks. */
    int regularRadiusChunks() const noexcept;
    /** @brief Return the impostor visibility radius after conversion to chunks. */
    int impostorRadiusChunks() const noexcept;
    /** @brief Stream full terrain chunks around a world-space sample coordinate. */
    [[nodiscard]] Result<TerrainStreamStats> streamAround(int worldSampleX, int worldSampleY, int maxLoads = 0);
    /** @brief Select the runtime representation for a world-space distance. */
    PcgTerrainStreamingTier tierForDistance(float worldDistance) const noexcept;

    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;

private:
    int rangeToChunks(float range) const noexcept;

    TerrainStreamingCache* cache_ = nullptr;
    PcgTerrainStreamingState state_{};
    bool authority_ = false;
};

}  // namespace eve::procgen
