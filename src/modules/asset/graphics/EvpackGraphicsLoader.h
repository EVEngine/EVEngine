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
    /** @brief Explicit UV channel projected into the backend single-UV vertex stream. */
    std::uint32_t texcoordSet = 0;
    /** @brief Available source UV channels, validated before upload. */
    std::vector<std::uint32_t> availableTexcoordSets;
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

    /** @copydoc graphics::IMeshResourceFactory::setMeshTexcoords */
    [[nodiscard]] Result<void> setMeshTexcoords(graphics::Mesh* mesh, std::uint32_t set,
                                                std::span<const float> values) override;

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
     * @brief Validate and upload one canonical mesh v2 (or compatibility v1) atomically.
     * @param texcoordSet UV channel to upload to the existing single-UV backend. Missing channels fail,
     * except channel 0 on an untextured mesh. Other channels remain available through decodeCanonicalMesh.
     * @thread Graphics thread; inputs are borrowed for this call, no callbacks or retained staging pointers.
     * @param preserveAllTexcoords Attach every other UV channel through the factory before publication.
     * Providers without that capability fail and the staged mesh is released.
     * @details Default mode explicitly projects one channel; full mode retains every channel.
     * @return A borrowed backend-owned mesh on success. The caller releases it through the
     * same `IMeshResourceFactory`; no backend object is created when package validation fails.
     */
    [[nodiscard]] Result<LoadedGraphicsMesh> loadMesh(const AssetRef&                  asset,
                                                      const asset::EvpackCapabilities& capabilities,
                                                      const GraphicsAssetLoadLimits&   limits      = {},
                                                      std::uint32_t                    texcoordSet = 0,
                                                      bool preserveAllTexcoords                    = false) const;

private:
    const asset::EvpackResourceReader& reader_;
    graphics::IMeshResourceFactory&    factory_;
};

}  // namespace eve::asset_graphics
