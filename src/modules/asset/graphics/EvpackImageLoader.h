#pragma once

/** @file EvpackImageLoader.h @brief Transactional EVIMG runtime texture loading. */

#include "asset/EvpackImageDecoder.h"
#include "graphics/IImageResourceFactory.h"

namespace eve::graphics {
class IResourceFactory;
}

namespace eve::asset_graphics {

using ImageAssetLoadLimits = asset::EvpackImageDecodeLimits;

/** @brief Successfully uploaded texture and selected capability variant. */
struct LoadedGraphicsImage {
    AssetRef                      asset;
    graphics::Texture*            texture = nullptr;
    asset::EvpackVariantSelection variant;
};

/** @brief Result-based adapter over the engine's established graphics resource factory. */
class GraphicsImageFactoryAdapter final : public graphics::IImageResourceFactory {
public:
    /** @brief Bind a borrowed graphics factory that must outlive this adapter. */
    explicit GraphicsImageFactoryAdapter(graphics::IResourceFactory& factory) noexcept
        : factory_(factory) {}

    /**
     * @copydoc graphics::IImageResourceFactory::uploadRgba8
     * @ownership Returned texture remains owned by the bound graphics factory.
     * @lifetime Valid until release, backend shutdown, or device loss.
     */
    [[nodiscard]] Result<graphics::Texture*> uploadRgba8(
        std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels,
        bool srgb) override;

    /** @copydoc graphics::IImageResourceFactory::uploadRgba8MipChain */
    [[nodiscard]] Result<graphics::Texture*> uploadRgba8MipChain(uint32_t width, uint32_t height, uint32_t levels,
                                                                 std::span<const uint8_t> pixels) override;

    /** @copydoc graphics::IImageResourceFactory::uploadRgba8Volume */
    [[nodiscard]] Result<graphics::Texture*> uploadRgba8Volume(uint32_t width, uint32_t height, uint32_t depth,
                                                               std::span<const uint8_t> pixels) override;

    /**
     * @copydoc graphics::IImageResourceFactory::releaseImage
     * @param texture Borrowed factory-owned texture, observed only for this call.
     */
    [[nodiscard]] Result<void> releaseImage(graphics::Texture* texture) override;

private:
    graphics::IResourceFactory& factory_;
};

/** @brief Capability-aware adapter from admitted EVIMG assets to backend textures. */
class EvpackImageLoader {
public:
    /** @brief Bind borrowed reader and image factory; both must outlive this loader. */
    EvpackImageLoader(const asset::EvpackResourceReader& reader,
                      graphics::IImageResourceFactory& factory) noexcept
        : reader_(reader), factory_(factory) {}

    /**
     * @brief Validate and upload image/3 mip assets, retaining image/2 single-level compatibility.
     * @return
     * Borrowed backend texture on success; validation failure performs no upload.
     */
    [[nodiscard]] Result<LoadedGraphicsImage> load(
        const AssetRef& image, const asset::EvpackCapabilities& capabilities,
        const ImageAssetLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
    graphics::IImageResourceFactory&   factory_;
};

/** @brief Bounds checked before backend volume allocation. */
struct VolumeTextureAssetLoadLimits {
    std::uint32_t maximumDimension    = 2048;
    std::uint64_t maximumVoxels       = 268'435'456;
    std::uint64_t maximumDecodedBytes = 1024ull * 1024ull * 1024ull;
};

/** @brief Successfully uploaded volume texture and selected capability variant. */
struct LoadedGraphicsVolumeTexture {
    AssetRef                      asset;
    graphics::Texture*            texture = nullptr;
    asset::EvpackVariantSelection variant;
};

/** @brief Capability-aware loader for canonical volume-texture/1 assets. */
class EvpackVolumeTextureLoader {
public:
    /** @brief Bind borrowed reader and image factory; both must outlive this loader. */
    EvpackVolumeTextureLoader(const asset::EvpackResourceReader& reader,
                              graphics::IImageResourceFactory&   factory) noexcept
        : reader_(reader), factory_(factory) {}

    /** @brief Validate definition and EVVOL bulk before atomically uploading one volume.
     * @param volume Canonical
     * asset identity.
     * @param capabilities Runtime target capabilities.
     * @param limits Allocation and
     * decoded-byte limits.
     * @return Borrowed backend texture on success; validation failure performs no upload.

     * * @thread Graphics thread, synchronous and non-reentrant; no callbacks.
     */
    [[nodiscard]] Result<LoadedGraphicsVolumeTexture> load(const AssetRef&                     volume,
                                                           const asset::EvpackCapabilities&    capabilities,
                                                           const VolumeTextureAssetLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
    graphics::IImageResourceFactory&   factory_;
};

}  // namespace eve::asset_graphics
