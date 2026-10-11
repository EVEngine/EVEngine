#include "procgen/heightmap/TerrainBakedMaskCache.h"

#include <algorithm>
#include <utility>
#include <vector>
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

namespace eve::procgen {
namespace {
constexpr size_t kMaxIdentityLength = 512;
bool validIdentity(const std::string& value) { return !value.empty() && value.size() <= kMaxIdentityLength; }
Result<int> invalid(const char* message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message));
}
Result<int> missing(const char* message) {
    return Result<int>::failure(Diagnostic::error(DiagnosticCode::NotFound, message));
}
}  // namespace

struct TerrainBakedMaskCache::Impl {
    struct Entry {
        std::string terrainId;
        std::string maskGuid;
        Heightmap   raster;
        bool        dirty = false;
    };
    std::vector<Entry> entries;
};

TerrainBakedMaskCache::TerrainBakedMaskCache() : impl_(std::make_unique<Impl>()) {}
TerrainBakedMaskCache::~TerrainBakedMaskCache() = default;
TerrainBakedMaskCache::TerrainBakedMaskCache(TerrainBakedMaskCache&&) noexcept = default;
TerrainBakedMaskCache& TerrainBakedMaskCache::operator=(TerrainBakedMaskCache&&) noexcept = default;

Result<int> TerrainBakedMaskCache::store(const std::string& terrainId, const std::string& maskGuid,
                                         const Heightmap& raster) {
    using namespace raster_detail;
    if (!impl_ || !validIdentity(terrainId) || !validIdentity(maskGuid) || !validRaster(raster))
        return invalid("terrain.bakedMask.store: stable bounded identities and finite raster required");
    auto candidate = impl_->entries;
    auto it = std::find_if(candidate.begin(), candidate.end(), [&](const auto& entry) {
        return entry.terrainId == terrainId && entry.maskGuid == maskGuid;
    });
    if (it == candidate.end()) candidate.push_back({terrainId, maskGuid, raster, false});
    else {
        it->raster = raster;
        it->dirty  = false;
    }
    impl_->entries.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->entries.size()));
}

Result<int> TerrainBakedMaskCache::markDirty(const std::string& maskGuid) {
    if (!impl_ || !validIdentity(maskGuid)) return invalid("terrain.bakedMask.markDirty: bounded mask GUID required");
    int changed = 0;
    for (auto& entry : impl_->entries)
        if (entry.maskGuid == maskGuid && !entry.dirty) {
            entry.dirty = true;
            ++changed;
        }
    return Result<int>::success(changed);
}

Result<int> TerrainBakedMaskCache::erase(const std::string& terrainId, const std::string& maskGuid) {
    if (!impl_ || !validIdentity(terrainId) || !validIdentity(maskGuid))
        return invalid("terrain.bakedMask.erase: bounded terrain and mask identities required");
    const auto oldSize = impl_->entries.size();
    std::erase_if(impl_->entries, [&](const auto& entry) {
        return entry.terrainId == terrainId && entry.maskGuid == maskGuid;
    });
    if (impl_->entries.size() == oldSize) return missing("terrain.bakedMask.erase: entry not found");
    return Result<int>::success(1);
}

void TerrainBakedMaskCache::clear() {
    if (impl_) impl_->entries.clear();
}
int TerrainBakedMaskCache::getEntryCount() const noexcept {
    return impl_ ? static_cast<int>(impl_->entries.size()) : 0;
}

Result<int> TerrainBakedMaskCache::copyMask(const std::string& terrainId, const std::string& maskGuid,
                                            Heightmap& output) const {
    using namespace raster_detail;
    if (!impl_ || !validIdentity(terrainId) || !validIdentity(maskGuid) || !validRaster(output))
        return invalid("terrain.bakedMask.copy: bounded identities and finite output required");
    const auto it = std::find_if(impl_->entries.begin(), impl_->entries.end(), [&](const auto& entry) {
        return entry.terrainId == terrainId && entry.maskGuid == maskGuid;
    });
    if (it == impl_->entries.end()) return missing("terrain.bakedMask.copy: entry not found");
    if (it->dirty) return missing("terrain.bakedMask.copy: entry is dirty and provider rebuild is required");
    std::vector<float> candidate(output.data().size());
    for (int z = 0; z < output.getHeight(); ++z)
        for (int x = 0; x < output.getWidth(); ++x)
            candidate[size_t(z) * output.getWidth() + x] =
                static_cast<float>(textureSample(it->raster, (x + 0.5) / output.getWidth(),
                                                 (z + 0.5) / output.getHeight()));
    return publish(output, std::move(candidate));
}
}  // namespace eve::procgen
