#include "graphics/ReflectionProbeRegistry.h"

#include "graphics/ReflectionProbeCapture.h"
#include "graphics/RenderSystem3D.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::graphics {

ReflectionProbeRegistry::~ReflectionProbeRegistry() { clear(); }

void ReflectionProbeRegistry::add(ReflectionProbeCapture *probe) {
    if (!probe) return;
    const auto found = std::find_if(entries_.begin(), entries_.end(),
                                    [probe](const Entry &entry) { return entry.probe == probe; });
    if (found == entries_.end()) {
        entries_.push_back(Entry{probe, nextOrder_++});
        probe->attachRegistry(this);
    }
}

void ReflectionProbeRegistry::remove(ReflectionProbeCapture *probe) {
    if (probe) probe->detachRegistry(this);
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [probe](const Entry &entry) { return entry.probe == probe; }),
                   entries_.end());
    lastSelected_.erase(std::remove(lastSelected_.begin(), lastSelected_.end(), probe),
                        lastSelected_.end());
}

void ReflectionProbeRegistry::clear() {
    for (const Entry &entry : entries_)
        if (entry.probe) entry.probe->detachRegistry(this);
    entries_.clear();
    lastSelected_.clear();
    lastSelectedCount_ = 0;
    lastCapturedFaceCount_ = 0;
    lastPublishedCount_ = 0;
    lastCandidateCount_ = 0;
}

void ReflectionProbeRegistry::setSelectionHysteresis(float distance) {
    selectionHysteresis_ = std::clamp(distance, 0.f, 100.f);
}

int ReflectionProbeRegistry::queueCapture(int changedMask) {
    const uint32_t mask = static_cast<uint32_t>(changedMask);
    int queued = 0;
    for (Entry &entry : entries_) {
        if (!entry.probe || (static_cast<uint32_t>(entry.probe->getCaptureMask()) & mask) == 0u)
            continue;
        entry.probe->queueCapture();
        ++queued;
    }
    return queued;
}

int ReflectionProbeRegistry::queueCaptureAABB(float minX, float minY, float minZ, float maxX,
                                              float maxY, float maxZ, int changedMask) {
    const float changedMin[3] = {std::min(minX, maxX), std::min(minY, maxY),
                                 std::min(minZ, maxZ)};
    const float changedMax[3] = {std::max(minX, maxX), std::max(minY, maxY),
                                 std::max(minZ, maxZ)};
    const uint32_t mask = static_cast<uint32_t>(changedMask);
    int queued = 0;
    for (Entry &entry : entries_) {
        ReflectionProbeCapture *probe = entry.probe;
        if (!probe || (static_cast<uint32_t>(probe->getCaptureMask()) & mask) == 0u) continue;
        const float center[3] = {probe->getCenterX(), probe->getCenterY(), probe->getCenterZ()};
        float distance2 = 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            const float nearest = std::clamp(center[axis], changedMin[axis], changedMax[axis]);
            const float delta = center[axis] - nearest;
            distance2 += delta * delta;
        }
        const float captureRadius = probe->getCaptureFarDistance();
        if (distance2 > captureRadius * captureRadius) continue;
        probe->queueCapture();
        ++queued;
    }
    return queued;
}

int ReflectionProbeRegistry::tick(int faceBudget, int filterBudget, int filterSamples) {
    lastCapturedFaceCount_ = 0;
    lastPublishedCount_ = 0;
    faceBudget = std::max(faceBudget, 0);
    filterBudget = std::max(filterBudget, 0);
    filterSamples = std::clamp(filterSamples, 8, 512);

    for (Entry &entry : entries_) {
        if (!entry.probe) continue;
        entry.probe->advanceRefreshPolicy();
        const bool waiting = entry.probe->isCapturePending() ||
                             entry.probe->getPublishedRevision() != entry.probe->getRevision();
        if (waiting && entry.waitAge < std::numeric_limits<uint32_t>::max()) ++entry.waitAge;
    }

    auto better = [](const Entry *a, const Entry *b) {
        const int priorityA = a->probe->getInfluencePriority();
        const int priorityB = b->probe->getInfluencePriority();
        if (priorityA != priorityB) return priorityA > priorityB;
        if (a->waitAge != b->waitAge) return a->waitAge > b->waitAge;
        return a->order < b->order;
    };

    while (faceBudget-- > 0) {
        Entry *selected = nullptr;
        for (Entry &entry : entries_) {
            if (!entry.probe || !entry.probe->isCapturePending()) continue;
            if (!selected || better(&entry, selected)) selected = &entry;
        }
        if (!selected) break;
        selected->probe->update(1);
        selected->waitAge = 0;
        lastCapturedFaceCount_ += selected->probe->getLastCapturedFaceCount();
    }

    while (filterBudget-- > 0) {
        Entry *selected = nullptr;
        for (Entry &entry : entries_) {
            if (!entry.probe || entry.probe->isCapturePending() ||
                entry.probe->getRevision() == entry.probe->getPublishedRevision())
                continue;
            if (!selected || better(&entry, selected)) selected = &entry;
        }
        if (!selected) break;
        if (selected->probe->stageCapturedFaces() &&
            selected->probe->filterAndPublish(filterSamples)) {
            selected->waitAge = 0;
            ++lastPublishedCount_;
        } else {
            break;
        }
    }
    return lastPublishedCount_;
}

int ReflectionProbeRegistry::updateCamera(Camera3D *camera) {
    lastSelectedCount_ = 0;
    lastCandidateCount_ = 0;
    if (!camera) {
        lastSelected_.clear();
        return 0;
    }
    for (int slot = 0; slot < Camera3D::Data::kMaxReflectionProbes; ++slot)
        camera->clearReflectionProbe(slot);

    struct Candidate {
        ReflectionProbeCapture *probe = nullptr;
        int priority = 0;
        float distance2 = 0.f;
        bool retained = false;
        uint64_t order = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(entries_.size());
    const auto cameraData = camera->data();
    const float eye[3] = {cameraData->eyeX, cameraData->eyeY, cameraData->eyeZ};
    const float target[3] = {cameraData->targetX, cameraData->targetY, cameraData->targetZ};
    const float viewSegment[3] = {target[0] - eye[0], target[1] - eye[1],
                                  target[2] - eye[2]};
    const float viewSegmentLength2 = viewSegment[0] * viewSegment[0] +
                                     viewSegment[1] * viewSegment[1] +
                                     viewSegment[2] * viewSegment[2];
    for (const Entry &entry : entries_) {
        ReflectionProbeCapture *probe = entry.probe;
        if (!probe || !probe->getActiveCubemap() || probe->getPublishedRevision() == 0 ||
            probe->getInfluenceIntensity() <= 0.f)
            continue;
        const float center[3] = {probe->getCenterX(), probe->getCenterY(), probe->getCenterZ()};
        const float extent[3] = {probe->getInfluenceExtentX(), probe->getInfluenceExtentY(),
                                 probe->getInfluenceExtentZ()};
        float closestT = 0.f;
        if (viewSegmentLength2 > 1e-8f) {
            const float toCenter[3] = {center[0] - eye[0], center[1] - eye[1],
                                       center[2] - eye[2]};
            closestT = std::clamp((toCenter[0] * viewSegment[0] +
                                   toCenter[1] * viewSegment[1] +
                                   toCenter[2] * viewSegment[2]) /
                                      viewSegmentLength2,
                                  0.f, 1.f);
        }
        const float closestViewPoint[3] = {
            eye[0] + viewSegment[0] * closestT,
            eye[1] + viewSegment[1] * closestT,
            eye[2] + viewSegment[2] * closestT,
        };
        float distance2 = 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            const float outside =
                std::max(std::abs(closestViewPoint[axis] - center[axis]) - extent[axis], 0.f);
            distance2 += outside * outside;
        }
        const bool retained =
            std::find(lastSelected_.begin(), lastSelected_.end(), probe) != lastSelected_.end();
        const float distance = std::sqrt(distance2);
        const float biasedDistance =
            retained ? std::max(distance - selectionHysteresis_, 0.f) : distance;
        candidates.push_back(Candidate{probe, probe->getInfluencePriority(),
                                       biasedDistance * biasedDistance, retained, entry.order});
    }
    lastCandidateCount_ = static_cast<int>(candidates.size());
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate &a, const Candidate &b) {
                         if (a.priority != b.priority) return a.priority > b.priority;
                         if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
                         if (a.retained != b.retained) return a.retained;
                         return a.order < b.order;
                     });

    const int selected = std::min(static_cast<int>(candidates.size()),
                                  Camera3D::Data::kMaxReflectionProbes);
    lastSelected_.clear();
    lastSelected_.reserve(static_cast<size_t>(selected));
    for (int slot = 0; slot < selected; ++slot) {
        ReflectionProbeCapture *probe = candidates[static_cast<size_t>(slot)].probe;
        if (probe->applyConfiguredToCamera(camera, slot)) {
            lastSelected_.push_back(probe);
            ++lastSelectedCount_;
        }
    }
    return lastSelectedCount_;
}

}  // namespace eve::graphics
