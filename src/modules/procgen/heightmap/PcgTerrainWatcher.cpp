#include "procgen/heightmap/PcgTerrainWatcher.h"

#include <algorithm>

namespace eve::procgen {
namespace {
template <class T>
Result<T> watcherFailure(const char* message) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, message,
                                                "procgen.pcgTerrainWatcher"));
}
}  // namespace

void PcgTerrainWatcher::beginScan() {
    candidate_.clear();
    scanning_ = true;
}

Result<int> PcgTerrainWatcher::addTerrain(const std::string& terrainId) {
    if (!scanning_) return watcherFailure<int>("beginScan must precede addTerrain");
    if (terrainId.empty()) return watcherFailure<int>("terrain id must not be empty");
    if (std::find(candidate_.begin(), candidate_.end(), terrainId) != candidate_.end())
        return watcherFailure<int>("terrain ids in one scan must be unique");
    candidate_.push_back(terrainId);
    return Result<int>::success(static_cast<int>(candidate_.size()));
}

Result<int> PcgTerrainWatcher::commitScan() {
    if (!scanning_) return watcherFailure<int>("beginScan must precede commitScan");
    std::vector<PcgTerrainChange> nextChanges;
    if (initialized_) {
        for (const auto& id : candidate_)
            if (std::find(terrains_.begin(), terrains_.end(), id) == terrains_.end())
                nextChanges.push_back({id, PcgTerrainChangeType::Created});
        for (const auto& id : terrains_)
            if (std::find(candidate_.begin(), candidate_.end(), id) == candidate_.end())
                nextChanges.push_back({id, PcgTerrainChangeType::Removed});
    }
    terrains_.swap(candidate_);
    changes_.swap(nextChanges);
    candidate_.clear();
    scanning_ = false;
    initialized_ = true;
    return Result<int>::success(static_cast<int>(changes_.size()));
}

void PcgTerrainWatcher::cancelScan() {
    candidate_.clear();
    scanning_ = false;
}
int PcgTerrainWatcher::getTerrainCount() const noexcept { return static_cast<int>(terrains_.size()); }
int PcgTerrainWatcher::getChangeCount() const noexcept { return static_cast<int>(changes_.size()); }
Result<std::string> PcgTerrainWatcher::getChangeTerrainId(int index) const {
    if (index < 0 || index >= static_cast<int>(changes_.size()))
        return watcherFailure<std::string>("change index is out of range");
    return Result<std::string>::success(changes_[static_cast<std::size_t>(index)].terrainId);
}
Result<int> PcgTerrainWatcher::getChangeType(int index) const {
    if (index < 0 || index >= static_cast<int>(changes_.size()))
        return watcherFailure<int>("change index is out of range");
    return Result<int>::success(static_cast<int>(changes_[static_cast<std::size_t>(index)].type));
}
}  // namespace eve::procgen
