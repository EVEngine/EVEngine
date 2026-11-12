#pragma once

#include <memory>
#include "common/SceneSearch.h"

namespace eve::scene {
struct SceneHnswIndex;
/**
 * @brief Scene-owned bounded search projection. One active session, no retained entity pointers.
 * @thread Engine thread only. Does not invoke callbacks or modify the scene.
 */
class EVENGINE_API_PLATFORM SceneSearch final : public ISceneSearch {
public:
    /** @brief Create an empty engine-thread search session; invokes no callbacks. */
    SceneSearch();
    /** @brief Release the derived HNSW index before its distance-space owner. */
    ~SceneSearch() override;
    SceneSearch(const SceneSearch&)            = delete;
    SceneSearch& operator=(const SceneSearch&) = delete;
    SceneSearch(SceneSearch&&)                 = delete;
    SceneSearch& operator=(SceneSearch&&)      = delete;
    /** @brief Drop all projection tokens at an owning runtime/world boundary; engine thread, no callbacks. */
    void invalidate();
    /** @brief Capture an existing host; invalidates the old session only on success. */
    [[nodiscard]] Result<SceneSearchSnapshot> prepare(std::string_view host) override;
    /** @brief Read live geometry and current index identity; source changes return an explicit stale status. */
    [[nodiscard]] Result<SceneSearchSnapshot> status() override;
    /** @brief Validate source and publish one complete normalized embedding space atomically. */
    [[nodiscard]] Result<void> publish(const SceneEmbeddingBatch& batch) override;
    /** @brief Validate source, apply exact filters, rank and page without scene mutation. */
    [[nodiscard]] Result<SceneSearchResult> query(const SceneSearchQuery& query) override;

private:
    SceneSearchSnapshot              snapshot_;
    std::string                      source_, model_;
    std::vector<std::vector<double>> vectors_;
    std::unique_ptr<SceneHnswIndex>  index_;
    std::string                      cursor_, queryKey_;
    std::size_t                      offset_   = 0;
    std::uint64_t                    sequence_ = 0;
};
/** @brief Install the process-lifetime provider using the existing capability registry. */
void registerSceneSearch();
/** @brief Invalidate the registered projection before Scene/runtime teardown; engine thread, no callbacks. */
void invalidateSceneSearch();
}  // namespace eve::scene
