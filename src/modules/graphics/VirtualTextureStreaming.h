#pragma once

#include "common/Result.h"
#include "graphics/VirtualTextureBlend.h"
#include "image/ImageData.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

class Graphics;
class Material;
class Texture;

/**
 * @brief One GPU/CPU feedback sample requesting residency for a virtual page.
 *
 * Packed layout matches the GLSL helper in `virtual_texture.glsl`:
 * bits 0-11 pageX, 12-23 pageY, 24-31 mipLevel.
 */
struct VirtualTextureFeedbackSample {
    int pageX    = 0;
    int pageY    = 0;
    int mipLevel = 0;
};

/** @brief Runtime streaming budgets over a VirtualTextureBlend producer. */
struct VirtualTextureStreamingConfig {
    VirtualTextureBlendConfig blend{};
    /** @brief Maximum page decodes applied by one update() call. */
    std::size_t maxDecodesPerUpdate = 4;
    /** @brief Soft cap on retained raw feedback samples before compact. */
    std::size_t feedbackCapacity = 4096;
};

/** @brief Observable counters for one streaming session. */
struct VirtualTextureStreamingStats {
    std::uint64_t generation         = 0;
    std::size_t   residentPages      = 0;
    std::size_t   pendingFeedback    = 0;
    std::size_t   pendingUploads     = 0;
    std::uint64_t feedbackSamples    = 0;
    std::uint64_t uniqueFeedback     = 0;
    std::uint64_t pagesDecoded       = 0;
    std::uint64_t pagesEvicted       = 0;
    std::uint64_t pagesInvalidated   = 0;
    std::uint64_t publishes          = 0;
    std::uint64_t abandonedPublishes = 0;
};

/**
 * @brief One RGBA8 rectangle owned by an upload batch.
 * @ownership Pixels are owned by the batch until it is destroyed.
 */
struct VirtualTextureRegionPixels {
    int                       x      = 0;
    int                       y      = 0;
    int                       width  = 0;
    int                       height = 0;
    std::vector<std::uint8_t> rgba;
};

/**
 * @brief Ordered GPU upload transaction for atlas pages and page-table texels.
 *
 * Apply `pageTableClears` first, then albedo/normal regions, then `pageTableSets`.
 * Commit only after every region upload succeeds; otherwise call abandonPublish().
 */
struct VirtualTextureUploadBatch {
    std::uint64_t                           expectedGeneration = 0;
    std::vector<VirtualTextureRegionPixels> pageTableClears;
    std::vector<VirtualTextureRegionPixels> albedoRegions;
    std::vector<VirtualTextureRegionPixels> normalRegions;
    std::vector<VirtualTextureRegionPixels> pageTableSets;
};

/**
 * @brief UE5-style runtime virtual texture session: feedback → decode → atomic publish.
 *
 * Owns a VirtualTextureBlend producer plus a portable physical atlas / page-table cache.
 * GPU resources stay in Graphics; this object only stages CPU pixels and generation-qualified
 * upload batches. Sampling continues to use Material::setVirtualTexture().
 *
 * @thread Not thread-safe; record feedback, update, and publish on one authoring/render thread.
 * @reentrancy Does not invoke callbacks.
 */
class EVENGINE_API_BACKENDS VirtualTextureStreaming {
public:
    /** @brief Validate config and create an empty streaming session. */
    [[nodiscard]] static eve::Result<std::unique_ptr<VirtualTextureStreaming>> create(
        const VirtualTextureStreamingConfig& config);

    /**
     * @brief Copy and append one material layer through the owned producer.
     * @return Stable zero-based layer index, or a structured validation failure.
     */
    [[nodiscard]] eve::Result<std::uint32_t> addLayer(const VirtualTextureBlendLayer& layer);

    /**
     * @brief Allocate CPU atlases and a page table with only the substitute slot resident.
     * @return Success after publishing generation zero, or a structured failure.
     */
    [[nodiscard]] eve::Result<void> initializePhysicalCache();

    /** @brief Pack a feedback sample into the 32-bit GPU/CPU wire format. */
    [[nodiscard]] static std::uint32_t packFeedback(int pageX, int pageY, int mipLevel) noexcept;
    /**
     * @brief Unpack a feedback sample into page coordinates.
     * @return Success with the decoded sample, or InvalidArgument when fields exceed the bit budget.
     */
    [[nodiscard]] static eve::Result<VirtualTextureFeedbackSample> unpackFeedback(std::uint32_t packed);

    /**
     * @brief Record one page request (cheap; retained until the next update compact).
     * @return Failure when the physical cache is uninitialized or the coordinate is invalid.
     */
    [[nodiscard]] eve::Result<void> recordFeedback(int pageX, int pageY, int mipLevel = 0);

    /**
     * @brief Record the page covering normalized UV (fract).
     * @return Failure when the physical cache is uninitialized.
     */
    [[nodiscard]] eve::Result<void> recordFeedbackUv(float u, float v, int mipLevel = 0);

    /**
     * @brief Ingest a packed feedback buffer (UE-style page IDs).
     * @cost Cheap relative to update(); copies and retains samples up to feedbackCapacity.
     * @amortized Per feedback readback / frame ingest.
     */
    [[nodiscard]] eve::Result<void> ingestPackedFeedback(std::span<const std::uint32_t> packed);

    /**
     * @brief Record every cell centre of a low-resolution UV grid (CPU feedback stand-in).
     * @cost Linear in gridW*gridH retained samples; still cheaper than page decode.
     * @amortized Per simulated feedback pass.
     */
    [[nodiscard]] eve::Result<void> recordFeedbackGrid(int gridW, int gridH, int mipLevel = 0);

    /**
     * @brief Mark resident pages covering a UV rectangle dirty so the next update re-decodes them.
     * @return Failure when uninitialized or the rectangle is empty/non-finite.
     */
    [[nodiscard]] eve::Result<void> invalidateRect(float u0, float v0, float u1, float v1, int mipLevel = 0);

    /** @brief Mark one page dirty for writer-style attribute edits. */
    [[nodiscard]] eve::Result<void> invalidatePage(int pageX, int pageY, int mipLevel = 0);

    /**
     * @brief Compact feedback, decode up to budget, and stage an incremental upload batch.
     * @cost Expensive relative to recordFeedback: page compose plus atlas/page-table staging.
     * @amortized Per streaming update / frame.
     * @return Snapshot stats after the tick; failure leaves the previous published mapping intact.
     */
    [[nodiscard]] eve::Result<VirtualTextureStreamingStats> update();

    /**
     * @brief Take the staged upload batch for GPU application.
     * @return Failure when nothing is pending or a previous batch was not committed/abandoned.
     */
    [[nodiscard]] eve::Result<VirtualTextureUploadBatch> takePendingUploads();

    /**
     * @brief Publish working state after successful GPU uploads (bumps generation).
     * @param expectedGeneration Must equal the batch's expectedGeneration.
     */
    [[nodiscard]] eve::Result<void> commitPublish(std::uint64_t expectedGeneration);

    /**
     * @brief Discard staged working changes and restore the last published mapping.
     * @return Failure when there is no pending batch to abandon.
     */
    [[nodiscard]] eve::Result<void> abandonPublish();

    /**
     * @brief Create or refresh GPU textures and bind them to a material.
     * @cost Expensive on first call (full atlas upload); later calls apply pending regions only.
     * @amortized Per material sync after update().
     */
    [[nodiscard]] eve::Result<void> syncToMaterial(Graphics& gfx, Material& material);

    /** @brief Snapshot counters. */
    VirtualTextureStreamingStats stats() const;

    /** @brief Published albedo atlas (valid after initializePhysicalCache). */
    const image::ImageData& albedoAtlas() const;
    /** @brief Published normal atlas. */
    const image::ImageData& normalAtlas() const;
    /** @brief Published page table. */
    const image::ImageData& pageTable() const;

    /** @brief Atlas slots x. */
    int atlasSlotsX() const { return atlasSlotsX_; }
    /** @brief Atlas slots y. */
    int atlasSlotsY() const { return atlasSlotsY_; }
    /** @brief Page extent including gutter. */
    int pageExtent() const { return pageExtent_; }
    /** @brief Border texels / pageExtent. */
    float borderFraction() const { return borderFraction_; }
    /** @brief Mip-zero page count x. */
    int pageCountX() const;
    /** @brief Mip-zero page count y. */
    int pageCountY() const;
    /** @brief Published generation. */
    std::uint64_t generation() const { return generation_; }
    /** @brief Borrowed producer. */
    VirtualTextureBlend& producer() { return *producer_; }
    /** @brief Borrowed producer. */
    const VirtualTextureBlend& producer() const { return *producer_; }

private:
    struct SlotRecord {
        bool                     occupied = false;
        bool                     dirty    = false;
        int                      pageX    = -1;
        int                      pageY    = -1;
        std::list<int>::iterator lru;
    };
    struct PageKey {
        int x = 0;
        int y = 0;
        /** @brief Operator ==. */
        bool operator==(const PageKey& other) const = default;
    };
    struct PageKeyHash {
        /** @brief Operator (). */
        std::size_t operator()(const PageKey& key) const noexcept;
    };
    struct PhysicalState {
        std::unique_ptr<image::ImageData>             albedo;
        std::unique_ptr<image::ImageData>             normal;
        std::unique_ptr<image::ImageData>             pageTable;
        std::vector<SlotRecord>                       slots;
        std::list<int>                                lru;  // physical slot indices, front = most recent
        std::unordered_map<PageKey, int, PageKeyHash> pageToSlot;
    };

    explicit VirtualTextureStreaming(VirtualTextureStreamingConfig        config,
                                     std::unique_ptr<VirtualTextureBlend> producer);

    [[nodiscard]] eve::Result<void>                 ensureInitialized() const;
    [[nodiscard]] eve::Result<void>                 validatePage(int pageX, int pageY, int mipLevel) const;
    void                                            touchSlot(PhysicalState& state, int slot);
    [[nodiscard]] eve::Result<int>                  allocateSlot(PhysicalState&                           state,
                                                                 std::vector<VirtualTextureRegionPixels>& pageTableClears);
    [[nodiscard]] eve::Result<void>                 decodeIntoSlot(PhysicalState& state, int slot, int pageX, int pageY,
                                                                   std::vector<VirtualTextureRegionPixels>& albedoRegions,
                                                                   std::vector<VirtualTextureRegionPixels>& normalRegions,
                                                                   std::vector<VirtualTextureRegionPixels>& pageTableSets);
    [[nodiscard]] static VirtualTextureRegionPixels copyRegion(const image::ImageData& image, int x, int y, int w,
                                                               int h);
    [[nodiscard]] static VirtualTextureRegionPixels copyPageTableTexel(const image::ImageData& table, int pageX,
                                                                       int pageY);
    void                                            clearPageMapping(PhysicalState& state, int pageX, int pageY);
    PhysicalState                                   cloneState(const PhysicalState& source) const;

    VirtualTextureStreamingConfig             config_;
    std::unique_ptr<VirtualTextureBlend>      producer_;
    bool                                      initialized_    = false;
    int                                       atlasSlotsX_    = 0;
    int                                       atlasSlotsY_    = 0;
    int                                       pageExtent_     = 0;
    float                                     borderFraction_ = 0.f;
    std::uint64_t                             generation_     = 0;
    PhysicalState                             published_;
    PhysicalState                             working_;
    std::optional<VirtualTextureUploadBatch>  pending_;
    std::vector<VirtualTextureFeedbackSample> feedback_;
    VirtualTextureStreamingStats              lifetime_{};

    Texture* gpuAlbedo_    = nullptr;
    Texture* gpuNormal_    = nullptr;
    Texture* gpuPageTable_ = nullptr;
};

}  // namespace eve::graphics
