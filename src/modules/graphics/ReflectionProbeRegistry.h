#pragma once

#include <cstdint>
#include <vector>

namespace eve::graphics {

class Camera3D;
class ReflectionProbeCapture;

/** @brief Scene-level stable selection of published reflection probes for a camera. */
class ReflectionProbeRegistry {
public:
    /** @brief Detach all probes before script-owned destruction. */
    ~ReflectionProbeRegistry();
    /** @brief Register a probe once; duplicate registrations are ignored. */
    void add(ReflectionProbeCapture *probe);
    /** @brief Remove a registered probe. */
    void remove(ReflectionProbeCapture *probe);
    /** @brief Remove every registered probe. */
    void clear();
    /** @brief Return the number of registered probes. */
    int getCount() const { return static_cast<int>(entries_.size()); }
    /** @brief Return the number of published probes selected by the last update. */
    int getLastSelectedCount() const { return lastSelectedCount_; }
    /** @brief Return faces rendered by the most recent scheduled tick. */
    int getLastCapturedFaceCount() const { return lastCapturedFaceCount_; }
    /** @brief Return probe revisions published by the most recent scheduled tick. */
    int getLastPublishedCount() const { return lastPublishedCount_; }
    /** @brief Set world-space distance hysteresis used to retain previous camera selections. */
    void setSelectionHysteresis(float distance);
    /** @brief Return world-space camera-selection hysteresis. */
    float getSelectionHysteresis() const { return selectionHysteresis_; }
    /** @brief Return published candidates considered by the most recent camera update. */
    int getLastCandidateCount() const { return lastCandidateCount_; }
    /**
     * @brief Queue coherent recapture for probes whose capture mask intersects @p changedMask.
     * @return Number of probes marked dirty.
     */
    int queueCapture(int changedMask = -1);
    /**
     * @brief Queue probes whose mask and capture-radius sphere overlap a changed world AABB.
     * @return Number of probes marked dirty.
     */
    int queueCaptureAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                         int changedMask = -1);
    /**
     * @brief Advance every refresh policy while enforcing scene-wide GPU work budgets.
     * @param faceBudget Maximum cubemap faces rendered this call.
     * @param filterBudget Maximum completed probes staged and filtered this call.
     * @param filterSamples GGX filter sample count per published probe, clamped to [8,512].
     * @return Number of newly published revisions.
     */
    int tick(int faceBudget = 1, int filterBudget = 1, int filterSamples = 64);
    /**
     * @brief Select eight probes nearest the camera view segment and replace Camera3D slots.
     * @return Number of enabled camera slots after selection.
     */
    int updateCamera(Camera3D *camera);

private:
    struct Entry {
        ReflectionProbeCapture *probe = nullptr;
        uint64_t order = 0;
        uint32_t waitAge = 0;
    };

    std::vector<Entry> entries_;
    uint64_t nextOrder_ = 0;
    int lastSelectedCount_ = 0;
    int lastCapturedFaceCount_ = 0;
    int lastPublishedCount_ = 0;
    int lastCandidateCount_ = 0;
    float selectionHysteresis_ = 0.75f;
    std::vector<ReflectionProbeCapture *> lastSelected_;
};

}  // namespace eve::graphics
