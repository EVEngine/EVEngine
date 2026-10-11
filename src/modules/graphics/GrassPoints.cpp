#include <climits>
#include <cmath>
#include <limits>
#include "graphics/Graphics.h"
#include "graphics/Grass.h"
#include "image/ImageData.h"
namespace eve::graphics {
Result<int> GrassField::bakePoints(const std::vector<grass::Point>& points, float width, float height) {
    return bakePointData(points, width, height, nullptr);
}
Result<int> GrassField::bakeTexturedPoints(const std::vector<grass::Point>& points, float width, float height,
                                           const image::ImageData& image) {
    return bakePointData(points, width, height, &image);
}
Result<int> GrassField::bakeFoliagePoints(const std::vector<grass::Point>& points, float width, float height,
                                          const image::ImageData& albedo, const image::ImageData& normal,
                                          const image::ImageData& mask, const grass::GrassFoliageSettings& settings) {
    return bakePointData(points, width, height, &albedo, &normal, &mask, &settings);
}
Result<int> GrassField::setTerrainDetailOverwrite(const grass::TerrainDetailOverwriteSettings& settings) {
    if (!foliageProfile_ || !shader_ || !std::isfinite(settings.pcgDetailDistance) ||
        !std::isfinite(settings.pcgFadeoutDistance) || settings.unityDetailDistance < 0 ||
        settings.unityDetailDistance > 8388607 ||
        !std::isfinite(settings.unityDetailDensity) || settings.unityDetailDensity < 0 ||
        settings.unityDetailDensity > 1)
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "grass.detailOverwrite: uploaded foliage and valid settings required"));
    grass::GrassFoliageSettings profile;
    profile.renderDistance = shader_->pushConstantData()[13];
    profile.fadeRange = shader_->pushConstantData()[14];
    profile.hardRenderDistance = detailHardDistance_;
    profile.density = detailDensity_;
    auto applied = grass::applyTerrainDetailOverwrite(profile, settings);
    if (!applied.ok()) return applied;
    baseDetailRenderDistance_=profile.renderDistance;baseDetailFadeRange_=profile.fadeRange;
    baseDetailHardDistance_=float(settings.unityDetailDistance);baseDetailDensity_=settings.unityDetailDensity;
    applyPhotoModeShaderState();
    return applied;
}
Result<int> GrassField::bakePointData(const std::vector<grass::Point>& points, float width, float height,
                                      const image::ImageData* image, const image::ImageData* normal,
                                      const image::ImageData* mask, const grass::GrassFoliageSettings* foliage) {
    auto invalid = []() {
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "grass.points: finite roots, scales above 0.001, 24-bit IDs and positive dimensions required"));
    };
    if (!gfx_ || !std::isfinite(width) || width <= 0 || !std::isfinite(height) || height <= 0 ||
        points.size() > size_t(INT_MAX / 6))
        return invalid();
    for (const auto& point : points) {
        const bool legacyTint = point.tint == glm::vec3(-1.f);
        const bool validTint  = std::isfinite(point.tint.x) && std::isfinite(point.tint.y) &&
                               std::isfinite(point.tint.z) && point.tint.x >= 0 && point.tint.x <= 1 &&
                               point.tint.y >= 0 && point.tint.y <= 1 && point.tint.z >= 0 && point.tint.z <= 1;
        if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y) || !std::isfinite(point.position.z) ||
            (!legacyTint && !validTint) || !std::isfinite(point.scale) || point.scale <= 0.001F ||
            point.id > 0xffffffu || !std::isfinite(point.widthScale) || point.widthScale < 0 ||
            (point.widthScale > 0 && point.widthScale <= 0.001F) ||
            double(point.widthScale > 0 ? point.widthScale : point.scale) * width > std::numeric_limits<float>::max() ||
            double(point.scale) * height > std::numeric_limits<float>::max())
            return invalid();
    }
    const auto validImage = [](const image::ImageData* value) {
        return value && value->getWidth() > 0 && value->getHeight() > 0 && value->getFormat() == "RGBA8" &&
               value->getData() && size_t(value->getWidth()) <= std::numeric_limits<size_t>::max() / 4 /
                                                        size_t(value->getHeight()) &&
               size_t(value->getWidth()) * size_t(value->getHeight()) * 4 <= value->getSize();
    };
    if (image && !validImage(image))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "grass.points: complete RGBA8 image required"));
    if (foliage) {
        const auto finite01 = [](float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; };
        if (!validImage(normal) || !validImage(mask) || normal->getWidth() != image->getWidth() ||
            normal->getHeight() != image->getHeight() || mask->getWidth() != image->getWidth() ||
            mask->getHeight() != image->getHeight() || !finite01(foliage->baseR) || !finite01(foliage->baseG) ||
            !finite01(foliage->baseB) || !finite01(foliage->snowR) || !finite01(foliage->snowG) ||
            !finite01(foliage->snowB) || !finite01(foliage->alphaCutoff) || !finite01(foliage->snowProgress) ||
            !std::isfinite(foliage->normalStrength) || foliage->normalStrength < 0.01f ||
            !std::isfinite(foliage->renderDistance) || foliage->renderDistance < 0.f ||
            !std::isfinite(foliage->fadeRange) || foliage->fadeRange < 0.f ||
            !std::isfinite(foliage->hardRenderDistance) || foliage->hardRenderDistance < 0.f ||
            foliage->hardRenderDistance > 8388607.f ||
            foliage->hardRenderDistance != std::floor(foliage->hardRenderDistance) ||
            !finite01(foliage->density) ||
            !std::isfinite(foliage->snowMinimumHeight) || !std::isfinite(foliage->snowFadeDistance) ||
            foliage->snowFadeDistance <= 0.f)
            return Result<int>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "grass.foliage: matching RGBA8 maps and finite surface settings required"));
    }
    if (points.empty()) {
        denseMesh_   = nullptr;
        sparseMesh_  = nullptr;
        normal_      = nullptr;
        mask_        = nullptr;
        foliageProfile_ = false;
        denseCount_  = 0;
        sparseCount_ = 0;
        return Result<int>::success(0);
    }
    const auto             data            = grass::buildBillboards(points, width, height, false);
    auto*                  candidateShader = grass::createShader(gfx_);
    auto*                  candidateAtlas  = image ? gfx_->newTexture(image->getWidth(), image->getHeight(),
                                                                      static_cast<const uint8_t*>(image->getData()), false, false)
                                                   : grass::createSwayAtlas(gfx_, 64, 64, 4);
    auto* candidateNormal = foliage ? gfx_->newTexture(normal->getWidth(), normal->getHeight(),
                                                        static_cast<const uint8_t*>(normal->getData()), false, false)
                                    : nullptr;
    auto* candidateMask = foliage ? gfx_->newTexture(mask->getWidth(), mask->getHeight(),
                                                      static_cast<const uint8_t*>(mask->getData()), false, false)
                                  : nullptr;
    grass::PackedAtlasInfo layout;
    layout.frames    = image ? 1 : 4;
    layout.atlasCols = layout.frames;
    layout.atlasRows = 1;
    if (foliage) grass::bindFoliage(candidateShader, *foliage);
    else {
        grass::bindDefaults(candidateShader);
        grass::bindAtlasLayout(candidateShader, layout);
    }
    if (image) {
        candidateShader->sendVec3("lightGreen", 1.0F, 1.0F, 1.0F);
        candidateShader->sendVec3("darkGreen", 0.5F, 0.5F, 0.5F);
    }
    candidateShader->sendFloat("grassWidth", width);
    candidateShader->sendFloat("grassHeight", height);
    candidateShader->sendFloat("frameDuration", frameDuration_);
    candidateShader->sendFloat("time", time_);
    auto* candidateMesh =
        gfx_->newMeshFromArrays(data.posXYZ.data(), data.nrmXYZ.data(), data.uvST.data(), int(data.posXYZ.size() / 3),
                                data.indices.data(), int(data.indices.size()));
    if (!candidateShader || !candidateAtlas || !candidateMesh || (foliage && (!candidateNormal || !candidateMask)))
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "grass.points: GPU resource creation returned empty"));
    shader_      = candidateShader;
    atlas_       = candidateAtlas;
    normal_      = candidateNormal;
    mask_        = candidateMask;
    denseMesh_   = candidateMesh;
    sparseMesh_  = nullptr;
    denseCount_  = int(points.size());
    sparseCount_ = 0;
    foliageProfile_ = foliage != nullptr;
    if (foliage) {
        baseDetailRenderDistance_=foliage->renderDistance;baseDetailFadeRange_=foliage->fadeRange;
        baseDetailHardDistance_=foliage->hardRenderDistance;baseDetailDensity_=foliage->density;
        applyPhotoModeShaderState();
    }
    return Result<int>::success(denseCount_);
}
}  // namespace eve::graphics
