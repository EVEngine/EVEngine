#pragma once
#include "common/Export.h"


#include "common/Result.h"
#include "common/Value.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::scene {

/** @brief Provider-neutral position and quaternion stored by Pcg's location profile. */
struct LocationPose {
    float x = 0, y = 0, z = 0;
    float qx = 0, qy = 0, qz = 0, qw = 1;
};

/** @brief One named Pcg location bookmark with optional player transform metadata. */
struct LocationBookmark {
    std::string name;
    LocationPose camera;
    std::optional<LocationPose> player;
    std::string controller = "Not Been Set";
    std::string scene = "Not Been Set";
};

/** @brief Loaded camera/player transform pair. */
struct LocationSnapshot {
    LocationPose camera;
    std::optional<LocationPose> player;
};

/**
 * @brief Caller-owned Pcg location and bookmark state.
 * Mutable operations validate complete candidate data before publication. No scene objects are retained.
 */
class EVENGINE_API_PLATFORM LocationProfile {
public:
    /** @brief Save the one-shot startup location, replacing the previous value atomically. */
    [[nodiscard]] Result<void> saveLocation(const LocationPose& camera,
                                            const std::optional<LocationPose>& player = std::nullopt);
    /** @brief Consume the startup location and clear hasSaved exactly once, matching Pcg LoadLocation. */
    [[nodiscard]] Result<LocationSnapshot> loadLocation();
    /** @brief Whether a startup location is waiting to be consumed. */
    bool hasSavedLocation() const noexcept { return saved_.has_value(); }

    /** @brief Add a uniquely named bookmark and return its index. */
    [[nodiscard]] Result<int> addBookmark(LocationBookmark bookmark);
    /** @brief Replace one bookmark while preserving its existing name. */
    [[nodiscard]] Result<void> overrideBookmark(int index, const LocationSnapshot& snapshot,
                                                std::string controller, std::string scene);
    /** @brief Remove one bookmark and return Pcg's adjusted selected index. */
    [[nodiscard]] Result<int> removeBookmark(int index, int selectedIndex);
    /** @brief Copy a bookmark transform snapshot. */
    [[nodiscard]] Result<LocationSnapshot> loadBookmark(int index) const;
    /** @brief Return the previous wrapped selection, or NotFound for an empty profile. */
    [[nodiscard]] Result<int> previousBookmark(int selectedIndex) const;
    /** @brief Return the next wrapped selection, or NotFound for an empty profile. */
    [[nodiscard]] Result<int> nextBookmark(int selectedIndex) const;
    /** @brief Remove every bookmark. */
    void clearBookmarks() noexcept { bookmarks_.clear(); }
    int getBookmarkCount() const noexcept { return static_cast<int>(bookmarks_.size()); }
    /** @brief Copy a bookmark name, or return InvalidArgument for an invalid index. */
    [[nodiscard]] Result<std::string> getBookmarkName(int index) const;
    /** @brief Copy a bookmark controller name, or return InvalidArgument for an invalid index. */
    [[nodiscard]] Result<std::string> getBookmarkController(int index) const;
    /** @brief Copy a bookmark scene name, or return InvalidArgument for an invalid index. */
    [[nodiscard]] Result<std::string> getBookmarkScene(int index) const;
    /** @brief Serialize schema `eve.scene.location-profile` version 1 as deterministic JSON. */
    [[nodiscard]] Result<std::string> serializeJson() const;
    /**
     * @brief Atomically restore schema version 1 JSON.
     * @param json Strict UTF-8 JSON; unknown fields are rejected.
     * @remarks The current profile remains unchanged when parsing or validation fails.
     */
    [[nodiscard]] Result<void> restoreJson(std::string_view json);
    /**
     * @brief Borrow one bookmark.
     * @return Non-owning pointer valid until this profile's next bookmark mutation, or null for an invalid index.
     * @ownership The profile retains ownership; callers must not store the pointer across mutations or threads.
     */
    const LocationBookmark* bookmarkAt(int index) const noexcept;

private:
    std::optional<LocationSnapshot> saved_;
    std::vector<LocationBookmark> bookmarks_;
};

}  // namespace eve::scene
