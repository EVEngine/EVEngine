#pragma once

#include "common/Result.h"

#include <string>
#include <vector>

namespace eve::procgen {

/** @brief Pcg terrain hierarchy change kind. */
enum class PcgTerrainChangeType { Created = 0, Removed = 1 };

/** @brief One copied terrain identity and its change kind. */
struct PcgTerrainChange {
    std::string terrainId;
    PcgTerrainChangeType type = PcgTerrainChangeType::Created;
};

/**
 * @brief Deterministic, caller-driven equivalent of PcgTerrainWatcher's hierarchy diff.
 *
 * A scan is staged with beginScan/addTerrain and published with commitScan. The first
 * successful scan seeds the cache without reporting creation, matching Pcg's startup
 * CacheExistingTerrains behavior. Later scans report creations in current-scan order,
 * followed by removals in prior-scan order.
 *
 * @ownership Owns copied terrain ids and changes; retains no scene object or callback.
 * @thread Caller-thread affine. No method invokes script or user callbacks.
 */
class PcgTerrainWatcher {
public:
    /** @brief Begin a candidate hierarchy scan without changing the published cache. */
    void beginScan();
    /** @brief Add one non-empty, unique stable terrain id to the candidate scan. */
    [[nodiscard]] Result<int> addTerrain(const std::string& terrainId);
    /** @brief Atomically publish the candidate and return the number of emitted changes. */
    [[nodiscard]] Result<int> commitScan();
    /** @brief Discard a candidate scan. */
    void cancelScan();
    int getTerrainCount() const noexcept;
    int getChangeCount() const noexcept;
    /** @brief Copy a changed terrain id by index. */
    [[nodiscard]] Result<std::string> getChangeTerrainId(int index) const;
    /** @brief Return 0 for Created or 1 for Removed by index. */
    [[nodiscard]] Result<int> getChangeType(int index) const;

private:
    bool initialized_ = false;
    bool scanning_ = false;
    std::vector<std::string> terrains_;
    std::vector<std::string> candidate_;
    std::vector<PcgTerrainChange> changes_;
};

}  // namespace eve::procgen
