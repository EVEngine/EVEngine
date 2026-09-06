#pragma once

/** @file EvpackGraphicsLoader.h @brief Transactional graphics consumers for canonical runtime assets. */

#include "asset/EvpackResourceReader.h"
#include "graphics/IMeshResourceFactory.h"

namespace eve::graphics {
class IResourceFactory;
class Mesh;
}  // namespace eve::graphics

namespace eve::asset_graphics {

/** @brief Successfully uploaded backend mesh and the selected package variant. */
struct LoadedGraphicsMesh {
    AssetRef                      asset;
    graphics::Mesh*               mesh = nullptr;
    asset::EvpackVariantSelection variant;
};

/** @brief Bounds applied before allocating canonical mesh staging arrays. */
struct GraphicsAssetLoadLimits {
    std::uint32_t maximumVertices = 4'000'000;
    std::uint32_t maximumIndices = 12'000'000;
    std::uint64_t maximumDecodedBytes = 512ull * 1024ull * 1024ull;
};

/** @brief Result-based adapter over the engine's established graphics resource factory. */
class GraphicsMeshFactoryAdapter final : public graphics::IMeshResourceFactory {
public:
    /** @brief Bind a borrowed graphics factory that must outlive this adapter. */
    explicit GraphicsMeshFactoryAdapter(graphics::IResourceFactory& factory) noexcept
        : factory_(factory) {}

    /**
     * @copydoc graphics::IMeshResourceFactory::uploadMesh
     * @ownership Returned mesh remains owned by the bound graphics factory.
     * @lifetime Valid until release, backend shutdown, or device loss.
     */
    [[nodiscard]] Result<graphics::Mesh*> uploadMesh(
        const float* posXYZ, const float* nrmXYZ, const float* uvST, int vertexCount,
        const std::uint32_t* indices, int indexCount) override;

    /**
     * @copydoc graphics::IMeshResourceFactory::releaseMesh
     * @param mesh Borrowed factory-owned mesh, observed only for this call.
     */
    [[nodiscard]] Result<void> releaseMesh(graphics::Mesh* mesh) override;

private:
    graphics::IResourceFactory& factory_;
};

/** @brief Capability-aware adapter from admitted `.evpack` assets to graphics resources. */
class EvpackGraphicsLoader {
public:
    /** @brief Bind a reader and backend factory; both must outlive this adapter. */
    EvpackGraphicsLoader(const asset::EvpackResourceReader& reader,
                         graphics::IMeshResourceFactory& factory) noexcept
        : reader_(reader), factory_(factory) {}

    /**
     * @brief Validate and upload one canonical `eve.mesh/1` asset atomically.
     * @return A borrowed backend-owned mesh on success. The caller releases it through the
     * same `IMeshResourceFactory`; no backend object is created when package validation fails.
     */
    [[nodiscard]] Result<LoadedGraphicsMesh> loadMesh(
        const AssetRef& asset, const asset::EvpackCapabilities& capabilities,
        const GraphicsAssetLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
    graphics::IMeshResourceFactory&    factory_;
};

}  // namespace eve::asset_graphics
