#pragma once

#include "common/Result.h"
#include "image/ImageData.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

/** @brief Backend-neutral configuration for a paged material-attribute texture. */
struct VirtualTextureBlendConfig {
    int         width                = 1024;
    int         height               = 1024;
    int         tileSize             = 128;
    int         borderSize           = 2;
    std::size_t residentPageCapacity = 64;
};

/** @brief One immutable source layer copied into a VirtualTextureBlend. */
struct VirtualTextureBlendLayer {
    std::shared_ptr<const image::ImageData> albedo;
    std::shared_ptr<const image::ImageData> normal;
    std::shared_ptr<const image::ImageData> weight;
    float                                   uvScale        = 1.f;
    float                                   constantWeight = 1.f;
    float                                   heightContrast = 0.f;
};

/** @brief Observable residency counters for virtual-texture diagnostics. */
struct VirtualTextureBlendStats {
    std::size_t   residentPages = 0;
    std::uint64_t pageHits      = 0;
    std::uint64_t pageMisses    = 0;
    std::uint64_t pageEvictions = 0;
};

/** @brief Mip-zero virtual page selected for physical residency. */
struct VirtualTexturePageId {
    int  x                                                   = 0;
    int  y                                                   = 0;
    bool operator==(const VirtualTexturePageId& other) const = default;
};

/**
 * @brief CPU images and sampling metadata for one atomically publishable physical cache.
 *
 * Slot zero contains a whole-surface fallback. Resident virtual pages occupy the remaining slots;
 * page-table blue is one for a resident page and zero for fallback. Red/green contain normalized
 * physical-slot centres. Upload all three images before publishing the associated material state.
 */
struct VirtualTextureResidentSet {
    std::unique_ptr<image::ImageData> albedoAtlas;
    std::unique_ptr<image::ImageData> normalAtlas;
    std::unique_ptr<image::ImageData> pageTable;
    int                               atlasSlotsX    = 0;
    int                               atlasSlotsY    = 0;
    int                               pageExtent     = 0;
    float                             borderFraction = 0.f;
    std::size_t                       residentPages  = 0;
};

/**
 * @brief CPU reference implementation of a tiled, height-aware material virtual texture.
 *
 * Pages are generated on demand and retained in an LRU cache. Source rasters are copied when a
 * layer is admitted, so requests never retain caller-owned pointers. Every page includes a texel
 * border sampled from virtual neighbours, preventing filtering seams between physical pages.
 * The implementation is deterministic and backend-neutral; render backends may upload requested
 * pages to a physical cache without changing the material authoring contract.
 *
 * @thread Not thread-safe; create, mutate, and request pages on one authoring/render thread.
 * @reentrancy Does not invoke callbacks.
 */
class VirtualTextureBlend {
public:
    /** @brief Validate configuration and create an empty virtual texture. */
    [[nodiscard]] static eve::Result<std::unique_ptr<VirtualTextureBlend>> create(
        const VirtualTextureBlendConfig& config);

    /**
     * @brief Copy and append one material layer, invalidating generated pages atomically.
     * @return Stable zero-based layer index, or a structured validation failure.
     */
    [[nodiscard]] eve::Result<std::uint32_t> addLayer(const VirtualTextureBlendLayer& layer);

    /**
     * @brief Resolve one page at the requested mip level.
     * @return Shared immutable RGBA8 page. The receipt remains alive after LRU eviction.
     */
    [[nodiscard]] eve::Result<std::shared_ptr<const image::ImageData>> requestAlbedoPage(int pageX, int pageY,
                                                                                         int mipLevel);

    /** @brief Resolve a tangent-space normal page using vector blending and renormalization. */
    [[nodiscard]] eve::Result<std::shared_ptr<const image::ImageData>> requestNormalPage(int pageX, int pageY,
                                                                                         int mipLevel);

    /** @brief Bake the complete mip-zero albedo fallback without exposing cache internals. */
    [[nodiscard]] eve::Result<std::unique_ptr<image::ImageData>> bakeAlbedo();

    /** @brief Bake the complete mip-zero tangent-space normal fallback. */
    [[nodiscard]] eve::Result<std::unique_ptr<image::ImageData>> bakeNormal();

    /**
     * @brief Build an atlas, page table, and whole-surface fallback for selected mip-zero pages.
     * @param residentPages Unique page coordinates to publish as resident.
     * @return A self-contained immutable upload transaction, or a validation failure.
     */
    [[nodiscard]] eve::Result<VirtualTextureResidentSet> buildResidentSet(
        const std::vector<VirtualTexturePageId>& residentPages);

    /** @brief Clear generated pages while preserving immutable source layers. */
    void invalidate();

    /** @brief Snapshot current cache counters. */
    VirtualTextureBlendStats stats() const;

    int                              pageCountX(int mipLevel = 0) const;
    int                              pageCountY(int mipLevel = 0) const;
    const VirtualTextureBlendConfig& config() const { return config_; }

private:
    enum class Attribute : std::uint8_t { Albedo, Normal };
    struct PageKey {
        int       x                                      = 0;
        int       y                                      = 0;
        int       mip                                    = 0;
        Attribute attribute                              = Attribute::Albedo;
        bool      operator==(const PageKey& other) const = default;
    };
    struct PageKeyHash {
        std::size_t operator()(const PageKey& key) const noexcept;
    };
    struct CacheEntry {
        std::shared_ptr<const image::ImageData> image;
        std::list<PageKey>::iterator            lru;
    };

    explicit VirtualTextureBlend(VirtualTextureBlendConfig config) : config_(config) {}
    [[nodiscard]] eve::Result<std::shared_ptr<const image::ImageData>> requestPage(PageKey key);
    std::shared_ptr<const image::ImageData>                            composePage(const PageKey& key) const;
    [[nodiscard]] eve::Result<std::unique_ptr<image::ImageData>>       bakeAttribute(Attribute attribute);

    VirtualTextureBlendConfig                            config_;
    std::vector<VirtualTextureBlendLayer>                layers_;
    std::list<PageKey>                                   lru_;
    std::unordered_map<PageKey, CacheEntry, PageKeyHash> cache_;
    std::uint64_t                                        hits_      = 0;
    std::uint64_t                                        misses_    = 0;
    std::uint64_t                                        evictions_ = 0;
};

}  // namespace eve::graphics
