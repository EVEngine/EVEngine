#include "fluids/FluidSurfaceRenderer.h"

#include "fluids/FluidSsfKernels.h"
#include "fluids/Fluids.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace eve::fluids {
namespace {

constexpr int kSsfPushVP0     = 0;
constexpr int kSsfPushCount   = 16;
constexpr int kSsfPushNear    = 19;
constexpr int kSsfPushFar     = 20;
constexpr int kSsfPushTanHalf = 21;
constexpr int kSsfPushAspect  = 22;
constexpr int kSsfPushW       = 23;
constexpr int kSsfPushH       = 24;
constexpr int kSsfPushRadius  = 25;
constexpr int kSsfPushMode    = 26;
constexpr int kSsfPushThick   = 27;
constexpr int kSsfPushFalloff = 28;

constexpr uint32_t kEmptyKey = 0xFFFFFFFFu;
constexpr float    kKeyScale = 16777215.f;

int groupsFor(int count, int localSize = 64) { return (count + localSize - 1) / localSize; }

}  // namespace

FluidSurfaceRenderer::FluidSurfaceRenderer(const FluidSurfaceParams& params, bool preferGpu)
    : params_(params), preferGpu_(preferGpu) {
    params_.aspect   = float(params_.width) / float(params_.height);
    const int pixels = params_.width * params_.height;
    depth_.assign(size_t(pixels), 1e30f);
    thickness_.assign(size_t(pixels), 0.f);
    normals_.assign(size_t(pixels), glm::vec3(0.f));
    color_.assign(size_t(pixels) * 4u, 0);
}

FluidSurfaceRenderer::~FluidSurfaceRenderer() {
    delete seq_;
    delete shShade_;
    delete shNormal_;
    delete shSmooth_;
    delete shSplat_;
    delete shClear_;
    delete stColor_;
    delete stNormal_;
    delete stThick_;
    delete stDepth_;
    delete bufColor_;
    delete bufNormal_;
    delete bufThick_;
    delete bufDepthB_;
    delete bufDepthA_;
    delete bufParts_;
}

void FluidSurfaceRenderer::setCamera(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up,
                                     float fovYDeg) {
    params_.eye     = eye;
    params_.target  = target;
    params_.up      = up;
    params_.fovYDeg = fovYDeg;
    params_.aspect  = float(params_.width) / float(params_.height);
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

void FluidSurfaceRenderer::render(const std::vector<glm::vec3>& positions, float particleRadius) {
    positions_             = positions;
    params_.particleRadius = particleRadius;
    if (preferGpu_ && !gpuOk_) ensureGpu();
    if (gpuOk_) {
        // GPU path implemented below; the CPU path is exercised by tests.
        const int pixels = params_.width * params_.height;
        seq_->begin();
        uploadParticles();
        const int groupsPix = groupsFor(pixels);
        setCommonConstants(shClear_, params_.depthFalloff);
        seq_->recordDispatch(shClear_, groupsPix);
        setCommonConstants(shSplat_, params_.depthFalloff);
        seq_->recordDispatch(shSplat_, groupsFor(int(positions_.size())));
        bool inA = true;
        for (int it = 0; it < params_.smoothIterations; ++it) {
            shSmooth_->bindBuffer(1, inA ? bufDepthA_ : bufDepthB_);
            shSmooth_->bindBuffer(2, inA ? bufDepthB_ : bufDepthA_);
            setCommonConstants(shSmooth_, params_.depthFalloff);
            seq_->recordDispatch(shSmooth_, groupsPix);
            inA = !inA;
        }
        gpgpu::GpuBuffer* finalDepth = inA ? bufDepthA_ : bufDepthB_;
        shNormal_->bindBuffer(1, finalDepth);
        setCommonConstants(shNormal_, params_.depthFalloff);
        seq_->recordDispatch(shNormal_, groupsPix);
        shShade_->bindBuffer(1, finalDepth);
        setCommonConstants(shShade_, params_.depthFalloff);
        seq_->recordDispatch(shShade_, groupsPix);
        seq_->recordDownload(finalDepth, stDepth_, uint64_t(pixels) * sizeof(uint32_t));
        seq_->recordDownload(bufThick_, stThick_, uint64_t(pixels) * sizeof(uint32_t));
        seq_->recordDownload(bufNormal_, stNormal_, uint64_t(pixels) * 4u * sizeof(float));
        seq_->recordDownload(bufColor_, stColor_, uint64_t(pixels) * 4u * sizeof(float));
        seq_->submit();

        std::vector<uint32_t>  key(size_t(pixels), 0u);
        std::vector<uint32_t>  thickU(size_t(pixels), 0u);
        std::vector<glm::vec4> normal4(size_t(pixels), glm::vec4(0.f));
        std::vector<glm::vec4> color4(size_t(pixels), glm::vec4(0.f));
        stDepth_->downloadBytes(key.data(), uint64_t(key.size()) * sizeof(uint32_t));
        stThick_->downloadBytes(thickU.data(), uint64_t(thickU.size()) * sizeof(uint32_t));
        stNormal_->downloadBytes(normal4.data(), uint64_t(normal4.size()) * sizeof(glm::vec4));
        stColor_->downloadBytes(color4.data(), uint64_t(color4.size()) * sizeof(glm::vec4));
        const float nearZ = params_.nearZ;
        const float farZ  = params_.farZ;
        for (int i = 0; i < pixels; ++i) {
            depth_[size_t(i)] =
                key[size_t(i)] == kEmptyKey ? 1e30f : nearZ + (float(key[size_t(i)]) / kKeyScale) * (farZ - nearZ);
            thickness_[size_t(i)]      = float(thickU[size_t(i)]) / 256.f;
            normals_[size_t(i)]        = glm::vec3(normal4[size_t(i)]);
            color_[size_t(i) * 4u + 0] = uint8_t(std::clamp(color4[size_t(i)].x, 0.f, 1.f) * 255.f);
            color_[size_t(i) * 4u + 1] = uint8_t(std::clamp(color4[size_t(i)].y, 0.f, 1.f) * 255.f);
            color_[size_t(i) * 4u + 2] = uint8_t(std::clamp(color4[size_t(i)].z, 0.f, 1.f) * 255.f);
            color_[size_t(i) * 4u + 3] = uint8_t(std::clamp(color4[size_t(i)].w, 0.f, 1.f) * 255.f);
        }
        return;
    }
    renderCpu();
}

bool FluidSurfaceRenderer::ensureGpu() {
    if (gpuOk_) return true;
    gpgpu_ = eve::gpgpu::Gpgpu::create();
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
        bufColor_  = gpgpu_->newBuffer(pixels * 4 * int(sizeof(float)), "storage");
        stDepth_   = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "staging");
        stThick_   = gpgpu_->newBuffer(pixels * int(sizeof(uint32_t)), "staging");
        stNormal_  = gpgpu_->newBuffer(pixels * 4 * int(sizeof(float)), "staging");
        stColor_   = gpgpu_->newBuffer(pixels * 4 * int(sizeof(float)), "staging");
        seq_       = gpgpu_->newSequence();
    } catch (...) {
        return false;
    }
    if (!seq_ || !seq_->isAvailable()) return false;
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
    shNormal_->bindBuffer(4, bufNormal_);
    shShade_->bindBuffer(3, bufThick_);
    shShade_->bindBuffer(4, bufNormal_);
    shShade_->bindBuffer(5, bufColor_);
    gpuOk_ = true;
    return true;
}

void FluidSurfaceRenderer::uploadParticles() {
    if (!gpuOk_) return;
    const size_t       count = std::min(positions_.size(), size_t(65536));
    std::vector<float> posF(count * 4u, 0.f);
    for (size_t i = 0; i < count; ++i) {
        posF[i * 4u + 0] = positions_[i].x;
        posF[i * 4u + 1] = positions_[i].y;
        posF[i * 4u + 2] = positions_[i].z;
        posF[i * 4u + 3] = params_.particleRadius;
    }
    seq_->recordUpload(bufParts_, posF.data(), uint64_t(posF.size()) * sizeof(float));
}

void FluidSurfaceRenderer::setCommonConstants(gpgpu::ComputeShader* shader, float falloff) {
    if (!shader) return;
    const glm::mat4 view = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const glm::mat4 proj =
        glm::perspectiveRH(glm::radians(params_.fovYDeg), params_.aspect, params_.nearZ, params_.farZ);
    const glm::mat4 vp    = proj * view;
    const float*    vpPtr = &vp[0][0];
    for (int i = 0; i < 16; ++i) shader->setFloat(kSsfPushVP0 + i, vpPtr[i]);
    shader->setFloat(kSsfPushCount, float(std::min(positions_.size(), size_t(65536))));
    shader->setFloat(kSsfPushNear, params_.nearZ);
    shader->setFloat(kSsfPushFar, params_.farZ);
    shader->setFloat(kSsfPushTanHalf, std::tan(glm::radians(params_.fovYDeg) * 0.5f));
    shader->setFloat(kSsfPushAspect, params_.aspect);
    shader->setFloat(kSsfPushW, float(params_.width));
    shader->setFloat(kSsfPushH, float(params_.height));
    shader->setFloat(kSsfPushRadius, params_.particleRadius);
    shader->setFloat(kSsfPushMode, float(params_.mode));
    shader->setFloat(kSsfPushThick, params_.thicknessScale);
    shader->setFloat(kSsfPushFalloff, falloff);
}

void FluidSurfaceRenderer::renderCpu() {
    const int       W       = params_.width;
    const int       H       = params_.height;
    const int       pixels  = W * H;
    const float     nearZ   = params_.nearZ;
    const float     farZ    = params_.farZ;
    const float     tanHalf = std::tan(glm::radians(params_.fovYDeg) * 0.5f);
    const glm::mat4 view    = glm::lookAtRH(params_.eye, params_.target, params_.up);
    const glm::mat4 proj    = glm::perspectiveRH(glm::radians(params_.fovYDeg), params_.aspect, nearZ, farZ);
    const glm::mat4 vp      = proj * view;

    std::vector<float> depth(size_t(pixels), 1e30f);
    std::vector<float> thick(size_t(pixels), 0.f);

    // 1. Splat.
    for (const glm::vec3& p : positions_) {
        const glm::vec4 clip = vp * glm::vec4(p, 1.f);
        if (clip.w <= 1e-4f) continue;
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (glm::any(glm::lessThan(ndc, glm::vec3(-1.f))) || glm::any(glm::greaterThan(ndc, glm::vec3(1.f)))) continue;
        const float sx       = (ndc.x * 0.5f + 0.5f) * float(W);
        const float sy       = (0.5f - ndc.y * 0.5f) * float(H);
        const float depthVal = clip.w;  // -viewZ
        const float radiusPx = (params_.particleRadius * (float(H) * 0.5f) / tanHalf) / std::max(depthVal, 1e-4f);
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
                const size_t idx = size_t(yy) * size_t(W) + size_t(xx);
                if (depthVal < depth[idx]) depth[idx] = depthVal;
                const float w = 1.f - glm::smoothstep(0.4f, 1.f, std::sqrt(q));
                thick[idx] += w * 2.f * params_.particleRadius * params_.thicknessScale;
            }
        }
    }

    // 2. Bilateral smooth (ping-pong).
    std::vector<float> depthB = depth;
    for (int it = 0; it < params_.smoothIterations; ++it) {
        const std::vector<float>& src = (it % 2 == 0) ? depth : depthB;
        std::vector<float>&       dst = (it % 2 == 0) ? depthB : depth;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t idx = size_t(y) * size_t(W) + size_t(x);
                if (src[idx] >= 1e29f) {
                    dst[idx] = src[idx];
                    continue;
                }
                float sum  = src[idx];
                float wsum = 1.f;
                for (int oy = -2; oy <= 2; ++oy) {
                    for (int ox = -2; ox <= 2; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        const int xx = x + ox;
                        const int yy = y + oy;
                        if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                        const size_t nidx = size_t(yy) * size_t(W) + size_t(xx);
                        if (src[nidx] >= 1e29f) continue;
                        const float spatial = std::exp(-float(ox * ox + oy * oy) * 0.5f);
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
    const std::vector<float>& smooth = (params_.smoothIterations % 2 == 0) ? depth : depthB;

    // 3. Normals from depth gradients (view space).
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t idx = size_t(y) * size_t(W) + size_t(x);
            if (smooth[idx] >= 1e29f) {
                normals_[idx] = glm::vec3(0.f);
                continue;
            }
            const auto viewPos = [&](int px, int py, float d) {
                const float u = (float(px) + 0.5f) / float(W);
                const float v = (float(py) + 0.5f) / float(H);
                return glm::vec3((u * 2.f - 1.f) * params_.aspect * tanHalf * d, (v * 2.f - 1.f) * tanHalf * d, -d);
            };
            const auto sd = [&](int ox, int oy) {
                const int   xx = std::clamp(x + ox, 0, W - 1);
                const int   yy = std::clamp(y + oy, 0, H - 1);
                const float d  = smooth[size_t(yy) * size_t(W) + size_t(xx)];
                return d < 1e29f ? d : smooth[idx];
            };
            const glm::vec3 dpx = (viewPos(x + 1, y, sd(1, 0)) - viewPos(x - 1, y, sd(-1, 0))) * 0.5f;
            const glm::vec3 dpy = (viewPos(x, y + 1, sd(0, 1)) - viewPos(x, y - 1, sd(0, -1))) * 0.5f;
            glm::vec3       n   = glm::normalize(glm::cross(dpx, dpy));
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
            if (smooth[idx] >= 1e29f) {
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
                const float     attenuation = std::exp(-thick[idx] * 1.8f);
                const glm::vec3 hv          = glm::normalize(L + V);
                const float     spec        = std::pow(std::max(glm::dot(n, hv), 0.f), 8.f) * 0.12f;
                outC                        = base * attenuation + glm::vec3(spec);
                alpha                       = std::clamp(thick[idx] * 0.6f, 0.f, 1.f);
            } else {
                const glm::vec3 base    = glm::vec3(0.05f, 0.32f, 0.72f) * (0.55f + 0.45f * diff);
                const float     fresnel = 0.04f + 0.96f * std::pow(1.f - std::max(glm::dot(n, V), 0.f), 5.f);
                const glm::vec3 hv      = glm::normalize(L + V);
                const float     spec    = std::pow(std::max(glm::dot(n, hv), 0.f), 64.f) * 0.45f;
                outC                    = base + glm::vec3(0.55f, 0.72f, 1.f) * fresnel * 0.75f + glm::vec3(spec);
                alpha                   = std::clamp(thick[idx] * 0.35f, 0.f, 1.f);
            }
            color_[idx * 4u + 0] = uint8_t(std::clamp(outC.x, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 1] = uint8_t(std::clamp(outC.y, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 2] = uint8_t(std::clamp(outC.z, 0.f, 1.f) * 255.f);
            color_[idx * 4u + 3] = uint8_t(std::clamp(alpha, 0.f, 1.f) * 255.f);
        }
    }
    depth_     = std::move(depth);
    thickness_ = std::move(thick);
}

void FluidSurfaceRenderer::writePpm(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) return;
    out << "P6\n" << params_.width << " " << params_.height << "\n255\n";
    for (size_t i = 0; i < color_.size(); i += 4) out << char(color_[i]) << char(color_[i + 1]) << char(color_[i + 2]);
}

}  // namespace eve::fluids
