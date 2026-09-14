#pragma once

/** @file EvpackImageLoader.h @brief Transactional EVIMG runtime texture loading. */

#include "asset/EvpackResourceReader.h"
#include "graphics/IImageResourceFactory.h"

namespace eve::graphics {
class IResourceFactory;
}

namespace eve::asset_graphics {

/** @brief Bounds checked before backend texture allocation. */
struct ImageAssetLoadLimits {
    std::uint32_t maximumDimension = 32768;
    std::uint64_t maximumPixels = 268'435'456;
    std::uint64_t maximumDecodedBytes = 1024ull * 1024ull * 1024ull;
};

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
     * @brief Validate and upload one canonical `eve.image/2` asset atomically.
     * @return Borrowed backend texture on success; validation failure performs no upload.
     */
    [[nodiscard]] Result<LoadedGraphicsImage> load(
        const AssetRef& image, const asset::EvpackCapabilities& capabilities,
        const ImageAssetLoadLimits& limits = {}) const;

private:
    const asset::EvpackResourceReader& reader_;
    graphics::IImageResourceFactory&   factory_;
};

}  // namespace eve::asset_graphics
