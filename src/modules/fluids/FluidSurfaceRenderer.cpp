#include "fluids/FluidSurfaceRenderer.h"

#include "common/Profile.h"
#include "fluids/FluidSsfKernels.h"
#include "fluids/Fluids.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace eve::fluids {
namespace {

constexpr int kSsfPushVP0          = 0;
constexpr int kSsfPushCount        = 16;
constexpr int kSsfPushOrthographic = 17;
constexpr int kSsfPushNear         = 19;
constexpr int kSsfPushFar          = 20;
constexpr int kSsfPushTanHalf      = 21;
constexpr int kSsfPushAspect       = 22;
constexpr int kSsfPushW            = 23;
constexpr int kSsfPushH            = 24;
constexpr int kSsfPushRadius       = 25;
constexpr int kSsfPushMode         = 26;
constexpr int kSsfPushThick        = 27;
constexpr int kSsfPushFalloff      = 28;
constexpr int kSsfPushBlurRadius   = 29;

constexpr uint32_t kEmptyKey = 0xFFFFFFFFu;
constexpr float    kKeyScale = 16777215.f;

int groupsFor(int count, int localSize = 64) { return (count + localSize - 1) / localSize; }

}  // namespace

FluidSurfaceRenderer::FluidSurfaceRenderer(const FluidSurfaceParams& params, bool preferGpu)
    : params_(params), preferGpu_(preferGpu) {
    // Keep both CPU allocations and Vulkan image-sized buffers bounded even when
    // a native caller bypasses Fluids::newSurfaceRenderer().
    params_.width    = std::clamp(params_.width, 8, 1024);
    params_.height   = std::clamp(params_.height, 8, 1024);
    params_.aspect   = float(params_.width) / float(params_.height);
    const int pixels = params_.width * params_.height;
    depth_.assign(size_t(pixels), 1e30f);
    thickness_.assign(size_t(pixels), 0.f);
    normals_.assign(size_t(pixels), glm::vec3(0.f));
    color_.assign(size_t(pixels) * 4u, 0);
}

FluidSurfaceRenderer::~FluidSurfaceRenderer() { releaseGpu(); }

void FluidSurfaceRenderer::releaseGpu() noexcept {
    delete seq_;
    seq_ = nullptr;
    delete shShade_;
    shShade_ = nullptr;
    delete shColorSplat_;
    shColorSplat_ = nullptr;
    delete shColorClear_;
    shColorClear_ = nullptr;
    delete shNormal_;
    shNormal_ = nullptr;
    delete shSmooth_;
    shSmooth_ = nullptr;
    delete shSplat_;
    shSplat_ = nullptr;
    delete shAnisotropicSplat_;
    shAnisotropicSplat_ = nullptr;
    delete shClear_;
    shClear_ = nullptr;
    delete stColor_;
    stColor_ = nullptr;
    delete stNormal_;
    stNormal_ = nullptr;
    delete stThick_;
    stThick_ = nullptr;
    delete stDepth_;
    stDepth_ = nullptr;
    delete bufColor_;
    bufColor_ = nullptr;
    delete bufColorAccum_;
    bufColorAccum_ = nullptr;
    delete bufParticleColors_;
    bufParticleColors_ = nullptr;
    delete bufNormal_;
    bufNormal_ = nullptr;
    delete bufThick_;
    bufThick_ = nullptr;
    delete bufDepthB_;
    bufDepthB_ = nullptr;
    delete bufDepthA_;
    bufDepthA_ = nullptr;
    delete bufParts_;
    bufParts_ = nullptr;
    delete bufAnisotropicParts_;
    bufAnisotropicParts_ = nullptr;
    multicolorGpuReady_  = false;
    gpuOk_               = false;
}

void FluidSurfaceRenderer::setCamera(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up,
                                     float fovYDeg) {
    params_.eye     = eye;
    params_.target  = target;
    params_.up      = up;
    params_.fovYDeg = fovYDeg;
    params_.aspect  = float(params_.width) / float(params_.height);
    resetReducedRenderers();
}

void FluidSurfaceRenderer::render(const FluidSimulator& sim) {
    std::vector<glm::vec3> pos;
    sim.readPositions(pos);
    render(pos, sim.params().particleRadius);
}

void FluidSurfaceRenderer::renderFrom(FluidSimulator* sim) {
    if (!sim) return;
    render(*sim);
}

Result<void> FluidSurfaceRenderer::configureProjection(bool orthographic, float verticalHalfSize) {
    if (!std::isfinite(verticalHalfSize) || verticalHalfSize < .001f || verticalHalfSize > 10000.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid orthographic vertical half size", "fluids.surface.projection"));
    params_.orthographic     = orthographic;
    params_.orthographicSize = verticalHalfSize;
    resetReducedRenderers();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::prepare() {
    if (surfaceDownsample_ > 1) {
        ensureReducedRenderer();
        auto prepared = reducedRenderer_->prepare();
        if (!prepared) return prepared;
        if (thicknessDownsample_ != surfaceDownsample_) {
            ensureThicknessRenderer();
            return thicknessRenderer_->prepare();
        }
        return Result<void>::success();
    }
    if (preferGpu_ && !gpuOk_) ensureGpu();
    if (thicknessDownsample_ != 1) {
        ensureThicknessRenderer();
        return thicknessRenderer_->prepare();
    }
    return Result<void>::success();
}

void FluidSurfaceRenderer::render(const std::vector<glm::vec3>& positions, float particleRadius) {
    if (surfaceDownsample_ > 1) {
        ensureReducedRenderer();
        reducedRenderer_->render(positions, particleRadius);
        expandReducedOutputs(false);
        return;
    }
    uniformVolumeGpuShade_    = false;
    multicolorVolumeGpuShade_ = false;
    anisotropicFrame_         = false;
    renderInternal(positions, particleRadius);
}

void FluidSurfaceRenderer::renderInternal(const std::vector<glm::vec3>& positions, float particleRadius) {
    EV_PROFILE_MODULE("fluids", "FluidSurfaceRenderer::reconstruct");
    if (&positions != &positions_) positions_ = positions;
    params_.particleRadius = particleRadius;
    auxiliaryCurrent_      = true;
    residentColorCurrent_  = false;
    // Empty emitters and expired pools need no compute submission or readback.
    // Clear every public output so a previously rendered surface cannot persist.
    if (positions_.empty()) {
        std::fill(depth_.begin(), depth_.end(), 1e30f);
        std::fill(thickness_.begin(), thickness_.end(), 0.f);
        std::fill(normals_.begin(), normals_.end(), glm::vec3(0.f));
        std::fill(color_.begin(), color_.end(), uint8_t(0));
        return;
    }
    if (preferGpu_ && !gpuOk_) ensureGpu();
    if (gpuOk_) {
        // GPU path implemented below; the CPU path is exercised by tests.
        const int pixels = params_.width * params_.height;
        if (multicolorVolumeGpuShade_ && !ensureMulticolorGpu()) multicolorVolumeGpuShade_ = false;
        if (anisotropicFrame_ && !ensureAnisotropyGpu()) {
            renderCpu();
            return;
        }
        seq_->begin();
        if (anisotropicFrame_) {
            seq_->recordUpload(bufAnisotropicParts_, anisotropicSplats_.data(),
                               uint64_t(anisotropicSplats_.size()) * sizeof(float));
            if (multicolorVolumeGpuShade_) uploadParticles();
        } else {
            uploadParticles();
        }
        const int groupsPix = groupsFor(pixels);
        setCommonConstants(shClear_, params_.depthFalloff);
        seq_->recordDispatch(shClear_, groupsPix);
        auto* splatShader = anisotropicFrame_ ? shAnisotropicSplat_ : shSplat_;
        setCommonConstants(splatShader, params_.depthFalloff);
        seq_->recordDispatch(splatShader, groupsFor(int(positions_.size())));
        if (multicolorVolumeGpuShade_) {
            seq_->recordUpload(bufParticleColors_, volumeColors_.data(),
                               uint64_t(volumeColors_.size()) * sizeof(glm::vec4));
            setCommonConstants(shColorClear_, params_.depthFalloff);
            seq_->recordDispatch(shColorClear_, groupsPix);
            setCommonConstants(shColorSplat_, params_.depthFalloff);
            seq_->recordDispatch(shColorSplat_, groupsFor(int(positions_.size())));
        }
        bool inA = true;
        for (int it = 0; it < params_.smoothIterations; ++it) {
            shSmooth_->bindBuffer(1, inA ? bufDepthA_ : bufDepthB_);
            shSmooth_->bindBuffer(2, inA ? bufDepthB_ : bufDepthA_);
            setCommonConstants(shSmooth_, params_.depthFalloff);
            seq_->recordDispatch(shSmooth_, groupsPix);
            inA = !inA;
        }
        gpgpu::GpuBuffer* finalDepth = inA ? bufDepthA_ : bufDepthB_;
        setCommonConstants(shNormal_, params_.depthFalloff);
        seq_->recordDispatch(shNormal_, groupsPix);
        setCommonConstants(shShade_, params_.depthFalloff);
        seq_->recordDispatch(shShade_, groupsPix);
        const bool deviceShadedVolume = uniformVolumeGpuShade_ || multicolorVolumeGpuShade_;
        if (!deviceShadedVolume) {
            seq_->recordDownload(finalDepth, stDepth_, uint64_t(pixels) * sizeof(uint32_t));
            seq_->recordDownload(bufThick_, stThick_, uint64_t(pixels) * sizeof(uint32_t));
            seq_->recordDownload(bufNormal_, stNormal_, uint64_t(pixels) * 4u * sizeof(float));
        }
        const bool keepColorResident = deviceShadedVolume && skipHostColorReadback_ &&
                                       bufColor_->residentView().backend == GpuResidentBackend::Vulkan;
        if (!keepColorResident) seq_->recordDownload(bufColor_, stColor_, uint64_t(pixels) * sizeof(uint32_t));
        seq_->submit();

        static_assert(sizeof(float) == sizeof(uint32_t));
        if (!keepColorResident) stColor_->downloadBytes(color_.data(), uint64_t(color_.size()));
        if (deviceShadedVolume) {
            residentColorCurrent_ = true;
            return;
        }
        gpuNormals_.resize(size_t(pixels));
        // Reuse final output storage for packed readback, then decode in place.
        // memcpy avoids aliasing uint keys through float pointers (including the empty sentinel).
        stDepth_->downloadBytes(depth_.data(), uint64_t(depth_.size()) * sizeof(uint32_t));
        stThick_->downloadBytes(thickness_.data(), uint64_t(thickness_.size()) * sizeof(uint32_t));
        stNormal_->downloadBytes(gpuNormals_.data(), uint64_t(gpuNormals_.size()) * sizeof(glm::vec4));
        const float nearZ = params_.nearZ;
        const float farZ  = params_.farZ;
        for (int i = 0; i < pixels; ++i) {
            uint32_t key, thick, rgba;
            std::memcpy(&key, &depth_[size_t(i)], sizeof(key));
            std::memcpy(&thick, &thickness_[size_t(i)], sizeof(thick));
            std::memcpy(&rgba, &color_[size_t(i) * 4u], sizeof(rgba));
            depth_[size_t(i)]          = key == kEmptyKey ? 1e30f : nearZ + (float(key) / kKeyScale) * (farZ - nearZ);
            thickness_[size_t(i)]      = float(thick) / 256.f;
            normals_[size_t(i)]        = glm::vec3(gpuNormals_[size_t(i)]);
            color_[size_t(i) * 4u + 0] = uint8_t(rgba & 255u);
            color_[size_t(i) * 4u + 1] = uint8_t((rgba >> 8u) & 255u);
            color_[size_t(i) * 4u + 2] = uint8_t((rgba >> 16u) & 255u);
            color_[size_t(i) * 4u + 3] = uint8_t(rgba >> 24u);
        }
        if (customShading_) applyConfiguredShading();
        return;
    }
    renderCpu();
}

Result<void> FluidSurfaceRenderer::copyToTexture(graphics::Graphics* graphics, graphics::Texture* texture) {
    if (!graphics || !texture)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing graphics or texture",
                                                       "fluids.surface.copyToTexture"));
    if (residentColorCurrent_ && bufColor_) {
        auto copied =
            graphics->updateTextureFromResidentRgba8(texture, bufColor_->residentView(), params_.width, params_.height);
        if (copied) return copied;
        if (!copied.error() || copied.error()->code() != DiagnosticCode::Unsupported) return copied;
    }
    if (!graphics->updateTexture(texture, params_.width, params_.height, color_.data()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "Fluid texture upload failed", "fluids.surface.copyToTexture"));
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::renderVolumeColorToTexture(const VolumeFluid& sim, graphics::Graphics* graphics,
                                                              graphics::Texture* texture) {
    EV_PROFILE_MODULE("fluids", "FluidSurfaceRenderer::renderToTexture");
    if (!graphics || !texture)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing graphics or texture",
                                                       "fluids.surface.renderVolumeColorToTexture"));
    skipHostColorReadback_ = true;
    try {
        renderVolumeColorOnly(sim);
    } catch (...) {
        skipHostColorReadback_ = false;
        throw;
    }
    skipHostColorReadback_ = false;
    return copyToTexture(graphics, texture);
}

bool FluidSurfaceRenderer::ensureGpu() {
    if (gpuOk_) return true;
    if (gpuAttempted_) return false;
    gpuAttempted_ = true;
    gpgpu_        = eve::gpgpu::Gpgpu::create();
    if (!gpgpu_ || !gpgpu_->isAvailable()) return false;
    const int pixels = params_.width * params_.height;
    const int maxP   = 65536;
    try {
        shClear_   = gpgpu_->newShader(kSsfClear);
        shSplat_   = gpgpu_->newShader(kSsfSplat);
        shSmooth_  = gpgpu_->newShader(kSsfSmooth);
        shNormal_  = gpgpu_->newShader(kSsfNormal);
        shShade_   = gpgpu_->newShader(kSsfShade);
        bufParts_  = gpgpu_->newBuffer(maxP * 4 * int(sizeof(float)), "storage");
        bufDepthA_ = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "storage");
        bufDepthB_ = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "storage");
        bufThick_  = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "storage");
        bufNormal_ = gpgpu_->newBuffer(pixels * 4 * int(sizeof(float)), "storage");
        bufColor_  = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "storage");
        // kSsfShade declares binding 7. Keep a valid minimal descriptor until a
        // multicolor frame transactionally replaces it with the full accumulator.
        bufColorAccum_ = gpgpu_->newBuffer(5 * int(sizeof(uint32_t)), "storage");
        stDepth_   = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "staging");
        stThick_   = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "staging");
        stNormal_  = gpgpu_->newBuffer(pixels * 4 * int(sizeof(float)), "staging");
        stColor_       = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "staging");
        seq_       = gpgpu_->newSequence();
    } catch (...) {
        releaseGpu();
        return false;
    }
    if (!seq_ || !seq_->isAvailable()) {
        releaseGpu();
        return false;
    }
    shClear_->bindBuffer(1, bufDepthA_);
    shClear_->bindBuffer(2, bufDepthB_);
    shClear_->bindBuffer(3, bufThick_);
    shClear_->bindBuffer(4, bufNormal_);
    shClear_->bindBuffer(5, bufColor_);
    shSplat_->bindBuffer(0, bufParts_);
    shSplat_->bindBuffer(1, bufDepthA_);
    shSplat_->bindBuffer(3, bufThick_);
    shSmooth_->bindBuffer(1, bufDepthA_);
    shSmooth_->bindBuffer(2, bufDepthB_);
    // Iteration count is fixed at construction. Keep these descriptors stable
    // across frames; rebinding marks them dirty and reallocates descriptor sets.
    gpgpu::GpuBuffer* finalDepth = std::max(0, params_.smoothIterations) % 2 == 0 ? bufDepthA_ : bufDepthB_;
    shNormal_->bindBuffer(1, finalDepth);
    shNormal_->bindBuffer(4, bufNormal_);
    shShade_->bindBuffer(1, finalDepth);
    shShade_->bindBuffer(3, bufThick_);
    shShade_->bindBuffer(4, bufNormal_);
    shShade_->bindBuffer(5, bufColor_);
    shShade_->bindBuffer(7, bufColorAccum_);
    gpuOk_ = true;
    return true;
}

bool FluidSurfaceRenderer::ensureMulticolorGpu() {
    if (multicolorGpuReady_) return true;
    if (!gpuOk_ || !gpgpu_) return false;
    gpgpu::ComputeShader* colorClear     = nullptr;
    gpgpu::ComputeShader* colorSplat     = nullptr;
    gpgpu::GpuBuffer*     particleColors = nullptr;
    gpgpu::GpuBuffer*     colorAccum     = nullptr;
    try {
        colorClear     = gpgpu_->newShader(kSsfColorClear);
        colorSplat     = gpgpu_->newShader(kSsfColorSplat);
        particleColors = gpgpu_->newBuffer(65536 * 4 * int(sizeof(float)), "storage");
        colorAccum     = gpgpu_->newBuffer(params_.width * params_.height * 5 * int(sizeof(uint32_t)), "storage");
        colorClear->bindBuffer(7, colorAccum);
        colorSplat->bindBuffer(0, bufParts_);
        colorSplat->bindBuffer(6, particleColors);
        colorSplat->bindBuffer(7, colorAccum);
        shShade_->bindBuffer(7, colorAccum);
    } catch (...) {
        delete colorAccum;
        delete particleColors;
        delete colorSplat;
        delete colorClear;
        return false;
    }
    delete bufColorAccum_;
    bufColorAccum_      = colorAccum;
    bufParticleColors_  = particleColors;
    shColorSplat_       = colorSplat;
    shColorClear_       = colorClear;
    multicolorGpuReady_ = true;
    return true;
}

bool FluidSurfaceRenderer::ensureAnisotropyGpu() {
    if (shAnisotropicSplat_ && bufAnisotropicParts_) return true;
    if (!gpuOk_ || !gpgpu_) return false;
    try {
        shAnisotropicSplat_  = gpgpu_->newShader(kSsfAnisotropicSplat);
        bufAnisotropicParts_ = gpgpu_->newBuffer(65536 * 12 * int(sizeof(float)), "storage");
        if (!shAnisotropicSplat_ || !bufAnisotropicParts_) throw std::runtime_error("anisotropy GPU resource");
        shAnisotropicSplat_->bindBuffer(0, bufAnisotropicParts_);
        shAnisotropicSplat_->bindBuffer(1, bufDepthA_);
        shAnisotropicSplat_->bindBuffer(3, bufThick_);
        return true;
    } catch (...) {
        delete shAnisotropicSplat_;
        shAnisotropicSplat_ = nullptr;
        delete bufAnisotropicParts_;
        bufAnisotropicParts_ = nullptr;
        return false;
    }
}

void FluidSurfaceRenderer::buildAnisotropicSplats() {
    const size_t count = std::min(positions_.size(), size_t(65536));
    anisotropicSplats_.assign(count * 12u, 0.f);
    const auto      view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const glm::mat3 viewRotation(view);
    const float projectionScale = params_.orthographic ? params_.orthographicSize
                                                       : std::max(std::tan(glm::radians(params_.fovYDeg) * .5f), 1e-4f);
    const float focal           = float(params_.height) * .5f / projectionScale;
    for (size_t i = 0; i < count; ++i) {
        const auto  center = view * glm::vec4(positions_[i], 1.f);
        const float z      = -center.z;
        if (z <= params_.nearZ || z >= params_.farZ) continue;
        const glm::vec3 radii    = glm::max(particleRadii_[i], glm::vec3(.0005f));
        const auto      q        = glm::normalize(glm::quat(particleOrientations_[i].w, particleOrientations_[i].x,
                                                            particleOrientations_[i].y, particleOrientations_[i].z));
        const glm::mat3 rotation = viewRotation * glm::mat3_cast(q);
        const glm::mat3 covariance =
            rotation *
            glm::mat3(radii.x * radii.x, 0.f, 0.f, 0.f, radii.y * radii.y, 0.f, 0.f, 0.f, radii.z * radii.z) *
            glm::transpose(rotation);
        const float     invZ = params_.orthographic ? 1.f : 1.f / z;
        const glm::vec3 jx(focal * invZ, 0.f, params_.orthographic ? 0.f : focal * center.x * invZ * invZ);
        const glm::vec3 jy(0.f, -focal * invZ, params_.orthographic ? 0.f : -focal * center.y * invZ * invZ);
        const glm::vec3 cjx         = covariance * jx;
        const glm::vec3 cjy         = covariance * jy;
        const float     s00         = glm::dot(jx, cjx);
        const float     s01         = glm::dot(jx, cjy);
        const float     s11         = glm::dot(jy, cjy);
        const float     determinant = s00 * s11 - s01 * s01;
        if (!std::isfinite(determinant) || determinant <= 1e-12f) continue;
        const float inv00               = s11 / determinant;
        const float inv01               = -s01 / determinant;
        const float inv11               = s00 / determinant;
        const float covZx               = cjx.z;
        const float covZy               = cjy.z;
        const float slopeX              = covZx * inv00 + covZy * inv01;
        const float slopeY              = covZx * inv01 + covZy * inv11;
        const float conditionalVariance = std::max(covariance[2][2] - (slopeX * covZx + slopeY * covZy), 1e-10f);
        const float trace               = s00 + s11;
        const float eigenMax = .5f * (trace + std::sqrt(std::max(0.f, (s00 - s11) * (s00 - s11) + 4.f * s01 * s01)));
        const float bound    = std::sqrt(std::max(eigenMax, 0.f)) + 1.f;
        const float divisor  = params_.orthographic ? 1.f : z;
        const float sx  = (.5f + .5f * center.x / (divisor * projectionScale * params_.aspect)) * float(params_.width);
        const float sy  = (.5f - .5f * center.y / (divisor * projectionScale)) * float(params_.height);
        float*      out = anisotropicSplats_.data() + i * 12u;
        out[0]          = sx;
        out[1]          = sy;
        out[2]          = z;
        out[3]          = bound;
        out[4]          = inv00;
        out[5]          = inv01;
        out[6]          = inv11;
        out[7]          = std::sqrt(conditionalVariance);
        out[8]          = slopeX;
        out[9]          = slopeY;
    }
}

void FluidSurfaceRenderer::uploadParticles() {
    if (!gpuOk_) return;
    const size_t count = std::min(positions_.size(), size_t(65536));
    gpuParticles_.resize(count * 4u);
    for (size_t i = 0; i < count; ++i) {
        gpuParticles_[i * 4u + 0] = positions_[i].x;
        gpuParticles_[i * 4u + 1] = positions_[i].y;
        gpuParticles_[i * 4u + 2] = positions_[i].z;
        gpuParticles_[i * 4u + 3] = params_.particleRadius;
    }
    seq_->recordUpload(bufParts_, gpuParticles_.data(), uint64_t(gpuParticles_.size()) * sizeof(float));
}

Result<void> FluidSurfaceRenderer::occludeWithSceneDepth(std::span<const float> sceneDepth, float depthBias) {
    const size_t pixels = size_t(params_.width) * size_t(params_.height);
    if (!auxiliaryCurrent_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Current color frame has no host-visible depth",
                                                       "fluids.surface.sceneDepth"));
    if (sceneDepth.size() != pixels || !std::isfinite(depthBias) || depthBias < 0.f || depthBias > 1.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Scene depth dimensions or bias are invalid",
                                                       "fluids.surface.sceneDepth"));
    for (float value : sceneDepth) {
        if (!std::isfinite(value) || value < 0.f)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "Scene depth must contain finite nonnegative view depths",
                                                           "fluids.surface.sceneDepth"));
    }
    for (size_t i = 0; i < pixels; ++i) {
        if (color_[i * 4u + 3u] == 0 || sceneDepth[i] + depthBias > depth_[i]) continue;
        color_[i * 4u + 0u] = color_[i * 4u + 1u] = color_[i * 4u + 2u] = color_[i * 4u + 3u] = 0;
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::compositeSceneRefraction(std::span<const uint8_t> sceneColor, float distortion,
                                                            float absorption) {
    const int    W      = params_.width;
    const int    H      = params_.height;
    const size_t pixels = size_t(W) * size_t(H);
    if (!auxiliaryCurrent_)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "Current color frame has no host-visible surface auxiliaries",
                                                       "fluids.surface.refraction"));
    if (sceneColor.size() != pixels * 4u || !std::isfinite(distortion) || distortion < 0.f || distortion > 64.f ||
        !std::isfinite(absorption) || absorption < 0.f || absorption > 30.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Scene color dimensions or refraction parameters are invalid",
                                                       "fluids.surface.refraction"));

    for (size_t i = 0; i < pixels; ++i) {
        const size_t rgba     = i * 4u;
        const float  coverage = float(color_[rgba + 3u]) / 255.f;
        if (coverage <= 0.f) {
            color_[rgba + 0u] = sceneColor[rgba + 0u];
            color_[rgba + 1u] = sceneColor[rgba + 1u];
            color_[rgba + 2u] = sceneColor[rgba + 2u];
            color_[rgba + 3u] = 255u;
            continue;
        }
        const int    x            = int(i % size_t(W));
        const int    y            = int(i / size_t(W));
        const float  bend         = distortion * std::clamp(thickness_[i] * 4.f, 0.f, 1.f);
        const int    sx           = std::clamp(int(std::lround(float(x) + normals_[i].x * bend)), 0, W - 1);
        const int    sy           = std::clamp(int(std::lround(float(y) - normals_[i].y * bend)), 0, H - 1);
        const size_t refracted    = (size_t(sy) * size_t(W) + size_t(sx)) * 4u;
        const float  transmission = std::exp(-absorption * std::max(thickness_[i], 0.f));
        for (size_t channel = 0; channel < 3u; ++channel) {
            const float through    = float(sceneColor[refracted + channel]);
            const float liquid     = float(color_[rgba + channel]);
            const float absorbed   = through * transmission + liquid * (1.f - transmission);
            const float composited = float(sceneColor[rgba + channel]) * (1.f - coverage) + absorbed * coverage;
            color_[rgba + channel] = uint8_t(std::clamp(composited, 0.f, 255.f));
        }
        color_[rgba + 3u] = 255u;
    }
    residentColorCurrent_ = false;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureSurface(float thicknessScale, float thicknessCutoff, float depthFalloff,
                                                    int smoothIterations) {
    if (!std::isfinite(thicknessScale) || thicknessScale < 0.f || thicknessScale > 16.f ||
        !std::isfinite(thicknessCutoff) || thicknessCutoff < 0.f || thicknessCutoff > 5.f ||
        !std::isfinite(depthFalloff) || depthFalloff <= 0.f || depthFalloff > 1.f || smoothIterations < 0 ||
        smoothIterations > 8)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid fluid surface controls", "fluids.surface.configureSurface"));
    params_.thicknessScale   = thicknessScale;
    thicknessCutoff_         = thicknessCutoff;
    params_.depthFalloff     = depthFalloff;
    params_.smoothIterations = smoothIterations;
    resetReducedRenderers();
    refreshCustomShadingFlag();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureSurfaceEnabled(bool enabled) {
    surfaceEnabled_ = enabled;
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureRendererSettings(const FluidRendererSettings& settings) {
    const auto finiteRange = [](float value, float minimum, float maximum) {
        return std::isfinite(value) && value >= minimum && value <= maximum;
    };
    const auto blend = [](int value) { return value >= 0 && value <= 10; };
    if (!blend(settings.blendSource) || !blend(settings.blendDestination) || !blend(settings.particleBlendSource) ||
        !blend(settings.particleBlendDestination) || !finiteRange(settings.thicknessCutoff, .01f, 5.f) ||
        settings.thicknessDownsample < 1 || settings.thicknessDownsample > 4 ||
        !finiteRange(settings.blurRadius, 0.f, .1f) || settings.surfaceDownsample < 1 ||
        settings.surfaceDownsample > 4 || !finiteRange(settings.smoothness, 0.f, 1.f) ||
        !finiteRange(settings.metalness, 0.f, 1.f) || !finiteRange(settings.ambientMultiplier, 0.f, 6.f) ||
        !finiteRange(settings.reflection, 0.f, 1.f) || !finiteRange(settings.transparency, 0.f, 1.f) ||
        !finiteRange(settings.absorption, 0.f, 30.f) || !finiteRange(settings.refraction, -.1f, .1f) ||
        settings.refractionDownsample < 1 || settings.refractionDownsample > 4 || settings.foamDownsample < 1 ||
        settings.foamDownsample > 4)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid Fluid3D renderer settings", "fluids.surface.rendererSettings"));

    surfaceBlendSource_       = static_cast<FluidBlendFactor>(settings.blendSource);
    surfaceBlendDestination_  = static_cast<FluidBlendFactor>(settings.blendDestination);
    particleBlendSource_      = static_cast<FluidBlendFactor>(settings.particleBlendSource);
    particleBlendDestination_ = static_cast<FluidBlendFactor>(settings.particleBlendDestination);
    particleDepthWrite_       = settings.particleZWrite;
    particleBlendConfigured_  = true;
    thicknessCutoff_          = settings.thicknessCutoff;
    thicknessDownsample_      = settings.thicknessDownsample;
    surfaceEnabled_           = settings.generateSurface;
    surfaceBlurRadius_        = settings.blurRadius;
    surfaceDownsample_        = settings.surfaceDownsample;
    lighting_                 = settings.lighting;
    smoothness_               = settings.smoothness;
    metalness_                = settings.metalness;
    ambientMultiplier_        = settings.ambientMultiplier;
    reflectionEnabled_        = settings.generateReflection;
    reflection_               = settings.reflection;
    refractionEnabled_        = settings.generateRefraction;
    refractionTransparency_   = settings.transparency;
    refractionAbsorption_     = settings.absorption;
    refractionCoefficient_    = settings.refraction;
    refractionDownsample_     = settings.refractionDownsample;
    foamEnabled_              = settings.generateFoam;
    foamDownsample_           = settings.foamDownsample;
    resetReducedRenderers();
    refreshCustomShadingFlag();
    return Result<void>::success();
}

FluidRendererSettings FluidSurfaceRenderer::rendererSettings() const noexcept {
    FluidRendererSettings settings;
    settings.blendSource              = int(surfaceBlendSource_);
    settings.blendDestination         = int(surfaceBlendDestination_);
    settings.particleBlendSource      = int(particleBlendSource_);
    settings.particleBlendDestination = int(particleBlendDestination_);
    settings.particleZWrite           = particleDepthWrite_;
    settings.thicknessCutoff          = thicknessCutoff_;
    settings.thicknessDownsample      = thicknessDownsample_;
    settings.generateSurface          = surfaceEnabled_;
    settings.blurRadius               = surfaceBlurRadius_;
    settings.surfaceDownsample        = surfaceDownsample_;
    settings.lighting                 = lighting_;
    settings.smoothness               = smoothness_;
    settings.metalness                = metalness_;
    settings.ambientMultiplier        = ambientMultiplier_;
    settings.generateReflection       = reflectionEnabled_;
    settings.reflection               = reflection_;
    settings.generateRefraction       = refractionEnabled_;
    settings.transparency             = refractionTransparency_;
    settings.absorption               = refractionAbsorption_;
    settings.refraction               = refractionCoefficient_;
    settings.refractionDownsample     = refractionDownsample_;
    settings.generateFoam             = foamEnabled_;
    settings.foamDownsample           = foamDownsample_;
    return settings;
}

Result<void> FluidSurfaceRenderer::configureMaterial(bool lighting, float smoothness, float metalness,
                                                     float ambientMultiplier, float reflection, float opacity) {
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; };
    if (!unit(smoothness) || !unit(metalness) || !std::isfinite(ambientMultiplier) || ambientMultiplier < 0.f ||
        ambientMultiplier > 6.f || !unit(reflection) || !std::isfinite(opacity) || opacity < 0.f || opacity > 30.f)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Invalid fluid material controls", "fluids.surface.configureMaterial"));
    lighting_          = lighting;
    smoothness_        = smoothness;
    metalness_         = metalness;
    ambientMultiplier_ = ambientMultiplier;
    reflection_        = reflection;
    opacity_           = opacity;
    resetReducedRenderers();
    refreshCustomShadingFlag();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureSurfaceBlurRadius(float radius) {
    if (!std::isfinite(radius) || radius < 0.f || radius > .1f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Surface blur radius must be finite and within [0,0.1]",
                                                       "fluids.surface.blurRadius"));
    surfaceBlurRadius_ = radius;
    resetReducedRenderers();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureReflection(bool enabled) {
    if (reflectionEnabled_ == enabled) return Result<void>::success();
    reflectionEnabled_ = enabled;
    resetReducedRenderers();
    refreshCustomShadingFlag();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureColors(const glm::vec3& baseColor, const glm::vec3& reflectionColor) {
    const auto valid = [](const glm::vec3& c) {
        return std::isfinite(c.x) && std::isfinite(c.y) && std::isfinite(c.z) && c.x >= 0.f && c.y >= 0.f &&
               c.z >= 0.f && c.x <= 1.f && c.y <= 1.f && c.z <= 1.f;
    };
    if (!valid(baseColor) || !valid(reflectionColor))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Fluid colors must be finite linear RGB values in [0,1]",
                                                       "fluids.surface.configureColors"));
    baseColor_       = baseColor;
    reflectionColor_ = reflectionColor;
    resetReducedRenderers();
    refreshCustomShadingFlag();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureAnisotropy(bool enabled) {
    anisotropyEnabled_ = enabled;
    if (!enabled) anisotropicFrame_ = false;
    resetReducedRenderers();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureSurfaceDownsample(int factor) {
    if (factor < 1 || factor > 4)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Surface downsample must be in [1,4]", "fluids.surface.downsample"));
    if (factor == surfaceDownsample_) return Result<void>::success();
    surfaceDownsample_ = factor;
    resetReducedRenderers();
    return Result<void>::success();
}

Result<void> FluidSurfaceRenderer::configureThicknessDownsample(int factor) {
    if (factor < 1 || factor > 4)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "Thickness downsample must be in [1,4]",
                                                       "fluids.surface.thicknessDownsample"));
    if (factor == thicknessDownsample_) return Result<void>::success();
    thicknessDownsample_ = factor;
    thicknessRenderer_.reset();
    return Result<void>::success();
}

void FluidSurfaceRenderer::resetReducedRenderers() {
    reducedRenderer_.reset();
    thicknessRenderer_.reset();
}

void FluidSurfaceRenderer::ensureReducedRenderer() {
    if (reducedRenderer_ || surfaceDownsample_ <= 1) return;
    auto reduced                         = params_;
    reduced.width                        = std::max(8, (params_.width + surfaceDownsample_ - 1) / surfaceDownsample_);
    reduced.height                       = std::max(8, (params_.height + surfaceDownsample_ - 1) / surfaceDownsample_);
    reduced.aspect                       = float(reduced.width) / float(reduced.height);
    reducedRenderer_                     = std::make_unique<FluidSurfaceRenderer>(reduced, preferGpu_);
    reducedRenderer_->thicknessCutoff_   = thicknessCutoff_;
    reducedRenderer_->surfaceBlurRadius_ = surfaceBlurRadius_;
    reducedRenderer_->lighting_          = lighting_;
    reducedRenderer_->smoothness_        = smoothness_;
    reducedRenderer_->metalness_         = metalness_;
    reducedRenderer_->ambientMultiplier_ = ambientMultiplier_;
    reducedRenderer_->reflection_        = reflection_;
    reducedRenderer_->reflectionEnabled_ = reflectionEnabled_;
    reducedRenderer_->opacity_           = opacity_;
    reducedRenderer_->baseColor_         = baseColor_;
    reducedRenderer_->reflectionColor_   = reflectionColor_;
    reducedRenderer_->customShading_     = customShading_;
    reducedRenderer_->anisotropyEnabled_ = anisotropyEnabled_;
}

void FluidSurfaceRenderer::expandReducedOutputs(bool colorOnly) {
    const int sourceWidth  = reducedRenderer_->getWidth();
    const int sourceHeight = reducedRenderer_->getHeight();
    const int width        = params_.width;
    const int height       = params_.height;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int    sx     = std::min(sourceWidth - 1, x * sourceWidth / width);
            const int    sy     = std::min(sourceHeight - 1, y * sourceHeight / height);
            const size_t source = size_t(sy) * size_t(sourceWidth) + size_t(sx);
            const size_t target = size_t(y) * size_t(width) + size_t(x);
            std::memcpy(color_.data() + target * 4u, reducedRenderer_->color().data() + source * 4u, 4u);
            if (!colorOnly) {
                depth_[target]     = reducedRenderer_->depth()[source];
                thickness_[target] = reducedRenderer_->thickness()[source];
                normals_[target]   = reducedRenderer_->normals()[source];
            }
        }
    auxiliaryCurrent_ = !colorOnly;
}

void FluidSurfaceRenderer::ensureThicknessRenderer() {
    if (thicknessRenderer_) return;
    auto reduced       = params_;
    reduced.width      = std::max(8, (params_.width + thicknessDownsample_ - 1) / thicknessDownsample_);
    reduced.height     = std::max(8, (params_.height + thicknessDownsample_ - 1) / thicknessDownsample_);
    reduced.aspect     = float(reduced.width) / float(reduced.height);
    thicknessRenderer_ = std::make_unique<FluidSurfaceRenderer>(reduced, preferGpu_);
    thicknessRenderer_->anisotropyEnabled_ = anisotropyEnabled_;
}

void FluidSurfaceRenderer::replaceThicknessFromReduced() {
    const int sourceWidth  = thicknessRenderer_->getWidth();
    const int sourceHeight = thicknessRenderer_->getHeight();
    for (int y = 0; y < params_.height; ++y)
        for (int x = 0; x < params_.width; ++x) {
            const int sx = std::min(sourceWidth - 1, x * sourceWidth / params_.width);
            const int sy = std::min(sourceHeight - 1, y * sourceHeight / params_.height);
            thickness_[size_t(y) * size_t(params_.width) + size_t(x)] =
                thicknessRenderer_->thickness()[size_t(sy) * size_t(sourceWidth) + size_t(sx)];
        }
}

void FluidSurfaceRenderer::refreshCustomShadingFlag() {
    customShading_ = thicknessCutoff_ != 0.f || !lighting_ || smoothness_ != .8f || metalness_ != 0.f ||
                     ambientMultiplier_ != .55f || reflection_ != .75f || opacity_ != .35f || !reflectionEnabled_ ||
                     baseColor_ != glm::vec3(.05f, .32f, .72f) || reflectionColor_ != glm::vec3(.55f, .72f, 1.f);
}

void FluidSurfaceRenderer::applyConfiguredShading() {
    const glm::vec3 L = glm::normalize(glm::vec3(.35f, .65f, .55f));
    const glm::vec3 V(0.f, 0.f, 1.f);
    for (size_t i = 0; i < thickness_.size(); ++i) {
        const size_t rgba = i * 4u;
        if (depth_[i] >= 1e29f || thickness_[i] * 10.f < thicknessCutoff_) {
            color_[rgba] = color_[rgba + 1u] = color_[rgba + 2u] = color_[rgba + 3u] = 0;
            continue;
        }
        if (params_.mode == 1) continue;
        const glm::vec3 n    = normals_[i];
        const float     diff = std::max(glm::dot(n, L), 0.f);
        const float     lit =
            lighting_ ? std::clamp(ambientMultiplier_ + (1.f - std::min(ambientMultiplier_, 1.f)) * diff, 0.f, 6.f)
                      : 1.f;
        const float     fresnel  = .04f + .96f * std::pow(1.f - std::max(glm::dot(n, V), 0.f), 5.f);
        const glm::vec3 hv       = glm::normalize(L + V);
        const float     exponent = 4.f + 124.f * smoothness_;
        const float spec = lighting_ ? std::pow(std::max(glm::dot(n, hv), 0.f), exponent) * smoothness_ * .56f : 0.f;
        const glm::vec3 reflected          = glm::mix(reflectionColor_, baseColor_, metalness_);
        const float     reflectionStrength = reflectionEnabled_ ? reflection_ : 0.f;
        const glm::vec3 shaded = baseColor_ * lit + reflected * fresnel * reflectionStrength + glm::vec3(spec);
        for (size_t c = 0; c < 3u; ++c) color_[rgba + c] = uint8_t(255.f * std::clamp(shaded[int(c)], 0.f, 1.f));
        color_[rgba + 3u] = uint8_t(255.f * std::clamp(thickness_[i] * opacity_, 0.f, 1.f));
    }
}

void FluidSurfaceRenderer::setCommonConstants(gpgpu::ComputeShader* shader, float falloff) {
    if (!shader) return;
    const glm::mat4 view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const glm::mat4 proj =
        params_.orthographic
            ? glm::orthoRH(-params_.orthographicSize * params_.aspect, params_.orthographicSize * params_.aspect,
                           -params_.orthographicSize, params_.orthographicSize, params_.nearZ, params_.farZ)
            : glm::perspectiveRH(glm::radians(params_.fovYDeg), params_.aspect, params_.nearZ, params_.farZ);
    const glm::mat4 vp    = proj * view;
    const float*    vpPtr = &vp[0][0];
    for (int i = 0; i < 16; ++i) shader->setFloat(kSsfPushVP0 + i, vpPtr[i]);
    shader->setFloat(kSsfPushCount, float(std::min(positions_.size(), size_t(65536))));
    shader->setFloat(kSsfPushOrthographic, params_.orthographic ? 1.f : 0.f);
    shader->setFloat(kSsfPushNear, params_.nearZ);
    shader->setFloat(kSsfPushFar, params_.farZ);
    shader->setFloat(kSsfPushTanHalf,
                     params_.orthographic ? params_.orthographicSize : std::tan(glm::radians(params_.fovYDeg) * .5f));
    shader->setFloat(kSsfPushAspect, params_.aspect);
    shader->setFloat(kSsfPushW, float(params_.width));
    shader->setFloat(kSsfPushH, float(params_.height));
    shader->setFloat(kSsfPushRadius, params_.particleRadius);
    shader->setFloat(kSsfPushMode, float(params_.mode));
    shader->setFloat(kSsfPushThick, params_.thicknessScale);
    shader->setFloat(kSsfPushFalloff, falloff);
    shader->setFloat(kSsfPushBlurRadius, surfaceBlurRadius_);
    if (shader == shShade_ && (uniformVolumeGpuShade_ || multicolorVolumeGpuShade_)) {
        shader->setFloat(kSsfPushMode, float(params_.mode + (multicolorVolumeGpuShade_ ? 20 : 10)));
        shader->setFloat(0, uniformVolumeColor_.r);
        shader->setFloat(1, uniformVolumeColor_.g);
        shader->setFloat(2, uniformVolumeColor_.b);
        shader->setFloat(3, uniformVolumeColor_.a);
        shader->setFloat(4, reflectionColor_.r);
        shader->setFloat(5, reflectionColor_.g);
        shader->setFloat(6, reflectionColor_.b);
        shader->setFloat(7, lighting_ ? 1.f : 0.f);
        shader->setFloat(8, smoothness_);
        shader->setFloat(9, metalness_);
        shader->setFloat(10, ambientMultiplier_);
        shader->setFloat(11, reflectionEnabled_ ? reflection_ : 0.f);
        shader->setFloat(12, opacity_);
        shader->setFloat(13, thicknessCutoff_);
    }
}

void FluidSurfaceRenderer::renderCpu() {
    const int   W      = params_.width;
    const int   H      = params_.height;
    const int   pixels = W * H;
    const float nearZ  = params_.nearZ;
    const float farZ   = params_.farZ;
    const float tanHalf =
        params_.orthographic ? params_.orthographicSize : std::tan(glm::radians(params_.fovYDeg) * .5f);
    const glm::mat4 view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const glm::mat4 proj =
        params_.orthographic
            ? glm::orthoRH(-tanHalf * params_.aspect, tanHalf * params_.aspect, -tanHalf, tanHalf, nearZ, farZ)
            : glm::perspectiveRH(glm::radians(params_.fovYDeg), params_.aspect, nearZ, farZ);
    std::fill(depth_.begin(), depth_.end(), 1e30f);
    std::fill(thickness_.begin(), thickness_.end(), 0.f);
    depthScratch_.resize(size_t(pixels));

    // 1. Splat.
    if (anisotropicFrame_) {
        for (size_t i = 0; i < positions_.size(); ++i) {
            const float* splat = anisotropicSplats_.data() + i * 12u;
            const float  sx = splat[0], sy = splat[1], depthVal = splat[2], bound = splat[3];
            if (bound <= 0.f) continue;
            const int x0 = std::max(int(std::floor(sx - bound)), 0), x1 = std::min(int(std::ceil(sx + bound)), W - 1);
            const int y0 = std::max(int(std::floor(sy - bound)), 0), y1 = std::min(int(std::ceil(sy + bound)), H - 1);
            for (int yy = y0; yy <= y1; ++yy)
                for (int xx = x0; xx <= x1; ++xx) {
                    const float dx = float(xx) + .5f - sx, dy = float(yy) + .5f - sy;
                    const float q = splat[4] * dx * dx + 2.f * splat[5] * dx * dy + splat[6] * dy * dy;
                    if (q >= 1.f) continue;
                    const float  cap         = splat[7] * std::sqrt(std::max(0.f, 1.f - q));
                    const float  centerDepth = depthVal + splat[8] * dx + splat[9] * dy;
                    const size_t at          = size_t(yy) * size_t(W) + size_t(xx);
                    depth_[at]               = std::min(depth_[at], std::clamp(centerDepth - cap, nearZ, farZ));
                    thickness_[at] += 2.f * cap * params_.thicknessScale;
                }
        }
    } else
        for (const glm::vec3& p : positions_) {
            const auto      viewPosition = view * glm::vec4(p, 1.f);
            const glm::vec4 clip         = proj * viewPosition;
            if (!params_.orthographic && clip.w <= 1e-4f) continue;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (glm::any(glm::lessThan(ndc, glm::vec3(-1.f))) || glm::any(glm::greaterThan(ndc, glm::vec3(1.f))))
                continue;
            const float sx       = (ndc.x * 0.5f + 0.5f) * float(W);
            const float sy       = (0.5f - ndc.y * 0.5f) * float(H);
            const float depthVal = -viewPosition.z;
            const float radiusPx = (params_.particleRadius * (float(H) * .5f) / tanHalf) /
                                   (params_.orthographic ? 1.f : std::max(depthVal, 1e-4f));
            if (radiusPx < 0.5f) continue;
            const int   x0 = std::max(int(std::floor(sx - radiusPx)), 0);
            const int   x1 = std::min(int(std::ceil(sx + radiusPx)), W - 1);
            const int   y0 = std::max(int(std::floor(sy - radiusPx)), 0);
            const int   y1 = std::min(int(std::ceil(sy + radiusPx)), H - 1);
            const float r2 = radiusPx * radiusPx;
            for (int yy = y0; yy <= y1; ++yy) {
                for (int xx = x0; xx <= x1; ++xx) {
                    const float ddx = float(xx) + 0.5f - sx;
                    const float ddy = float(yy) + 0.5f - sy;
                    const float q   = (ddx * ddx + ddy * ddy) / r2;
                    if (q >= 1.f) continue;
                    const size_t idx          = size_t(yy) * size_t(W) + size_t(xx);
                    const float  cap          = params_.particleRadius * std::sqrt(1.f - q);
                    const float  surfaceDepth = std::clamp(depthVal - cap, params_.nearZ, params_.farZ);
                    if (surfaceDepth < depth_[idx]) depth_[idx] = surfaceDepth;
                    thickness_[idx] += 2.f * cap * params_.thicknessScale;
                }
            }
        }

    // 2. Bilateral smooth (ping-pong).
    for (int it = 0; it < params_.smoothIterations; ++it) {
        const std::vector<float>& src = (it % 2 == 0) ? depth_ : depthScratch_;
        std::vector<float>&       dst = (it % 2 == 0) ? depthScratch_ : depth_;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t idx = size_t(y) * size_t(W) + size_t(x);
                if (src[idx] >= 1e29f) {
                    dst[idx] = src[idx];
                    continue;
                }
                float sum  = src[idx];
                float wsum = 1.f;
                const float projectedBlur =
                    surfaceBlurRadius_ < 0.f
                        ? 2.f
                        : surfaceBlurRadius_ * float(H) / (2.f * tanHalf * (params_.orthographic ? 1.f : src[idx]));
                const int   kernelRadius = std::clamp(int(std::ceil(projectedBlur)), 0, 4);
                const float sigma        = std::max(.5f, projectedBlur * .5f);
                for (int oy = -kernelRadius; oy <= kernelRadius; ++oy) {
                    for (int ox = -kernelRadius; ox <= kernelRadius; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        const int xx = x + ox;
                        const int yy = y + oy;
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        const size_t nidx = size_t(yy) * size_t(W) + size_t(xx);
                        if (src[nidx] >= 1e29f) continue;
                        const float spatial = std::exp(-float(ox * ox + oy * oy) / (2.f * sigma * sigma));
                        const float wDepth  = std::exp(-std::fabs(src[nidx] - src[idx]) / params_.depthFalloff);
                        const float w       = spatial * wDepth;
                        sum += src[nidx] * w;
                        wsum += w;
                    }
                }
                dst[idx] = sum / std::max(wsum, 1e-5f);
            }
        }
    }
    const std::vector<float>& smooth = (params_.smoothIterations % 2 == 0) ? depth_ : depthScratch_;

    // 3. Normals from depth gradients (view space).
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * size_t(W) + size_t(x);
            if (smooth[idx] >= 1e29f || thickness_[idx] * 10.f < thicknessCutoff_) {
                normals_[idx] = glm::vec3(0.f);
                continue;
            }
            const auto viewPos = [&](int px, int py, float d) {
                const float u     = (float(px) + 0.5f) / float(W);
                const float v     = (float(py) + 0.5f) / float(H);
                const float scale = params_.orthographic ? 1.f : d;
                return glm::vec3((u * 2.f - 1.f) * params_.aspect * tanHalf * scale, (1.f - v * 2.f) * tanHalf * scale,
                                 -d);
            };
            const auto sd = [&](int ox, int oy) {
                const int xx = x + ox, yy = y + oy;
                if (xx < 0 || yy < 0 || xx >= W || yy >= H) return 1e30f;
                return smooth[size_t(yy) * size_t(W) + size_t(xx)];
            };
            const auto derivative = [&](glm::vec3 forward, glm::vec3 backward, bool forwardValid, bool backwardValid) {
                if (!forwardValid && backwardValid) return backward;
                if (!backwardValid && forwardValid) return forward;
                const float tolerance = 2.f * (params_.farZ - params_.nearZ) / 16777215.f;
                if ((!forwardValid && !backwardValid) ||
                    std::abs(std::abs(forward.z) - std::abs(backward.z)) <= tolerance)
                    return (forward + backward) * .5f;
                return std::abs(forward.z) < std::abs(backward.z) ? forward : backward;
            };
            const float dl = sd(-1, 0), dr = sd(1, 0), dt = sd(0, -1), db = sd(0, 1);
            const auto  center = viewPos(x, y, smooth[idx]);
            const auto  pL     = viewPos(x - 1, y, dl < 1e29f ? dl : smooth[idx]);
            const auto  pR     = viewPos(x + 1, y, dr < 1e29f ? dr : smooth[idx]);
            const auto  pT     = viewPos(x, y - 1, dt < 1e29f ? dt : smooth[idx]);
            const auto  pB     = viewPos(x, y + 1, db < 1e29f ? db : smooth[idx]);
            const auto  dpx    = derivative(pR - center, center - pL, dr < 1e29f, dl < 1e29f);
            const auto  dpy    = derivative(pB - center, center - pT, db < 1e29f, dt < 1e29f);
            glm::vec3   n      = glm::normalize(glm::cross(dpx, dpy));
            if (n.z < 0.f) n = -n;
            normals_[idx] = n;
        }
    }

    // 4. Shade.
    const glm::vec3 L(0.35f, 0.65f, 0.55f);
    const glm::vec3 V(0.f, 0.f, 1.f);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * size_t(W) + size_t(x);
            if (smooth[idx] >= 1e29f || thickness_[idx] * 10.f < thicknessCutoff_) {
                color_[idx * 4u + 0] = 0;
                color_[idx * 4u + 1] = 0;
                color_[idx * 4u + 2] = 0;
                color_[idx * 4u + 3] = 0;
                continue;
            }
            const glm::vec3 n    = normals_[idx];
            const float     diff = std::max(glm::dot(n, L), 0.f);
            glm::vec3       outC;
            float           alpha;
            if (params_.mode == 1) {
                const glm::vec3 base        = glm::vec3(0.36f, 0.23f, 0.12f) * (0.45f + 0.55f * diff);
                const float     attenuation = std::exp(-thickness_[idx] * 1.8f);
                const glm::vec3 hv          = glm::normalize(L + V);
                const float     spec        = std::pow(std::max(glm::dot(n, hv), 0.f), 8.f) * 0.12f;
                outC                        = base * attenuation + glm::vec3(spec);
                alpha                       = std::clamp(thickness_[idx] * 0.6f, 0.f, 1.f);
            } else {
                const float lit =
                    lighting_
                        ? std::clamp(ambientMultiplier_ + (1.f - std::min(ambientMultiplier_, 1.f)) * diff, 0.f, 6.f)
                        : 1.f;
                const glm::vec3 base     = baseColor_ * lit;
                const float     fresnel  = 0.04f + 0.96f * std::pow(1.f - std::max(glm::dot(n, V), 0.f), 5.f);
                const glm::vec3 hv       = glm::normalize(L + V);
                const float     exponent = 4.f + 124.f * smoothness_;
                const float     spec =
                    lighting_ ? std::pow(std::max(glm::dot(n, hv), 0.f), exponent) * smoothness_ * .56f : 0.f;
                const glm::vec3 reflected          = glm::mix(reflectionColor_, baseColor_, metalness_);
                const float     reflectionStrength = reflectionEnabled_ ? reflection_ : 0.f;
                outC                               = base + reflected * fresnel * reflectionStrength + glm::vec3(spec);
                alpha                              = std::clamp(thickness_[idx] * opacity_, 0.f, 1.f);
            }
            color_[idx * 4u + 0] = uint8_t(std::clamp(outC.x, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 1] = uint8_t(std::clamp(outC.y, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 2] = uint8_t(std::clamp(outC.z, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 3] = uint8_t(std::clamp(alpha, 0.f, 1.f) * 255.f);
        }
    }
    // Expose the same smoothed depth used for normals/shading, including odd pass counts.
    if (params_.smoothIterations % 2 != 0) depth_.swap(depthScratch_);
}

void FluidSurfaceRenderer::writePpm(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << params_.width << " " << params_.height << "\n255\n";
    for (size_t i = 0; i < color_.size(); i += 4) out << char(color_[i]) << char(color_[i + 1]) << char(color_[i + 2]);
}

}  // namespace eve::fluids
