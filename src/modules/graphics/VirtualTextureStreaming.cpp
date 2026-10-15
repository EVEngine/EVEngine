#include "graphics/VirtualTextureStreaming.h"

#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Texture.h"
#include "graphics/TextureSampler.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace eve::graphics {
namespace {

eve::Diagnostic invalid(std::string message) {
    return eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message);
}

eve::Diagnostic failed(std::string message) { return eve::Diagnostic::error(eve::DiagnosticCode::Failed, message); }

float wrap01(float value) {
    value -= std::floor(value);
    return value < 0.f ? value + 1.f : value;
}

using Pixel = image::ImageData::Colorf;

Pixel sampleRepeat(const image::ImageData& image, float u, float v) {
    const int   width  = image.getWidth();
    const int   height = image.getHeight();
    const float x      = wrap01(u) * float(width) - 0.5f;
    const float y      = wrap01(v) * float(height) - 0.5f;
    const int   x0     = int(std::floor(x));
    const int   y0     = int(std::floor(y));
    const float tx     = x - float(x0);
    const float ty     = y - float(y0);
    auto        pixel  = [&](int px, int py) {
        px = (px % width + width) % width;
        py = (py % height + height) % height;
        return image.getPixel(px, py);
    };
    const Pixel a   = pixel(x0, y0);
    const Pixel b   = pixel(x0 + 1, y0);
    const Pixel c   = pixel(x0, y0 + 1);
    const Pixel d   = pixel(x0 + 1, y0 + 1);
    auto        mix = [tx, ty](float av, float bv, float cv, float dv) {
        return (av + (bv - av) * tx) + ((cv + (dv - cv) * tx) - (av + (bv - av) * tx)) * ty;
    };
    return Pixel{mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a)};
}

}  // namespace

std::size_t VirtualTextureStreaming::PageKeyHash::operator()(const PageKey& key) const noexcept {
    std::size_t seed = std::hash<int>{}(key.x);
    seed ^= std::hash<int>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
    return seed;
}

eve::Result<std::unique_ptr<VirtualTextureStreaming>> VirtualTextureStreaming::create(
    const VirtualTextureStreamingConfig& config) {
    if (config.maxDecodesPerUpdate == 0)
        return eve::Result<std::unique_ptr<VirtualTextureStreaming>>::failure(
            invalid("virtual texture streaming maxDecodesPerUpdate must be positive"));
    if (config.feedbackCapacity == 0)
        return eve::Result<std::unique_ptr<VirtualTextureStreaming>>::failure(
            invalid("virtual texture streaming feedbackCapacity must be positive"));
    auto producer = VirtualTextureBlend::create(config.blend);
    if (!producer.ok()) return eve::Result<std::unique_ptr<VirtualTextureStreaming>>::failure(producer.status());
    return eve::Result<std::unique_ptr<VirtualTextureStreaming>>::success(
        std::unique_ptr<VirtualTextureStreaming>(new VirtualTextureStreaming(config, std::move(producer).value())));
}

VirtualTextureStreaming::VirtualTextureStreaming(VirtualTextureStreamingConfig        config,
                                                 std::unique_ptr<VirtualTextureBlend> producer)
    : config_(std::move(config)), producer_(std::move(producer)) {}

eve::Result<std::uint32_t> VirtualTextureStreaming::addLayer(const VirtualTextureBlendLayer& layer) {
    auto result = producer_->addLayer(layer);
    if (!result.ok()) return result;
    if (initialized_) {
        // Layer edits invalidate every produced page; force re-decode of resident mappings.
        for (auto& slot : working_.slots) {
            if (slot.occupied) slot.dirty = true;
        }
        for (auto& slot : published_.slots) {
            if (slot.occupied) slot.dirty = true;
        }
        ++lifetime_.pagesInvalidated;
    }
    return result;
}

eve::Result<void> VirtualTextureStreaming::initializePhysicalCache() {
    if (producer_->pageCountX() <= 0 || producer_->pageCountY() <= 0)
        return eve::Result<void>::failure(invalid("virtual texture producer has no pages"));
    auto bootstrap = producer_->buildResidentSet({});
    if (!bootstrap.ok()) return eve::Result<void>::failure(bootstrap.status());

    const auto& set     = bootstrap.value();
    pageExtent_         = set.pageExtent;
    borderFraction_     = set.borderFraction;
    atlasSlotsX_        = set.atlasSlotsX;
    atlasSlotsY_        = set.atlasSlotsY;
    const int slotCount = atlasSlotsX_ * atlasSlotsY_;

    auto makeState = [&](PhysicalState& state) {
        state.albedo    = std::make_unique<image::ImageData>(*set.albedoAtlas);
        state.normal    = std::make_unique<image::ImageData>(*set.normalAtlas);
        state.pageTable = std::make_unique<image::ImageData>(*set.pageTable);
        state.slots.assign(std::size_t(slotCount), SlotRecord{});
        state.lru.clear();
        state.pageToSlot.clear();
        // Slot 0 is reserved for the whole-surface substitute and is never LRU-managed.
        state.slots[0].occupied = true;
        state.slots[0].pageX    = -1;
        state.slots[0].pageY    = -1;
    };
    makeState(published_);
    makeState(working_);
    pending_.reset();
    gpuAlbedo_ = gpuNormal_ = gpuPageTable_ = nullptr;
    generation_                             = 0;
    initialized_                            = true;
    lifetime_                               = {};
    lifetime_.generation                    = 0;
    lifetime_.residentPages                 = 0;
    return eve::Result<void>::success();
}

std::uint32_t VirtualTextureStreaming::packFeedback(int pageX, int pageY, int mipLevel) noexcept {
    return (std::uint32_t(pageX) & 0xFFFu) | ((std::uint32_t(pageY) & 0xFFFu) << 12u) |
           ((std::uint32_t(mipLevel) & 0xFFu) << 24u);
}

eve::Result<VirtualTextureFeedbackSample> VirtualTextureStreaming::unpackFeedback(std::uint32_t packed) {
    VirtualTextureFeedbackSample sample;
    sample.pageX    = int(packed & 0xFFFu);
    sample.pageY    = int((packed >> 12u) & 0xFFFu);
    sample.mipLevel = int((packed >> 24u) & 0xFFu);
    if (sample.pageX < 0 || sample.pageY < 0 || sample.mipLevel < 0)
        return eve::Result<VirtualTextureFeedbackSample>::failure(
            invalid("packed virtual texture feedback fields are out of range"));
    return eve::Result<VirtualTextureFeedbackSample>::success(sample);
}

eve::Result<void> VirtualTextureStreaming::ensureInitialized() const {
    if (!initialized_) return eve::Result<void>::failure(invalid("virtual texture streaming is not initialized"));
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::validatePage(int pageX, int pageY, int mipLevel) const {
    if (mipLevel != 0)
        return eve::Result<void>::failure(invalid("virtual texture streaming currently residencies mip zero only"));
    if (pageX < 0 || pageY < 0 || pageX >= pageCountX() || pageY >= pageCountY())
        return eve::Result<void>::failure(invalid("virtual texture page coordinate is outside mip zero"));
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::recordFeedback(int pageX, int pageY, int mipLevel) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;
    auto valid = validatePage(pageX, pageY, mipLevel);
    if (!valid.ok()) return valid;
    if (feedback_.size() >= config_.feedbackCapacity) feedback_.erase(feedback_.begin());
    feedback_.push_back(VirtualTextureFeedbackSample{pageX, pageY, mipLevel});
    ++lifetime_.feedbackSamples;
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::recordFeedbackUv(float u, float v, int mipLevel) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;
    if (!std::isfinite(u) || !std::isfinite(v))
        return eve::Result<void>::failure(invalid("virtual texture feedback UV must be finite"));
    const int pageX = int(std::floor(wrap01(u) * float(pageCountX())));
    const int pageY = int(std::floor(wrap01(v) * float(pageCountY())));
    return recordFeedback(std::min(pageX, pageCountX() - 1), std::min(pageY, pageCountY() - 1), mipLevel);
}

eve::Result<void> VirtualTextureStreaming::ingestPackedFeedback(std::span<const std::uint32_t> packed) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;
    for (std::uint32_t word : packed) {
        auto decoded = unpackFeedback(word);
        if (!decoded.ok()) continue;
        auto sample = decoded.value();
        // Higher mips collapse onto their mip-zero parents for the current physical pool.
        if (sample.mipLevel != 0) {
            sample.pageX >>= sample.mipLevel;
            sample.pageY >>= sample.mipLevel;
            sample.mipLevel = 0;
        }
        auto recorded = recordFeedback(sample.pageX, sample.pageY, sample.mipLevel);
        if (!recorded.ok()) return recorded;
    }
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::recordFeedbackGrid(int gridW, int gridH, int mipLevel) {
    if (gridW <= 0 || gridH <= 0)
        return eve::Result<void>::failure(invalid("feedback grid dimensions must be positive"));
    for (int y = 0; y < gridH; ++y) {
        for (int x = 0; x < gridW; ++x) {
            const float u        = (float(x) + 0.5f) / float(gridW);
            const float v        = (float(y) + 0.5f) / float(gridH);
            auto        recorded = recordFeedbackUv(u, v, mipLevel);
            if (!recorded.ok()) return recorded;
        }
    }
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::invalidatePage(int pageX, int pageY, int mipLevel) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;
    auto valid = validatePage(pageX, pageY, mipLevel);
    if (!valid.ok()) return valid;
    auto found = working_.pageToSlot.find(PageKey{pageX, pageY});
    if (found == working_.pageToSlot.end()) return eve::Result<void>::success();
    working_.slots[std::size_t(found->second)].dirty = true;
    ++lifetime_.pagesInvalidated;
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::invalidateRect(float u0, float v0, float u1, float v1, int mipLevel) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;
    if (!std::isfinite(u0) || !std::isfinite(v0) || !std::isfinite(u1) || !std::isfinite(v1))
        return eve::Result<void>::failure(invalid("invalidate rect coordinates must be finite"));
    if (u1 < u0) std::swap(u0, u1);
    if (v1 < v0) std::swap(v0, v1);
    const int minX = std::clamp(int(std::floor(wrap01(u0) * float(pageCountX()))), 0, pageCountX() - 1);
    const int maxX = std::clamp(int(std::floor(wrap01(u1) * float(pageCountX()))), 0, pageCountX() - 1);
    const int minY = std::clamp(int(std::floor(wrap01(v0) * float(pageCountY()))), 0, pageCountY() - 1);
    const int maxY = std::clamp(int(std::floor(wrap01(v1) * float(pageCountY()))), 0, pageCountY() - 1);
    for (int y = minY; y <= maxY; ++y)
        for (int x = minX; x <= maxX; ++x) {
            auto invalidated = invalidatePage(x, y, mipLevel);
            if (!invalidated.ok()) return invalidated;
        }
    return eve::Result<void>::success();
}

void VirtualTextureStreaming::touchSlot(PhysicalState& state, int slot) {
    auto& record = state.slots[std::size_t(slot)];
    if (!record.occupied || slot == 0) return;
    state.lru.splice(state.lru.begin(), state.lru, record.lru);
}

void VirtualTextureStreaming::clearPageMapping(PhysicalState& state, int pageX, int pageY) {
    const Pixel fallback{(0.5f) / float(atlasSlotsX_), (0.5f) / float(atlasSlotsY_), 0.f, 1.f};
    state.pageTable->setPixel(pageX, pageY, fallback);
}

VirtualTextureRegionPixels VirtualTextureStreaming::copyRegion(const image::ImageData& image, int x, int y, int w,
                                                               int h) {
    VirtualTextureRegionPixels region;
    region.x      = x;
    region.y      = y;
    region.width  = w;
    region.height = h;
    region.rgba.resize(std::size_t(w) * std::size_t(h) * 4u);
    const auto* src       = static_cast<const std::uint8_t*>(image.getData());
    const int   srcStride = image.getWidth() * 4;
    for (int row = 0; row < h; ++row) {
        std::memcpy(region.rgba.data() + std::size_t(row) * std::size_t(w) * 4u, src + (y + row) * srcStride + x * 4,
                    std::size_t(w) * 4u);
    }
    return region;
}

VirtualTextureRegionPixels VirtualTextureStreaming::copyPageTableTexel(const image::ImageData& table, int pageX,
                                                                       int pageY) {
    return copyRegion(table, pageX, pageY, 1, 1);
}

eve::Result<int> VirtualTextureStreaming::allocateSlot(PhysicalState&                           state,
                                                       std::vector<VirtualTextureRegionPixels>& pageTableClears) {
    for (int slot = 1; slot < int(state.slots.size()); ++slot) {
        if (!state.slots[std::size_t(slot)].occupied) {
            state.slots[std::size_t(slot)].occupied = true;
            state.lru.push_front(slot);
            state.slots[std::size_t(slot)].lru = state.lru.begin();
            return eve::Result<int>::success(slot);
        }
    }
    if (state.lru.empty())
        return eve::Result<int>::failure(failed("virtual texture physical cache has no evictable slots"));
    const int victim = state.lru.back();
    state.lru.pop_back();
    auto& record = state.slots[std::size_t(victim)];
    if (record.occupied && record.pageX >= 0 && record.pageY >= 0) {
        clearPageMapping(state, record.pageX, record.pageY);
        pageTableClears.push_back(copyPageTableTexel(*state.pageTable, record.pageX, record.pageY));
        state.pageToSlot.erase(PageKey{record.pageX, record.pageY});
        ++lifetime_.pagesEvicted;
    }
    record.occupied = true;
    record.dirty    = false;
    record.pageX    = -1;
    record.pageY    = -1;
    state.lru.push_front(victim);
    record.lru = state.lru.begin();
    return eve::Result<int>::success(victim);
}

eve::Result<void> VirtualTextureStreaming::decodeIntoSlot(PhysicalState& state, int slot, int pageX, int pageY,
                                                          std::vector<VirtualTextureRegionPixels>& albedoRegions,
                                                          std::vector<VirtualTextureRegionPixels>& normalRegions,
                                                          std::vector<VirtualTextureRegionPixels>& pageTableSets) {
    auto albedo = producer_->requestAlbedoPage(pageX, pageY, 0);
    if (!albedo.ok()) return eve::Result<void>::failure(albedo.status());
    auto normal = producer_->requestNormalPage(pageX, pageY, 0);
    if (!normal.ok()) return eve::Result<void>::failure(normal.status());

    const int slotX = slot % atlasSlotsX_;
    const int slotY = slot / atlasSlotsX_;
    const int dstX  = slotX * pageExtent_;
    const int dstY  = slotY * pageExtent_;
    state.albedo->paste(const_cast<image::ImageData*>(albedo.value().get()), dstX, dstY, 0, 0, pageExtent_,
                        pageExtent_);
    state.normal->paste(const_cast<image::ImageData*>(normal.value().get()), dstX, dstY, 0, 0, pageExtent_,
                        pageExtent_);
    albedoRegions.push_back(copyRegion(*state.albedo, dstX, dstY, pageExtent_, pageExtent_));
    normalRegions.push_back(copyRegion(*state.normal, dstX, dstY, pageExtent_, pageExtent_));

    auto& record                            = state.slots[std::size_t(slot)];
    record.pageX                            = pageX;
    record.pageY                            = pageY;
    record.dirty                            = false;
    record.occupied                         = true;
    state.pageToSlot[PageKey{pageX, pageY}] = slot;
    touchSlot(state, slot);

    const Pixel entry{(float(slotX) + 0.5f) / float(atlasSlotsX_), (float(slotY) + 0.5f) / float(atlasSlotsY_), 1.f,
                      1.f};
    state.pageTable->setPixel(pageX, pageY, entry);
    pageTableSets.push_back(copyPageTableTexel(*state.pageTable, pageX, pageY));
    ++lifetime_.pagesDecoded;
    return eve::Result<void>::success();
}

eve::Result<VirtualTextureStreamingStats> VirtualTextureStreaming::update() {
    auto ready = ensureInitialized();
    if (!ready.ok()) return eve::Result<VirtualTextureStreamingStats>::failure(ready.status());
    if (pending_.has_value())
        return eve::Result<VirtualTextureStreamingStats>::failure(
            failed("commit or abandon the pending virtual texture upload before update"));

    // Compact + dedupe feedback (UE feedback analyze).
    std::unordered_map<PageKey, int, PageKeyHash> unique;
    unique.reserve(feedback_.size());
    for (const auto& sample : feedback_) {
        if (sample.mipLevel != 0) continue;
        ++unique[PageKey{sample.pageX, sample.pageY}];
    }
    feedback_.clear();
    lifetime_.uniqueFeedback += unique.size();
    lifetime_.pendingFeedback = 0;

    std::vector<std::pair<PageKey, int>> ordered(unique.begin(), unique.end());
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        if (a.first.y != b.first.y) return a.first.y < b.first.y;
        return a.first.x < b.first.x;
    });

    VirtualTextureUploadBatch batch;
    batch.expectedGeneration = generation_;
    std::size_t decoded      = 0;

    for (const auto& [page, weight] : ordered) {
        (void)weight;
        auto found = working_.pageToSlot.find(page);
        if (found != working_.pageToSlot.end()) {
            touchSlot(working_, found->second);
            if (!working_.slots[std::size_t(found->second)].dirty) continue;
            if (decoded >= config_.maxDecodesPerUpdate) break;
            auto decodedOk = decodeIntoSlot(working_, found->second, page.x, page.y, batch.albedoRegions,
                                            batch.normalRegions, batch.pageTableSets);
            if (!decodedOk.ok()) return eve::Result<VirtualTextureStreamingStats>::failure(decodedOk.status());
            ++decoded;
            continue;
        }
        if (decoded >= config_.maxDecodesPerUpdate) break;
        auto slot = allocateSlot(working_, batch.pageTableClears);
        if (!slot.ok()) return eve::Result<VirtualTextureStreamingStats>::failure(slot.status());
        auto decodedOk = decodeIntoSlot(working_, slot.value(), page.x, page.y, batch.albedoRegions,
                                        batch.normalRegions, batch.pageTableSets);
        if (!decodedOk.ok()) return eve::Result<VirtualTextureStreamingStats>::failure(decodedOk.status());
        ++decoded;
    }

    // Also process dirty residents that were not in this frame's feedback.
    if (decoded < config_.maxDecodesPerUpdate) {
        for (int slot = 1; slot < int(working_.slots.size()) && decoded < config_.maxDecodesPerUpdate; ++slot) {
            auto& record = working_.slots[std::size_t(slot)];
            if (!record.occupied || !record.dirty || record.pageX < 0) continue;
            auto decodedOk = decodeIntoSlot(working_, slot, record.pageX, record.pageY, batch.albedoRegions,
                                            batch.normalRegions, batch.pageTableSets);
            if (!decodedOk.ok()) return eve::Result<VirtualTextureStreamingStats>::failure(decodedOk.status());
            ++decoded;
        }
    }

    const bool hasWork = !batch.pageTableClears.empty() || !batch.albedoRegions.empty() ||
                         !batch.normalRegions.empty() || !batch.pageTableSets.empty();
    if (hasWork) {
        pending_                 = std::move(batch);
        lifetime_.pendingUploads = pending_->pageTableClears.size() + pending_->albedoRegions.size() +
                                   pending_->normalRegions.size() + pending_->pageTableSets.size();
    } else {
        lifetime_.pendingUploads = 0;
    }
    lifetime_.generation    = generation_;
    lifetime_.residentPages = working_.pageToSlot.size();
    return eve::Result<VirtualTextureStreamingStats>::success(stats());
}

eve::Result<VirtualTextureUploadBatch> VirtualTextureStreaming::takePendingUploads() {
    if (!pending_.has_value())
        return eve::Result<VirtualTextureUploadBatch>::failure(failed("no pending virtual texture upload batch"));
    VirtualTextureUploadBatch batch = std::move(*pending_);
    // Keep the optional engaged with an empty marker? We need commit/abandon to know a batch was taken.
    // Store a sentinel empty batch with the same expectedGeneration.
    pending_                     = VirtualTextureUploadBatch{};
    pending_->expectedGeneration = batch.expectedGeneration;
    lifetime_.pendingUploads     = 0;
    return eve::Result<VirtualTextureUploadBatch>::success(std::move(batch));
}

eve::Result<void> VirtualTextureStreaming::commitPublish(std::uint64_t expectedGeneration) {
    if (!pending_.has_value())
        return eve::Result<void>::failure(failed("no pending virtual texture publish to commit"));
    if (pending_->expectedGeneration != expectedGeneration)
        return eve::Result<void>::failure(failed("virtual texture publish generation mismatch"));
    published_ = cloneState(working_);
    pending_.reset();
    ++generation_;
    ++lifetime_.publishes;
    lifetime_.generation     = generation_;
    lifetime_.residentPages  = published_.pageToSlot.size();
    lifetime_.pendingUploads = 0;
    return eve::Result<void>::success();
}

eve::Result<void> VirtualTextureStreaming::abandonPublish() {
    if (!pending_.has_value())
        return eve::Result<void>::failure(failed("no pending virtual texture publish to abandon"));
    working_ = cloneState(published_);
    pending_.reset();
    ++lifetime_.abandonedPublishes;
    lifetime_.pendingUploads = 0;
    lifetime_.residentPages  = published_.pageToSlot.size();
    return eve::Result<void>::success();
}

VirtualTextureStreaming::PhysicalState VirtualTextureStreaming::cloneState(const PhysicalState& source) const {
    PhysicalState copy;
    copy.albedo     = std::make_unique<image::ImageData>(*source.albedo);
    copy.normal     = std::make_unique<image::ImageData>(*source.normal);
    copy.pageTable  = std::make_unique<image::ImageData>(*source.pageTable);
    copy.slots      = source.slots;
    copy.pageToSlot = source.pageToSlot;
    copy.lru.clear();
    // Rebuild LRU iterators against the new list.
    for (int slot : source.lru) {
        copy.lru.push_back(slot);
        copy.slots[std::size_t(slot)].lru = std::prev(copy.lru.end());
    }
    // Slot 0 has no LRU entry.
    if (!copy.slots.empty()) copy.slots[0].lru = copy.lru.end();
    return copy;
}

VirtualTextureStreamingStats VirtualTextureStreaming::stats() const {
    VirtualTextureStreamingStats out = lifetime_;
    out.generation                   = generation_;
    out.residentPages                = initialized_ ? published_.pageToSlot.size() : 0;
    out.pendingFeedback              = feedback_.size();
    out.pendingUploads = pending_.has_value() ? (pending_->pageTableClears.size() + pending_->albedoRegions.size() +
                                                 pending_->normalRegions.size() + pending_->pageTableSets.size())
                                              : 0;
    return out;
}

const image::ImageData& VirtualTextureStreaming::albedoAtlas() const { return *published_.albedo; }
const image::ImageData& VirtualTextureStreaming::normalAtlas() const { return *published_.normal; }
const image::ImageData& VirtualTextureStreaming::pageTable() const { return *published_.pageTable; }

int VirtualTextureStreaming::pageCountX() const { return producer_->pageCountX(); }
int VirtualTextureStreaming::pageCountY() const { return producer_->pageCountY(); }

namespace {
eve::Result<void> uploadRegions(Graphics& gfx, Texture* texture,
                                const std::vector<VirtualTextureRegionPixels>& regions) {
    if (!texture) return eve::Result<void>::failure(invalid("virtual texture GPU texture is null"));
    for (const auto& region : regions) {
        TextureRegionUpload upload;
        upload.x           = region.x;
        upload.y           = region.y;
        upload.width       = region.width;
        upload.height      = region.height;
        upload.rgba        = std::span<const std::uint8_t>(region.rgba.data(), region.rgba.size());
        upload.bytesPerRow = std::size_t(region.width) * 4u;
        auto status        = gfx.updateTextureRegions(texture, std::span<const TextureRegionUpload>(&upload, 1));
        if (!status.ok()) return status;
    }
    return eve::Result<void>::success();
}
}  // namespace

eve::Result<void> VirtualTextureStreaming::syncToMaterial(Graphics& gfx, Material& material) {
    auto ready = ensureInitialized();
    if (!ready.ok()) return ready;

    if (!gpuAlbedo_ || !gpuNormal_ || !gpuPageTable_) {
        TextureCreateInfo info{};
        info.generateMipmaps = false;
        gpuAlbedo_           = gfx.newTexture(const_cast<image::ImageData*>(published_.albedo.get()), info);
        gpuNormal_           = gfx.newTexture(const_cast<image::ImageData*>(published_.normal.get()), info);
        gpuPageTable_        = gfx.newTexture(const_cast<image::ImageData*>(published_.pageTable.get()), info);
        if (!gpuAlbedo_ || !gpuNormal_ || !gpuPageTable_)
            return eve::Result<void>::failure(failed("failed to create virtual texture GPU atlases"));
        auto bound = material.setVirtualTexture(gpuAlbedo_, gpuNormal_, gpuPageTable_, pageCountX(), pageCountY(),
                                                atlasSlotsX_, atlasSlotsY_, borderFraction_);
        if (!bound.ok()) return bound;
    }

    if (!pending_.has_value()) return eve::Result<void>::success();

    auto batchResult = takePendingUploads();
    if (!batchResult.ok()) return eve::Result<void>::failure(batchResult.status());
    auto batch = std::move(batchResult).value();

    auto restoreGpuFromPublished = [&]() -> eve::Result<void> {
        TextureRegionUpload albedoUpload;
        albedoUpload.width  = published_.albedo->getWidth();
        albedoUpload.height = published_.albedo->getHeight();
        albedoUpload.rgba   = std::span<const std::uint8_t>(
            static_cast<const std::uint8_t*>(published_.albedo->getData()), published_.albedo->getSize());
        albedoUpload.bytesPerRow = std::size_t(albedoUpload.width) * 4u;
        auto albedoStatus =
            gfx.updateTextureRegions(gpuAlbedo_, std::span<const TextureRegionUpload>(&albedoUpload, 1));
        if (!albedoStatus.ok()) return albedoStatus;
        TextureRegionUpload normalUpload = albedoUpload;
        normalUpload.rgba                = std::span<const std::uint8_t>(
            static_cast<const std::uint8_t*>(published_.normal->getData()), published_.normal->getSize());
        auto normalStatus =
            gfx.updateTextureRegions(gpuNormal_, std::span<const TextureRegionUpload>(&normalUpload, 1));
        if (!normalStatus.ok()) return normalStatus;
        TextureRegionUpload tableUpload = albedoUpload;
        tableUpload.width               = published_.pageTable->getWidth();
        tableUpload.height              = published_.pageTable->getHeight();
        tableUpload.rgba                = std::span<const std::uint8_t>(
            static_cast<const std::uint8_t*>(published_.pageTable->getData()), published_.pageTable->getSize());
        tableUpload.bytesPerRow = std::size_t(tableUpload.width) * 4u;
        return gfx.updateTextureRegions(gpuPageTable_, std::span<const TextureRegionUpload>(&tableUpload, 1));
    };

    auto clears = uploadRegions(gfx, gpuPageTable_, batch.pageTableClears);
    if (!clears.ok()) {
        (void)abandonPublish();
        (void)restoreGpuFromPublished();
        return clears;
    }
    auto albedo = uploadRegions(gfx, gpuAlbedo_, batch.albedoRegions);
    if (!albedo.ok()) {
        (void)abandonPublish();
        (void)restoreGpuFromPublished();
        return albedo;
    }
    auto normal = uploadRegions(gfx, gpuNormal_, batch.normalRegions);
    if (!normal.ok()) {
        (void)abandonPublish();
        (void)restoreGpuFromPublished();
        return normal;
    }
    auto sets = uploadRegions(gfx, gpuPageTable_, batch.pageTableSets);
    if (!sets.ok()) {
        (void)abandonPublish();
        (void)restoreGpuFromPublished();
        return sets;
    }
    return commitPublish(batch.expectedGeneration);
}

}  // namespace eve::graphics
