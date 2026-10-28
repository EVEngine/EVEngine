#include "procgen/heightmap/PcgTerrainStreaming.h"

#include "common/Capability.h"

#include <cmath>

namespace eve::procgen {

PcgTerrainStreamingAuthority::~PcgTerrainStreamingAuthority() {
    if (authority_) cap::removeListener<IPhotoModeFieldSink>(this);
}

Result<void> PcgTerrainStreamingAuthority::setTarget(TerrainStreamingCache* cache, float worldUnitsPerSample) {
    if (!std::isfinite(worldUnitsPerSample) || worldUnitsPerSample <= 0.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "world units per sample must be positive and finite", {}, {}, "procgen.pcgTerrainStreaming"));
    }
    cache_ = cache;
    state_.worldUnitsPerSample = worldUnitsPerSample;
    return Result<void>::success();
}

void PcgTerrainStreamingAuthority::setAuthority(bool enabled) {
    if (enabled == authority_) return;
    if (enabled) cap::addListener<IPhotoModeFieldSink>(this);
    else cap::removeListener<IPhotoModeFieldSink>(this);
    authority_ = enabled;
}

PhotoModeFieldAcceptance PcgTerrainStreamingAuthority::acceptsPhotoModeField(
    const PhotoModeAssignment& assignment) const noexcept {
    return assignment.domain == PhotoModeDomain::Streaming ? PhotoModeFieldAcceptance::Accepted
                                                            : PhotoModeFieldAcceptance::Rejected;
}

Result<void> PcgTerrainStreamingAuthority::applyPhotoModeField(const PhotoModeAssignment& assignment) {
    const auto* value = std::get_if<float>(&assignment.value);
    if (!value || !std::isfinite(*value) || *value < 0.f || *value > 5000.f) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "streaming range must be finite in [0,5000]", assignment.field, {},
            "procgen.pcgTerrainStreaming"));
    }
    auto next = state_;
    if (assignment.field == "m_pcgLoadRange") next.regularRange = *value;
    else if (assignment.field == "m_pcgImpostorRange") next.impostorRange = *value;
    else {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "unsupported streaming photo-mode field", assignment.field, {}, "procgen.pcgTerrainStreaming"));
    }
    state_ = next;
    return Result<void>::success();
}

int PcgTerrainStreamingAuthority::rangeToChunks(float range) const noexcept {
    if (!cache_) return 0;
    const int chunkSize = cache_->asset().getChunkSize();
    if (chunkSize <= 0) return 0;
    return static_cast<int>(std::ceil(range / (state_.worldUnitsPerSample * static_cast<float>(chunkSize))));
}

int PcgTerrainStreamingAuthority::regularRadiusChunks() const noexcept {
    return rangeToChunks(state_.regularRange);
}

int PcgTerrainStreamingAuthority::impostorRadiusChunks() const noexcept {
    return rangeToChunks(state_.impostorRange);
}

Result<TerrainStreamStats> PcgTerrainStreamingAuthority::streamAround(
    int worldSampleX, int worldSampleY, int maxLoads) {
    if (!cache_) {
        return Result<TerrainStreamStats>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
            "terrain streaming cache is unavailable", {}, {}, "procgen.pcgTerrainStreaming"));
    }
    if (maxLoads < 0) {
        return Result<TerrainStreamStats>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
            "maximum loads must be non-negative", {}, {}, "procgen.pcgTerrainStreaming"));
    }
    return Result<TerrainStreamStats>::success(
        cache_->streamAround(worldSampleX, worldSampleY, regularRadiusChunks(), maxLoads));
}

PcgTerrainStreamingTier PcgTerrainStreamingAuthority::tierForDistance(float worldDistance) const noexcept {
    if (!std::isfinite(worldDistance) || worldDistance < 0.f) return PcgTerrainStreamingTier::Unloaded;
    if (worldDistance <= state_.regularRange) return PcgTerrainStreamingTier::Regular;
    if (worldDistance <= state_.impostorRange) return PcgTerrainStreamingTier::Impostor;
    return PcgTerrainStreamingTier::Unloaded;
}

}  // namespace eve::procgen
