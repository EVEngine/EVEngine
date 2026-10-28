#include "graphics/ReflectionProbeCapture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/ReflectionProbeRegistry.h"
#include "graphics/RenderSystem3D.h"

// ReflectionProbeCapture.cpp
namespace eve::graphics {

ReflectionProbeCapture::ReflectionProbeCapture(Graphics *gfx) : gfx_(gfx) {}

ReflectionProbeCapture::~ReflectionProbeCapture() {
    const auto registries = registries_;
    for (ReflectionProbeRegistry *registry : registries)
        if (registry) registry->remove(this);
}

void ReflectionProbeCapture::attachRegistry(ReflectionProbeRegistry *registry) {
    if (!registry || std::find(registries_.begin(), registries_.end(), registry) != registries_.end()) return;
    registries_.push_back(registry);
}

void ReflectionProbeCapture::detachRegistry(ReflectionProbeRegistry *registry) {
    registries_.erase(std::remove(registries_.begin(), registries_.end(), registry), registries_.end());
}

void ReflectionProbeCapture::configure(float x, float y, float z, int resolution, float nearZ, float farZ) {
    const int   clampedResolution = std::clamp(resolution, 16, 2048);
    const float clampedNear       = std::max(nearZ, 0.001f);
    const float clampedFar        = std::max(farZ, clampedNear + 0.01f);
    const bool  changed = x != centerX_ || y != centerY_ || z != centerZ_ || clampedResolution != resolution_ ||
                         clampedNear != nearZ_ || clampedFar != farZ_;
    centerX_    = x;
    centerY_    = y;
    centerZ_    = z;
    resolution_ = clampedResolution;
    nearZ_      = clampedNear;
    farZ_       = clampedFar;
    if (changed) requestCapture();
}

void ReflectionProbeCapture::requestCapture() {
    nextFace_                            = 0;
    pending_                             = true;
    recaptureAfterPublish_               = false;
    captureSkyR_                         = skyR_;
    captureSkyG_                         = skyG_;
    captureSkyB_                         = skyB_;
    captureSkyFaceTextures_              = skyFaceTextures_;
    captureSkyFaceTextureScales_         = skyFaceTextureScales_;
    captureSkyIntensity_                 = skyIntensity_;
    captureEnvironmentLighting_          = environmentLighting_;
    captureEnvironmentLightingIntensity_ = environmentLightingIntensity_;
    captureLodDistanceScale_             = lodDistanceScale_;
    snapshotCaptureTransparent_          = captureTransparent_;
    snapshotCaptureClusteredLighting_    = captureClusteredLighting_;
    if (camera_) {
        camera_->setEnvMap(captureEnvironmentLighting_);
        camera_->setEnvIntensity(captureEnvironmentLightingIntensity_);
    }
}

void ReflectionProbeCapture::requestCaptureAfterPublish() {
    if (pending_ && nextFace_ == 0) {
        requestCapture();
        return;
    }
    if (pending_ || publishedRevision_ != revision_) {
        recaptureAfterPublish_ = true;
        return;
    }
    requestCapture();
}

void ReflectionProbeCapture::queueCapture() { requestCaptureAfterPublish(); }

void ReflectionProbeCapture::ensureResources() {
    if (!gfx_) return;
    if (!camera_) {
        camera_ = Camera3D::createCamera();
        camera_->setActive(false);
        camera_->setFov(90.f);
        camera_->setAmbient(0.f, 0.f, 0.f);
        camera_->setEnvMap(captureEnvironmentLighting_);
        camera_->setEnvIntensity(captureEnvironmentLightingIntensity_);
        camera_->setAutoExposure(false);
        camera_->setExposure(0.f);
        camera_->setBloom(0.f, 1.f);
        camera_->clearEnvProbe();
        for (int i = 0; i < Camera3D::Data::kMaxReflectionProbes; ++i) camera_->clearReflectionProbe(i);
    }
    if (!skyQuad_) {
        static constexpr float positions[] = {
            -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f, 0.f,
        };
        static constexpr float normals[] = {
            0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
        };
        static constexpr float    uvs[]     = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
        static constexpr uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        skyQuad_                            = gfx_->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    }
    if (allocatedResolution_ == resolution_ && faces_[0]) return;
    for (Canvas *&face : faces_) face = gfx_->newHDRCanvas(resolution_, resolution_);
    allocatedResolution_ = resolution_;
}

void ReflectionProbeCapture::captureFace(int face) {
    static constexpr std::array<std::array<float, 3>, 6> directions{{
        {{1.f, 0.f, 0.f}},
        {{-1.f, 0.f, 0.f}},
        {{0.f, 1.f, 0.f}},
        {{0.f, -1.f, 0.f}},
        {{0.f, 0.f, 1.f}},
        {{0.f, 0.f, -1.f}},
    }};
    static constexpr std::array<std::array<float, 3>, 6> ups{{
        {{0.f, -1.f, 0.f}},
        {{0.f, -1.f, 0.f}},
        {{0.f, 0.f, 1.f}},
        {{0.f, 0.f, -1.f}},
        {{0.f, -1.f, 0.f}},
        {{0.f, -1.f, 0.f}},
    }};
    const auto                                          &direction = directions[static_cast<size_t>(face)];
    const auto                                          &up        = ups[static_cast<size_t>(face)];
    camera_->setEye(centerX_, centerY_, centerZ_);
    camera_->setTarget(centerX_ + direction[0], centerY_ + direction[1], centerZ_ + direction[2]);
    camera_->setUp(up[0], up[1], up[2]);
    auto data   = camera_->data();
    data->nearZ = nearZ_;
    data->farZ  = farZ_;
    faces_[static_cast<size_t>(face)]->clear(Color(captureSkyR_[static_cast<size_t>(face)] * captureSkyIntensity_,
                                                   captureSkyG_[static_cast<size_t>(face)] * captureSkyIntensity_,
                                                   captureSkyB_[static_cast<size_t>(face)] * captureSkyIntensity_, 1.f),
                                             std::nullopt, 1.0);
    RenderSystem3D::renderToCanvas(*gfx_, faces_[static_cast<size_t>(face)], camera_, captureMask_,
                                   captureLodDistanceScale_, snapshotCaptureTransparent_,
                                   snapshotCaptureClusteredLighting_,
                                   captureSkyFaceTextures_[static_cast<size_t>(face)], skyQuad_,
                                   captureSkyFaceTextureScales_[static_cast<size_t>(face)]);
}

bool ReflectionProbeCapture::update(int faceBudget) {
    lastCapturedFaceCount_ = 0;
    if (!pending_ || !gfx_) return isCaptureComplete();
    ensureResources();
    const int budget                = std::clamp(faceBudget, 1, 6);
    float     capturedGpuDurationMs = 0.f;
    for (int captured = 0; captured < budget && nextFace_ < 6; ++captured) {
        captureFace(nextFace_++);
        capturedGpuDurationMs += gfx_->getLastOffscreen3DGpuDurationMs();
        ++lastCapturedFaceCount_;
        ++totalCapturedFaceCount_;
    }
    reportGpuDurationMs(capturedGpuDurationMs);
    if (nextFace_ >= 6) {
        pending_ = false;
        ++revision_;
    }
    return isCaptureComplete();
}

Canvas *ReflectionProbeCapture::getFaceCanvas(int face) const {
    if (face < 0 || face >= 6) return nullptr;
    return faces_[static_cast<size_t>(face)];
}

bool ReflectionProbeCapture::stageCapturedFaces() {
    if (!gfx_ || !isCaptureComplete()) return false;
    if (stagedRevision_ == revision_ && stagingCubemap_) return true;
    if (!stagingCubemap_ || stagingResolution_ != resolution_) {
        stagingCubemap_    = gfx_->newHDRCubemap(resolution_);
        stagingResolution_ = stagingCubemap_ ? resolution_ : 0;
    }
    if (!stagingCubemap_) return false;
    if (!gfx_->copyHDRCanvasesToCubemap(faces_.data(), static_cast<int>(faces_.size()), stagingCubemap_)) return false;
    reportGpuDurationMs(gfx_->getLastOffscreen3DGpuDurationMs());
    stagedRevision_ = revision_;
    return true;
}

bool ReflectionProbeCapture::filterAndPublish(int sampleCount) {
    if (!gfx_ || !stagingCubemap_ || stagedRevision_ != revision_) return false;
    if (!gfx_->filterHDRReflectionCubemap(stagingCubemap_, sampleCount)) return false;
    reportGpuDurationMs(gfx_->getLastOffscreen3DGpuDurationMs());
    lastFilterSampleCount_       = std::clamp(sampleCount, 8, 512);
    Texture  *previousActive     = activeCubemap_;
    const int previousResolution = activeResolution_;
    activeCubemap_               = stagingCubemap_;
    activeResolution_            = resolution_;
    publishedRevision_           = stagedRevision_;
    stagingCubemap_              = previousResolution == resolution_ ? previousActive : nullptr;
    stagingResolution_           = stagingCubemap_ ? resolution_ : 0;
    framesSincePublish_          = 0;
    return true;
}

void ReflectionProbeCapture::setUpdateMode(const std::string &mode) {
    if (mode == "static")
        updateMode_ = UpdateMode::Static;
    else if (mode == "realtime")
        updateMode_ = UpdateMode::Realtime;
    else if (mode == "time_sliced" || mode == "timesliced")
        updateMode_ = UpdateMode::TimeSliced;
    else
        updateMode_ = UpdateMode::OnDemand;
}

std::string ReflectionProbeCapture::getUpdateMode() const {
    switch (updateMode_) {
        case UpdateMode::Static: return "static";
        case UpdateMode::Realtime: return "realtime";
        case UpdateMode::TimeSliced: return "time_sliced";
        case UpdateMode::OnDemand: return "on_demand";
    }
    return "on_demand";
}

void ReflectionProbeCapture::setRefreshInterval(int frames) { refreshInterval_ = std::clamp(frames, 1, 36000); }

void ReflectionProbeCapture::setCaptureMask(int mask) {
    const uint32_t nextMask = static_cast<uint32_t>(mask);
    if (nextMask == captureMask_) return;
    captureMask_ = nextMask;
    requestCapture();
}

void ReflectionProbeCapture::setEnvironmentLighting(Texture *environment, float intensity) {
    const float nextIntensity = std::max(intensity, 0.f);
    if (environment == environmentLighting_ && nextIntensity == environmentLightingIntensity_) return;
    environmentLighting_          = environment;
    environmentLightingIntensity_ = nextIntensity;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setSkyColor(float r, float g, float b, float intensity) {
    const float nextR         = std::max(r, 0.f);
    const float nextG         = std::max(g, 0.f);
    const float nextB         = std::max(b, 0.f);
    const float nextIntensity = std::max(intensity, 0.f);
    bool        changed       = nextIntensity != skyIntensity_;
    for (size_t face = 0; face < 6; ++face) {
        changed     = changed || skyR_[face] != nextR || skyG_[face] != nextG || skyB_[face] != nextB;
        skyR_[face] = nextR;
        skyG_[face] = nextG;
        skyB_[face] = nextB;
    }
    skyIntensity_ = nextIntensity;
    if (changed) requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setSkyFaceColor(int face, float r, float g, float b) {
    if (face < 0 || face >= 6) return;
    const size_t index = static_cast<size_t>(face);
    const float  nextR = std::max(r, 0.f);
    const float  nextG = std::max(g, 0.f);
    const float  nextB = std::max(b, 0.f);
    if (skyR_[index] == nextR && skyG_[index] == nextG && skyB_[index] == nextB) return;
    skyR_[index] = nextR;
    skyG_[index] = nextG;
    skyB_[index] = nextB;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setSkyFaceTexture(int face, Texture *texture) {
    if (face < 0 || face >= 6) return;
    const size_t index = static_cast<size_t>(face);
    if (skyFaceTextures_[index] == texture) return;
    skyFaceTextures_[index] = texture;
    requestCaptureAfterPublish();
}

Texture *ReflectionProbeCapture::getSkyFaceTexture(int face) const {
    if (face < 0 || face >= 6) return nullptr;
    return skyFaceTextures_[static_cast<size_t>(face)];
}

void ReflectionProbeCapture::setSkyFaceTextureScale(int face, float scale) {
    if (face < 0 || face >= 6) return;
    const size_t index     = static_cast<size_t>(face);
    const float  nextScale = std::clamp(scale, 0.f, 64.f);
    if (skyFaceTextureScales_[index] == nextScale) return;
    skyFaceTextureScales_[index] = nextScale;
    requestCaptureAfterPublish();
}

float ReflectionProbeCapture::getSkyFaceTextureScale(int face) const {
    if (face < 0 || face >= 6) return 1.f;
    return skyFaceTextureScales_[static_cast<size_t>(face)];
}

float ReflectionProbeCapture::getSkyFaceColor(int face, int component) const {
    if (face < 0 || face >= 6 || component < 0 || component >= 3) return 0.f;
    const size_t index = static_cast<size_t>(face);
    if (component == 0) return skyR_[index];
    if (component == 1) return skyG_[index];
    return skyB_[index];
}

void ReflectionProbeCapture::setCaptureLodDistanceScale(float scale) {
    const float nextScale = std::clamp(scale, 0.25f, 4.f);
    if (nextScale == lodDistanceScale_) return;
    lodDistanceScale_ = nextScale;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setCaptureTransparent(bool enabled) {
    if (enabled == captureTransparent_) return;
    captureTransparent_ = enabled;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setCaptureClusteredLighting(bool enabled) {
    if (enabled == captureClusteredLighting_) return;
    captureClusteredLighting_ = enabled;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setGpuBudgetMs(float milliseconds) {
    gpuBudgetMs_        = std::clamp(milliseconds, 0.1f, 16.f);
    underBudgetReports_ = 0;
}

void ReflectionProbeCapture::reportGpuDurationMs(float milliseconds) {
    if (milliseconds <= 0.f || !std::isfinite(milliseconds)) return;
    smoothedGpuDurationMs_ =
        smoothedGpuDurationMs_ > 0.f ? smoothedGpuDurationMs_ * 0.8f + milliseconds * 0.2f : milliseconds;

    if (smoothedGpuDurationMs_ > gpuBudgetMs_ * 1.1f) {
        underBudgetReports_ = 0;
        if (adaptiveFaceBudget_ > 1)
            --adaptiveFaceBudget_;
        else
            adaptiveFilterSamples_ = std::max(adaptiveFilterSamples_ / 2, 8);
        return;
    }

    if (smoothedGpuDurationMs_ < gpuBudgetMs_ * 0.7f) {
        if (++underBudgetReports_ < 4) return;
        underBudgetReports_ = 0;
        if (adaptiveFilterSamples_ < 64)
            adaptiveFilterSamples_ = std::min(adaptiveFilterSamples_ * 2, 64);
        else
            adaptiveFaceBudget_ = std::min(adaptiveFaceBudget_ + 1, 6);
    } else {
        underBudgetReports_ = 0;
    }
}

bool ReflectionProbeCapture::tickAdaptive() { return tick(adaptiveFaceBudget_, adaptiveFilterSamples_); }

void ReflectionProbeCapture::advanceRefreshPolicy() {
    ++framesSincePublish_;
    if (!pending_) {
        if (recaptureAfterPublish_ && publishedRevision_ == revision_)
            requestCapture();
        else if (updateMode_ == UpdateMode::Realtime ||
                 (updateMode_ == UpdateMode::TimeSliced && framesSincePublish_ >= refreshInterval_))
            requestCapture();
        else if (updateMode_ == UpdateMode::Static && revision_ == 0)
            requestCapture();
    }
}

bool ReflectionProbeCapture::tick(int faceBudget, int filterSamples) {
    if (gfx_) reportGpuDurationMs(gfx_->getLastOffscreen3DGpuDurationMs());
    advanceRefreshPolicy();
    if (pending_ && !update(faceBudget)) return false;
    if (publishedRevision_ == revision_) return false;
    if (!stageCapturedFaces()) return false;
    return filterAndPublish(filterSamples);
}

void ReflectionProbeCapture::configureInfluence(float extentX, float extentY, float extentZ, float intensity,
                                                float blendDistance, int priority) {
    influenceExtentX_       = std::max(extentX, 0.001f);
    influenceExtentY_       = std::max(extentY, 0.001f);
    influenceExtentZ_       = std::max(extentZ, 0.001f);
    influenceIntensity_     = std::max(intensity, 0.f);
    influenceBlendDistance_ = std::max(blendDistance, 0.f);
    influencePriority_      = priority;
}

bool ReflectionProbeCapture::applyConfiguredToCamera(Camera3D *camera, int slot) {
    if (!camera || !activeCubemap_ || publishedRevision_ == 0) return false;
    camera->setReflectionProbe(slot, activeCubemap_, centerX_, centerY_, centerZ_, influenceExtentX_, influenceExtentY_,
                               influenceExtentZ_, influenceIntensity_, influenceBlendDistance_, influencePriority_);
    return true;
}

bool ReflectionProbeCapture::applyToCamera(Camera3D *camera, int slot, float extentX, float extentY, float extentZ,
                                           float intensity, float blendDistance, int priority) {
    configureInfluence(extentX, extentY, extentZ, intensity, blendDistance, priority);
    return applyConfiguredToCamera(camera, slot);
}

}  // namespace eve::graphics

// ReflectionProbeRegistry.cpp
namespace eve::graphics {

ReflectionProbeRegistry::~ReflectionProbeRegistry() { clear(); }

void ReflectionProbeRegistry::add(ReflectionProbeCapture *probe) {
    if (!probe) return;
    const auto found =
        std::find_if(entries_.begin(), entries_.end(), [probe](const Entry &entry) { return entry.probe == probe; });
    if (found == entries_.end()) {
        entries_.push_back(Entry{probe, nextOrder_++});
        probe->attachRegistry(this);
    }
}

void ReflectionProbeRegistry::remove(ReflectionProbeCapture *probe) {
    if (probe) probe->detachRegistry(this);
    entries_.erase(
        std::remove_if(entries_.begin(), entries_.end(), [probe](const Entry &entry) { return entry.probe == probe; }),
        entries_.end());
    lastSelected_.erase(std::remove(lastSelected_.begin(), lastSelected_.end(), probe), lastSelected_.end());
}

void ReflectionProbeRegistry::clear() {
    for (const Entry &entry : entries_)
        if (entry.probe) entry.probe->detachRegistry(this);
    entries_.clear();
    lastSelected_.clear();
    lastSelectedCount_       = 0;
    lastCapturedFaceCount_   = 0;
    lastPublishedCount_      = 0;
    lastCandidateCount_      = 0;
    lastDistanceCulledCount_ = 0;
}

Result<void> ReflectionProbeRegistry::setMaxRenderDistance(float distance) {
    if (!std::isfinite(distance) || distance <= 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Reflection probe render distance must be positive and finite",
                                                       "graphics.reflectionProbe.maxRenderDistance"));
    maxRenderDistance_ = distance;
    return Result<void>::success();
}

void ReflectionProbeRegistry::setSelectionHysteresis(float distance) {
    selectionHysteresis_ = std::clamp(distance, 0.f, 100.f);
}

int ReflectionProbeRegistry::queueCapture(int changedMask) {
    const uint32_t mask   = static_cast<uint32_t>(changedMask);
    int            queued = 0;
    for (Entry &entry : entries_) {
        if (!entry.probe || (static_cast<uint32_t>(entry.probe->getCaptureMask()) & mask) == 0u) continue;
        entry.probe->queueCapture();
        ++queued;
    }
    return queued;
}

int ReflectionProbeRegistry::queueCaptureAABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ,
                                              int changedMask) {
    const float    changedMin[3] = {std::min(minX, maxX), std::min(minY, maxY), std::min(minZ, maxZ)};
    const float    changedMax[3] = {std::max(minX, maxX), std::max(minY, maxY), std::max(minZ, maxZ)};
    const uint32_t mask          = static_cast<uint32_t>(changedMask);
    int            queued        = 0;
    for (Entry &entry : entries_) {
        ReflectionProbeCapture *probe = entry.probe;
        if (!probe || (static_cast<uint32_t>(probe->getCaptureMask()) & mask) == 0u) continue;
        const float center[3] = {probe->getCenterX(), probe->getCenterY(), probe->getCenterZ()};
        float       distance2 = 0.f;
        for (int axis = 0; axis < 3; ++axis) {
            const float nearest = std::clamp(center[axis], changedMin[axis], changedMax[axis]);
            const float delta   = center[axis] - nearest;
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
    lastPublishedCount_    = 0;
    faceBudget             = std::max(faceBudget, 0);
    filterBudget           = std::max(filterBudget, 0);
    filterSamples          = std::clamp(filterSamples, 8, 512);

    for (Entry &entry : entries_) {
        if (!entry.probe || (distanceCullingEnabled_ && entry.distanceCulled)) continue;
        entry.probe->advanceRefreshPolicy();
        const bool waiting =
            entry.probe->isCapturePending() || entry.probe->getPublishedRevision() != entry.probe->getRevision();
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
        if (selected->probe->stageCapturedFaces() && selected->probe->filterAndPublish(filterSamples)) {
            selected->waitAge = 0;
            ++lastPublishedCount_;
        } else {
            break;
        }
    }
    return lastPublishedCount_;
}

int ReflectionProbeRegistry::updateCamera(Camera3D *camera) {
    lastSelectedCount_       = 0;
    lastCandidateCount_      = 0;
    lastDistanceCulledCount_ = 0;
    if (!camera) {
        lastSelected_.clear();
        return 0;
    }
    for (int slot = 0; slot < Camera3D::Data::kMaxReflectionProbes; ++slot) camera->clearReflectionProbe(slot);

    struct Candidate {
        ReflectionProbeCapture *probe     = nullptr;
        int                     priority  = 0;
        float                   distance2 = 0.f;
        bool                    retained  = false;
        uint64_t                order     = 0;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(entries_.size());
    const auto  cameraData     = camera->data();
    const float eye[3]         = {cameraData->eyeX, cameraData->eyeY, cameraData->eyeZ};
    const float target[3]      = {cameraData->targetX, cameraData->targetY, cameraData->targetZ};
    const float viewSegment[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
    const float viewSegmentLength2 =
        viewSegment[0] * viewSegment[0] + viewSegment[1] * viewSegment[1] + viewSegment[2] * viewSegment[2];
    for (Entry &entry : entries_) {
        ReflectionProbeCapture *probe = entry.probe;
        if (!probe) continue;
        const float center[3] = {probe->getCenterX(), probe->getCenterY(), probe->getCenterZ()};
        const float eyeDx = center[0] - eye[0], eyeDy = center[1] - eye[1], eyeDz = center[2] - eye[2];
        const bool  distanceCulled = distanceCullingEnabled_ && eyeDx * eyeDx + eyeDy * eyeDy + eyeDz * eyeDz >=
                                                                   maxRenderDistance_ * maxRenderDistance_;
        if (entry.distanceCulled && !distanceCulled) probe->queueCapture();
        entry.distanceCulled = distanceCulled;
        if (distanceCulled) {
            ++lastDistanceCulledCount_;
            continue;
        }
        if (!probe->getActiveCubemap() || probe->getPublishedRevision() == 0 || probe->getInfluenceIntensity() <= 0.f)
            continue;
        const float extent[3] = {probe->getInfluenceExtentX(), probe->getInfluenceExtentY(),
                                 probe->getInfluenceExtentZ()};
        float       closestT  = 0.f;
        if (viewSegmentLength2 > 1e-8f) {
            const float toCenter[3] = {center[0] - eye[0], center[1] - eye[1], center[2] - eye[2]};
            closestT                = std::clamp(
                (toCenter[0] * viewSegment[0] + toCenter[1] * viewSegment[1] + toCenter[2] * viewSegment[2]) /
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
            const float outside = std::max(std::abs(closestViewPoint[axis] - center[axis]) - extent[axis], 0.f);
            distance2 += outside * outside;
        }
        const bool  retained = std::find(lastSelected_.begin(), lastSelected_.end(), probe) != lastSelected_.end();
        const float distance = std::sqrt(distance2);
        const float biasedDistance = retained ? std::max(distance - selectionHysteresis_, 0.f) : distance;
        candidates.push_back(
            Candidate{probe, probe->getInfluencePriority(), biasedDistance * biasedDistance, retained, entry.order});
    }
    lastCandidateCount_ = static_cast<int>(candidates.size());
    std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.distance2 != b.distance2) return a.distance2 < b.distance2;
        if (a.retained != b.retained) return a.retained;
        return a.order < b.order;
    });

    const int selected = std::min(static_cast<int>(candidates.size()), Camera3D::Data::kMaxReflectionProbes);
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
