#include "gpuagents/LifeFieldMaterialBinding.h"

#include "common/Diagnostic.h"
#include "graphics/Graphics.h"

#include <algorithm>
#include <cmath>

namespace eve::gpuagents {
namespace {

std::uint8_t encode01(float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); }

float height01(float h, float minH, float maxH) {
    if (maxH - minH < 1e-5f) return 0.5f;
    return (h - minH) / (maxH - minH);
}

}  // namespace

Result<void> LifeFieldMaterialBinding::syncFrom(const SurfaceField& field) {
    if (field.resolution < 2 || field.lifeField.size() != static_cast<size_t>(field.resolution) * field.resolution) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "surface field is not initialized for life binding", "field", {},
                                                       "gpuagents"));
    }
    const int res        = field.resolution;
    uniforms_.origin     = field.origin;
    uniforms_.worldSize  = field.worldSize;
    uniforms_.resolution = res;

    lifePixels_.assign(static_cast<size_t>(res) * res * 4u, 0);
    surfacePixels_.assign(static_cast<size_t>(res) * res * 4u, 0);

    float minH = field.height.empty() ? 0.f : field.height[0];
    float maxH = minH;
    for (float h : field.height) {
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }

    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            const size_t i     = static_cast<size_t>(x + res * z);
            const auto&  life  = field.lifeField[i];
            const size_t p     = i * 4u;
            lifePixels_[p + 0] = encode01(life.r);
            lifePixels_[p + 1] = encode01(life.g);
            lifePixels_[p + 2] = encode01(life.b);
            lifePixels_[p + 3] = encode01(life.a);

            const float     h     = field.height.empty() ? 0.f : field.height[i];
            const glm::vec3 n     = field.normals.empty() ? glm::vec3(0.f, 1.f, 0.f) : field.normals[i];
            surfacePixels_[p + 0] = encode01(height01(h, minH, maxH));
            surfacePixels_[p + 1] = encode01(n.x * 0.5f + 0.5f);
            surfacePixels_[p + 2] = encode01(n.y * 0.5f + 0.5f);
            surfacePixels_[p + 3] = encode01(n.z * 0.5f + 0.5f);
        }
    }
    return Result<void>::success();
}

Result<graphics::Texture*> LifeFieldMaterialBinding::uploadLifeField(graphics::Graphics* gfx) {
    if (!gfx) {
        return Result<graphics::Texture*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "graphics is null", "gfx", {}, "gpuagents"));
    }
    if (lifePixels_.empty()) {
        return Result<graphics::Texture*>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                     "life-field pixels empty; call syncFrom first",
                                                                     "syncFrom", {}, "gpuagents"));
    }
    auto* tex = gfx->newTexture(uniforms_.resolution, uniforms_.resolution, lifePixels_.data(), false, false);
    if (!tex) {
        return Result<graphics::Texture*>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "failed to create life-field texture", "texture", {}, "gpuagents"));
    }
    return Result<graphics::Texture*>::success(tex);
}

Result<graphics::Texture*> LifeFieldMaterialBinding::uploadSurfaceData(graphics::Graphics* gfx) {
    if (!gfx) {
        return Result<graphics::Texture*>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "graphics is null", "gfx", {}, "gpuagents"));
    }
    if (surfacePixels_.empty()) {
        return Result<graphics::Texture*>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                                     "surface-data pixels empty; call syncFrom first",
                                                                     "syncFrom", {}, "gpuagents"));
    }
    auto* tex = gfx->newTexture(uniforms_.resolution, uniforms_.resolution, surfacePixels_.data(), false, false);
    if (!tex) {
        return Result<graphics::Texture*>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "failed to create surface-data texture", "texture", {}, "gpuagents"));
    }
    return Result<graphics::Texture*>::success(tex);
}

Result<void> LifeFieldMaterialBinding::updateTexture(graphics::Graphics* gfx, graphics::Texture* texture,
                                                     bool lifeField) const {
    if (!gfx || !texture) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "graphics or texture is null",
                                                       "texture", {}, "gpuagents"));
    }
    const auto& pixels = lifeField ? lifePixels_ : surfacePixels_;
    if (pixels.empty()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "pixel buffer empty; call syncFrom first", "syncFrom", {},
                                                       "gpuagents"));
    }
    if (!gfx->updateTexture(texture, uniforms_.resolution, uniforms_.resolution, pixels.data())) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "updateTexture failed", "texture", {}, "gpuagents"));
    }
    return Result<void>::success();
}

}  // namespace eve::gpuagents
