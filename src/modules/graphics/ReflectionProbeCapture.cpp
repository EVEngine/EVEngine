#include "graphics/ReflectionProbeCapture.h"

#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/ReflectionProbeRegistry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace eve::graphics {

ReflectionProbeCapture::ReflectionProbeCapture(Graphics *gfx) : gfx_(gfx) {}

ReflectionProbeCapture::~ReflectionProbeCapture() {
    const auto registries = registries_;
    for (ReflectionProbeRegistry *registry : registries)
        if (registry) registry->remove(this);
}

void ReflectionProbeCapture::attachRegistry(ReflectionProbeRegistry *registry) {
    if (!registry || std::find(registries_.begin(), registries_.end(), registry) != registries_.end())
        return;
    registries_.push_back(registry);
}

void ReflectionProbeCapture::detachRegistry(ReflectionProbeRegistry *registry) {
    registries_.erase(std::remove(registries_.begin(), registries_.end(), registry),
                      registries_.end());
}

void ReflectionProbeCapture::configure(float x, float y, float z, int resolution, float nearZ,
                                       float farZ) {
    const int clampedResolution = std::clamp(resolution, 16, 2048);
    const float clampedNear = std::max(nearZ, 0.001f);
    const float clampedFar = std::max(farZ, clampedNear + 0.01f);
    const bool changed = x != centerX_ || y != centerY_ || z != centerZ_ ||
                         clampedResolution != resolution_ || clampedNear != nearZ_ ||
                         clampedFar != farZ_;
    centerX_ = x;
    centerY_ = y;
    centerZ_ = z;
    resolution_ = clampedResolution;
    nearZ_ = clampedNear;
    farZ_ = clampedFar;
    if (changed) requestCapture();
}

void ReflectionProbeCapture::requestCapture() {
    nextFace_ = 0;
    pending_ = true;
    recaptureAfterPublish_ = false;
    captureSkyR_ = skyR_;
    captureSkyG_ = skyG_;
    captureSkyB_ = skyB_;
    captureSkyFaceTextures_ = skyFaceTextures_;
    captureSkyFaceTextureScales_ = skyFaceTextureScales_;
    captureSkyIntensity_ = skyIntensity_;
    captureEnvironmentLighting_ = environmentLighting_;
    captureEnvironmentLightingIntensity_ = environmentLightingIntensity_;
    captureLodDistanceScale_ = lodDistanceScale_;
    snapshotCaptureTransparent_ = captureTransparent_;
    snapshotCaptureClusteredLighting_ = captureClusteredLighting_;
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
        for (int i = 0; i < Camera3D::Data::kMaxReflectionProbes; ++i)
            camera_->clearReflectionProbe(i);
    }
    if (!skyQuad_) {
        static constexpr float positions[] = {
            -1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 1.f, 1.f, 0.f, -1.f, 1.f, 0.f,
        };
        static constexpr float normals[] = {
            0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f,
        };
        static constexpr float uvs[] = {0.f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f};
        static constexpr uint32_t indices[] = {0, 1, 2, 0, 2, 3};
        skyQuad_ = gfx_->newMeshFromArrays(positions, normals, uvs, 4, indices, 6);
    }
    if (allocatedResolution_ == resolution_ && faces_[0]) return;
    for (Canvas *&face : faces_) face = gfx_->newHDRCanvas(resolution_, resolution_);
    allocatedResolution_ = resolution_;
}

void ReflectionProbeCapture::captureFace(int face) {
    static constexpr std::array<std::array<float, 3>, 6> directions{{
        {{1.f, 0.f, 0.f}}, {{-1.f, 0.f, 0.f}}, {{0.f, 1.f, 0.f}},
        {{0.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f, -1.f}},
    }};
    static constexpr std::array<std::array<float, 3>, 6> ups{{
        {{0.f, -1.f, 0.f}}, {{0.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}},
        {{0.f, 0.f, -1.f}}, {{0.f, -1.f, 0.f}}, {{0.f, -1.f, 0.f}},
    }};
    const auto &direction = directions[static_cast<size_t>(face)];
    const auto &up = ups[static_cast<size_t>(face)];
    camera_->setEye(centerX_, centerY_, centerZ_);
    camera_->setTarget(centerX_ + direction[0], centerY_ + direction[1], centerZ_ + direction[2]);
    camera_->setUp(up[0], up[1], up[2]);
    auto data = camera_->data();
    data->nearZ = nearZ_;
    data->farZ = farZ_;
    faces_[static_cast<size_t>(face)]->clear(
        Color(captureSkyR_[static_cast<size_t>(face)] * captureSkyIntensity_,
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
    const int budget = std::clamp(faceBudget, 1, 6);
    float capturedGpuDurationMs = 0.f;
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
        stagingCubemap_ = gfx_->newHDRCubemap(resolution_);
        stagingResolution_ = stagingCubemap_ ? resolution_ : 0;
    }
    if (!stagingCubemap_) return false;
    if (!gfx_->copyHDRCanvasesToCubemap(faces_.data(), static_cast<int>(faces_.size()),
                                        stagingCubemap_))
        return false;
    reportGpuDurationMs(gfx_->getLastOffscreen3DGpuDurationMs());
    stagedRevision_ = revision_;
    return true;
}

bool ReflectionProbeCapture::filterAndPublish(int sampleCount) {
    if (!gfx_ || !stagingCubemap_ || stagedRevision_ != revision_) return false;
    if (!gfx_->filterHDRReflectionCubemap(stagingCubemap_, sampleCount)) return false;
    reportGpuDurationMs(gfx_->getLastOffscreen3DGpuDurationMs());
    lastFilterSampleCount_ = std::clamp(sampleCount, 8, 512);
    Texture *previousActive = activeCubemap_;
    const int previousResolution = activeResolution_;
    activeCubemap_ = stagingCubemap_;
    activeResolution_ = resolution_;
    publishedRevision_ = stagedRevision_;
    stagingCubemap_ = previousResolution == resolution_ ? previousActive : nullptr;
    stagingResolution_ = stagingCubemap_ ? resolution_ : 0;
    framesSincePublish_ = 0;
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

void ReflectionProbeCapture::setRefreshInterval(int frames) {
    refreshInterval_ = std::clamp(frames, 1, 36000);
}

void ReflectionProbeCapture::setCaptureMask(int mask) {
    const uint32_t nextMask = static_cast<uint32_t>(mask);
    if (nextMask == captureMask_) return;
    captureMask_ = nextMask;
    requestCapture();
}

void ReflectionProbeCapture::setEnvironmentLighting(Texture *environment, float intensity) {
    const float nextIntensity = std::max(intensity, 0.f);
    if (environment == environmentLighting_ && nextIntensity == environmentLightingIntensity_)
        return;
    environmentLighting_ = environment;
    environmentLightingIntensity_ = nextIntensity;
    requestCaptureAfterPublish();
}

void ReflectionProbeCapture::setSkyColor(float r, float g, float b, float intensity) {
    const float nextR = std::max(r, 0.f);
    const float nextG = std::max(g, 0.f);
    const float nextB = std::max(b, 0.f);
    const float nextIntensity = std::max(intensity, 0.f);
    bool changed = nextIntensity != skyIntensity_;
    for (size_t face = 0; face < 6; ++face) {
        changed = changed || skyR_[face] != nextR || skyG_[face] != nextG ||
                  skyB_[face] != nextB;
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
    const float nextR = std::max(r, 0.f);
    const float nextG = std::max(g, 0.f);
    const float nextB = std::max(b, 0.f);
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
    const size_t index = static_cast<size_t>(face);
    const float nextScale = std::clamp(scale, 0.f, 64.f);
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
    gpuBudgetMs_ = std::clamp(milliseconds, 0.1f, 16.f);
    underBudgetReports_ = 0;
}

void ReflectionProbeCapture::reportGpuDurationMs(float milliseconds) {
    if (milliseconds <= 0.f || !std::isfinite(milliseconds)) return;
    smoothedGpuDurationMs_ = smoothedGpuDurationMs_ > 0.f
                                 ? smoothedGpuDurationMs_ * 0.8f + milliseconds * 0.2f
                                 : milliseconds;

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

bool ReflectionProbeCapture::tickAdaptive() {
    return tick(adaptiveFaceBudget_, adaptiveFilterSamples_);
}

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

void ReflectionProbeCapture::configureInfluence(float extentX, float extentY, float extentZ,
                                                float intensity, float blendDistance,
                                                int priority) {
    influenceExtentX_ = std::max(extentX, 0.001f);
    influenceExtentY_ = std::max(extentY, 0.001f);
    influenceExtentZ_ = std::max(extentZ, 0.001f);
    influenceIntensity_ = std::max(intensity, 0.f);
    influenceBlendDistance_ = std::max(blendDistance, 0.f);
    influencePriority_ = priority;
}

bool ReflectionProbeCapture::applyConfiguredToCamera(Camera3D *camera, int slot) {
    if (!camera || !activeCubemap_ || publishedRevision_ == 0) return false;
    camera->setReflectionProbe(slot, activeCubemap_, centerX_, centerY_, centerZ_,
                               influenceExtentX_, influenceExtentY_, influenceExtentZ_,
                               influenceIntensity_, influenceBlendDistance_, influencePriority_);
    return true;
}

bool ReflectionProbeCapture::applyToCamera(Camera3D *camera, int slot, float extentX,
                                           float extentY, float extentZ, float intensity,
                                           float blendDistance, int priority) {
    configureInfluence(extentX, extentY, extentZ, intensity, blendDistance, priority);
    return applyConfiguredToCamera(camera, slot);
}

}  // namespace eve::graphics
