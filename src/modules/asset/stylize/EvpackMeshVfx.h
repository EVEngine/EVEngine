#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include "common/BorrowedRef.h"
#include "common/ResourceRef.h"

namespace eve::asset {
class EvpackResourceReader;
struct EvpackCapabilities;
}  // namespace eve::asset
namespace eve::graphics {
class Graphics;
class Mesh;
class Texture;
}  // namespace eve::graphics
namespace eve::asset_stylize {
/**
 * @brief Owning package-backed mesh effect, using the canonical MeshVfxAsset playback and curves.
 * @ownership Owns playback and shader leases. Graphics must outlive it; destroy before device shutdown.
 * @lifetime Mesh/texture inputs are never retained. Load/reload copy package data; readers may be destroyed
 * immediately.
 * @thread Graphics-thread affine, no callbacks, locks or reentrancy. Time comes exclusively from advance(dt).
 */
class EvpackMeshVfx final {
public:
    ~EvpackMeshVfx();
    EvpackMeshVfx(const EvpackMeshVfx&)            = delete;
    EvpackMeshVfx& operator=(const EvpackMeshVfx&) = delete;
    /** @brief Resolve every layer and validate all candidates before GPU upload; no partial instance is published. */
    [[nodiscard]] static Result<std::unique_ptr<EvpackMeshVfx>> load(const asset::EvpackResourceReader& reader,
                                                                     const AssetRef&                    asset,
                                                                     const asset::EvpackCapabilities&   capabilities,
                                                                     graphics::Graphics&                graphics);
    /** @brief Restart the authored playback and reset its curves on the next advance. */
    void play() noexcept;
    /** @brief Advance using finite nonnegative seconds; invalid input leaves playback unchanged. */
    [[nodiscard]] Result<void> advance(float dt);
    /** @brief Stop with an optional finite nonnegative fade. */
    [[nodiscard]] Result<void> stop(float fadeOutSeconds = 0.f);
    /** @brief Set one scalar override for one layer; authored curves still own animated parameters on advance. */
    [[nodiscard]] Result<void> setFloat(std::size_t layer, std::string_view name, float value);
    /** @brief Draw active layers during an open 3D frame with caller-owned mesh/texture and instance transform. Caller
     * selects surface/blend/depth state. */
    [[nodiscard]] Result<void> draw(graphics::Mesh& mesh, const std::array<float, 16>& transform,
                                    OptionalRef<graphics::Texture> texture,
                                    const std::array<float, 4>&    tint = {1, 1, 1, 1});
    /** @brief Atomically replace only after successful full load. Reapply defaults and restart; failure preserves old
     * playback. */
    [[nodiscard]] Result<void> reload(const asset::EvpackResourceReader& reader, const AssetRef& asset,
                                      const asset::EvpackCapabilities& capabilities);
    /** @brief Drain canonical timeline events; no caller callback is invoked while advancing. */
    [[nodiscard]] std::vector<std::string> drainEvents();

private:
    struct Impl;
    explicit EvpackMeshVfx(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::asset_stylize
