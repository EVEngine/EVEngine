#pragma once

#include "common/Export.h"
#include "common/PcgPhotoModeApply.h"
#include "procgen/heightmap/TerrainMesh.h"

namespace eve::procgen {
/** @brief Pcg terrain quality state used by terrain mesh, material and detail render callers. */
struct PcgTerrainPhotoModeState {
    bool drawInstanced = true;
    float detailDensity = 0.5f;
    float detailDistance = 150.f;
    float pixelError = 5.f;
    float basemapDistance = 1024.f;
};
/** @brief Terrain material tier selected from Pcg's basemap distance. */
enum class PcgTerrainTextureTier { Detailed = 0, Basemap = 1 };

/** @brief Explicit photo-mode owner for runtime terrain quality selection. */
class EVENGINE_API_DOMAINS PcgTerrainPhotoModeAuthority final : public IPhotoModeFieldSink {
public:
    ~PcgTerrainPhotoModeAuthority() override;
    /** @brief Register or revoke the unique terrain photo-mode authority. */
    void setAuthority(bool enabled);
    /** @brief Return the retained terrain quality state. */
    const PcgTerrainPhotoModeState& state() const noexcept { return state_; }
    /** @brief Select terrain mesh LOD using the current Pcg pixel-error budget. */
    int selectLod(const Heightmap& heightmap, TerrainMeshSettings settings, int maxLod, float distance,
                  float viewportHeight, float verticalFovDegrees) const;
    /** @brief Return whether distance has crossed Pcg's basemap texture threshold. */
    PcgTerrainTextureTier textureTier(float distance) const noexcept;
    PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept override;
    [[nodiscard]] Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment) override;
private:
    bool authority_ = false;
    PcgTerrainPhotoModeState state_{};
};
}
