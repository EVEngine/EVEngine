#include "graphics/hair/GroomInstance.h"

#include "common/Diagnostic.h"
#include "common/Exception.h"
#include "graphics/Color.h"
#include "graphics/Graphics.h"
#include "graphics/HairShader.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "graphics/hair/RibbonBuilder.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics::hair {

GroomInstance::GroomInstance(Graphics *gfx) : gfx_(gfx) {
    if (!gfx_) throw eve::Exception("GroomInstance: null graphics");
}

GroomInstance::~GroomInstance() = default;

const GroomGroup *GroomInstance::primaryGroup() const { return asset_.groupAt(0); }

size_t GroomInstance::resolveLodIndex(const GroomGroup &group) const {
    if (group.lods.empty()) return 0;
    if (forcedLod_ >= 0) {
        size_t lodIndex = size_t(forcedLod_);
        if (lodIndex >= group.lods.size()) lodIndex = group.lods.size() - 1;
        return lodIndex;
    }
    return selectLodIndex(group.lods, screenSize_);
}

StrandsDatas GroomInstance::decimatedStrands(const StrandsDatas &src, float curveFraction) const {
    StrandsDatas out;
    if (src.curveCount() == 0) return out;
    const float frac = std::clamp(curveFraction, 0.05f, 1.f);
    const size_t keep =
        std::max<size_t>(1, size_t(std::ceil(double(src.curveCount()) * double(frac))));
    const size_t step = std::max<size_t>(1, src.curveCount() / keep);

    std::vector<StrandPoint> points;
    std::vector<StrandCurve> curves;
    points.reserve(src.pointCount());
    curves.reserve(keep);

    for (size_t ci = 0; ci < src.curveCount(); ci += step) {
        if (curves.size() >= keep) break;
        const auto pts = src.curvePoints(ci);
        if (pts.size() < 2) continue;
        StrandCurve curve;
        curve.pointOffset = uint32_t(points.size());
        curve.pointCount = uint32_t(pts.size());
        float len = 0.f;
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            points.push_back(pts[pi]);
            if (pi > 0) len += glm::length(pts[pi].position - pts[pi - 1].position);
        }
        curve.length = len;
        curves.push_back(curve);
    }
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    return out;
}

Result<void> GroomInstance::rebuildClusterGrids() {
    groupCull_.assign(asset_.groupCount(), GroupCullState{});
    for (size_t gi = 0; gi < asset_.groupCount(); ++gi) {
        const GroomGroup *group = asset_.groupAt(gi);
        if (!group) continue;
        auto built = groupCull_[gi].clusters.build(group->strands, clusterCellSize_);
        if (!built.ok()) return Result<void>::failure(built.status());
    }
    return Result<void>::success();
}

Result<void> GroomInstance::bakeFromStrands(StrandsDatas strands, const char *debugName) {
    auto ok = strands.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());

    GroomGroup group;
    group.name = debugName ? debugName : "default";
    group.groupId = 0;
    group.strands = std::move(strands);

    // Cards geometric LOD lives in graphics/HairCards (separate agent/PR).
    GroomLod nearLod;
    nearLod.screenSize = 1.f;
    nearLod.representation = Representation::Strands;
    nearLod.curveFraction = 1.f;
    GroomLod midLod;
    midLod.screenSize = 0.35f;
    midLod.representation = Representation::Strands;
    midLod.curveFraction = 0.3f;
    midLod.thicknessScale = 2.f;
    GroomLod farLod;
    farLod.screenSize = 0.12f;
    farLod.representation = Representation::None;
    farLod.curveFraction = 0.05f;
    group.lods = {nearLod, midLod, farLod};

    GroomAsset asset;
    auto add = asset.addGroup(std::move(group));
    if (!add.ok()) return Result<void>::failure(add.status());
    return setAsset(asset);
}

Result<void> GroomInstance::setAsset(const GroomAsset &asset) {
    auto ok = asset.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());
    asset_ = asset;
    auto grid = rebuildClusterGrids();
    if (!grid.ok()) return Result<void>::failure(grid.status());
    return rebuild();
}

Result<void> GroomInstance::bakeProceduralPlane(float sizeX, float sizeZ,
                                                const ProceduralParams &params) {
    auto strands = generateOnPlane(sizeX, sizeZ, params);
    if (!strands.ok()) return Result<void>::failure(strands.status());
    return bakeFromStrands(std::move(strands).value(), "procedural_plane");
}

Result<void> GroomInstance::bakeProceduralMesh(const float *posXYZ, const float *nrmXYZ,
                                               int vertexCount, const uint32_t *indices,
                                               int indexCount, const ProceduralParams &params) {
    auto strands = generateOnMesh(posXYZ, nrmXYZ, vertexCount, indices, indexCount, params);
    if (!strands.ok()) return Result<void>::failure(strands.status());
    return bakeFromStrands(std::move(strands).value(), "procedural_mesh");
}

void GroomInstance::setForcedLod(int lod) { forcedLod_ = lod; }

int GroomInstance::getForcedLod() const { return forcedLod_; }

void GroomInstance::setScreenSize(float screenSize) {
    screenSize_ = std::clamp(screenSize, 0.f, 1.f);
}

float GroomInstance::getScreenSize() const { return screenSize_; }

void GroomInstance::setWidthScale(float scale) { widthScale_ = scale > 1e-6f ? scale : 1e-6f; }

float GroomInstance::getWidthScale() const { return widthScale_; }

void GroomInstance::setSideHint(float x, float y, float z) { sideHint_ = glm::vec3(x, y, z); }

void GroomInstance::setClusterCullingEnabled(bool enabled) { clusterCulling_ = enabled; }

bool GroomInstance::isClusterCullingEnabled() const { return clusterCulling_; }

void GroomInstance::setMarschnerLobes(float r, float tt, float trt) {
    marschnerR_ = r >= 0.f ? r : 0.f;
    marschnerTT_ = tt >= 0.f ? tt : 0.f;
    marschnerTRT_ = trt >= 0.f ? trt : 0.f;
    applyShadingParams();
}

float GroomInstance::getMarschnerR() const { return marschnerR_; }

float GroomInstance::getMarschnerTT() const { return marschnerTT_; }

float GroomInstance::getMarschnerTRT() const { return marschnerTRT_; }

void GroomInstance::setSelfShadow(float strength, float bias, float rootAo) {
    selfShadowStrength_ = std::clamp(strength, 0.f, 1.f);
    selfShadowBias_ = std::clamp(bias, 0.f, 1.f);
    rootAoStrength_ = std::clamp(rootAo, 0.f, 1.f);
    applyShadingParams();
}

float GroomInstance::getSelfShadowStrength() const { return selfShadowStrength_; }

float GroomInstance::getSelfShadowBias() const { return selfShadowBias_; }

float GroomInstance::getRootAoStrength() const { return rootAoStrength_; }

Mesh *GroomInstance::getMesh() const { return mesh_; }

Shader *GroomInstance::getShader() const { return shader_; }

Texture *GroomInstance::getTexture() const { return texture_; }

size_t GroomInstance::getGroupCount() const { return asset_.groupCount(); }

int GroomInstance::getActiveLodIndex() const { return activeLodIndex_; }

int GroomInstance::getActiveRepresentation() const { return int(activeRepresentation_); }

size_t GroomInstance::getClusterCount() const {
    size_t total = 0;
    for (const GroupCullState &state : groupCull_) total += state.clusters.clusterCount();
    return total;
}

int GroomInstance::getVisibleCurveCount() const {
    if (clusterCulling_) {
        int total = 0;
        bool anyMask = false;
        for (const GroupCullState &state : groupCull_) {
            if (!state.hasVisibility) continue;
            anyMask = true;
            total += int(state.visibleCurves.size());
        }
        if (anyMask) return total;
    }
    int total = 0;
    for (size_t gi = 0; gi < asset_.groupCount(); ++gi) {
        const GroomGroup *g = asset_.groupAt(gi);
        if (g) total += int(g->strands.curveCount());
    }
    return total;
}

void GroomInstance::applyShadingParams() {
    if (!shader_) return;
    shader_->sendFloat("marschnerR", marschnerR_);
    shader_->sendFloat("marschnerTT", marschnerTT_);
    shader_->sendFloat("marschnerTRT", marschnerTRT_);
    shader_->sendFloat("selfShadowStrength", selfShadowStrength_);
    shader_->sendFloat("selfShadowBias", selfShadowBias_);
    shader_->sendFloat("rootAoStrength", rootAoStrength_);
}

Result<void> GroomInstance::ensureDrawResources() {
    if (!shader_) {
        shader_ = createShader(gfx_);
        if (!shader_) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "GroomInstance: hair shader failed", "hair.groom.shader"));
        }
    }
    applyShadingParams();
    if (!texture_) {
        const uint8_t white[4] = {210, 170, 120, 255};
        texture_ = gfx_->newTexture(1, 1, white);
        if (!texture_) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "GroomInstance: texture failed", "hair.groom.texture"));
        }
    }
    return Result<void>::success();
}

Result<void> GroomInstance::updateVisibility(const float *viewProj16) {
    if (!viewProj16) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomInstance::updateVisibility: null viewProj",
            "hair.groom.viewProj"));
    }
    if (groupCull_.size() != asset_.groupCount()) {
        auto grid = rebuildClusterGrids();
        if (!grid.ok()) return Result<void>::failure(grid.status());
    }
    for (GroupCullState &state : groupCull_) {
        auto visibleClusters = state.clusters.cullClusters(viewProj16);
        if (!visibleClusters.ok()) return Result<void>::failure(visibleClusters.status());
        auto curves = state.clusters.collectCurveIndices(visibleClusters.value());
        if (!curves.ok()) return Result<void>::failure(curves.status());
        state.visibleCurves = std::move(curves).value();
        state.hasVisibility = true;
    }
    return rebuild();
}

Result<void> GroomInstance::rebuild() {
    if (asset_.groupCount() == 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomInstance::rebuild: no groups", "hair.groom"));
    }
    if (groupCull_.size() != asset_.groupCount()) {
        auto grid = rebuildClusterGrids();
        if (!grid.ok()) return Result<void>::failure(grid.status());
    }

    // Report LOD from the primary group (representative for script getters).
    const GroomGroup *primary = primaryGroup();
    if (primary && !primary->lods.empty()) {
        activeLodIndex_ = int(resolveLodIndex(*primary));
        activeRepresentation_ = primary->lods[size_t(activeLodIndex_)].representation;
    } else {
        activeLodIndex_ = 0;
        activeRepresentation_ = Representation::None;
    }

    RibbonMesh combined;
    bool anyStrands = false;

    for (size_t gi = 0; gi < asset_.groupCount(); ++gi) {
        const GroomGroup *group = asset_.groupAt(gi);
        if (!group || group->lods.empty()) continue;

        const size_t lodIndex = resolveLodIndex(*group);
        const GroomLod &lod = group->lods[lodIndex];
        if (lod.representation == Representation::None) continue;
        if (lod.representation == Representation::Cards ||
            lod.representation == Representation::Meshes) {
            mesh_ = nullptr;
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Unsupported,
                "GroomInstance::rebuild: Cards/Meshes LOD owned by HairCards / later phase",
                "hair.groom.representation"));
        }

        StrandsDatas source = group->strands;
        if (clusterCulling_ && gi < groupCull_.size() && groupCull_[gi].hasVisibility) {
            if (groupCull_[gi].visibleCurves.empty()) continue;
            auto filtered = filterStrandsByCurves(group->strands, groupCull_[gi].visibleCurves);
            if (!filtered.ok()) return Result<void>::failure(filtered.status());
            source = std::move(filtered).value();
        }

        StrandsDatas strands = decimatedStrands(source, lod.curveFraction);
        auto ok = strands.validate();
        if (!ok.ok()) return Result<void>::failure(ok.status());

        RibbonParams ribbon;
        ribbon.sideHint = sideHint_;
        ribbon.widthScale = widthScale_ * lod.thicknessScale;
        auto meshData = buildRibbons(strands, ribbon);
        if (!meshData.ok()) return Result<void>::failure(meshData.status());
        appendRibbonMesh(combined, meshData.value());
        anyStrands = true;
    }

    if (!anyStrands || combined.indices.empty()) {
        mesh_ = nullptr;
        return Result<void>::success();
    }

    mesh_ = gfx_->newMeshFromArrays(combined.posXYZ.data(), combined.nrmXYZ.data(),
                                    combined.uvST.data(), combined.vertexCount(),
                                    combined.indices.data(), combined.indexCount());
    if (!mesh_) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "GroomInstance::rebuild: mesh upload failed", "hair.groom.mesh"));
    }

    return ensureDrawResources();
}

void GroomInstance::draw() { draw(lastModel_); }

void GroomInstance::draw(const glm::mat4 &model) {
    lastModel_ = model;
    if (!gfx_ || !mesh_ || !shader_ || !texture_) return;
    applyShadingParams();
    const Color tint(1.f, 1.f, 1.f, 1.f);
    gfx_->drawMeshShader(mesh_, model, texture_, tint, shader_);
}

int GroomInstance::getCurveCount() const {
    int total = 0;
    for (size_t gi = 0; gi < asset_.groupCount(); ++gi) {
        const GroomGroup *g = asset_.groupAt(gi);
        if (g) total += int(g->strands.curveCount());
    }
    return total;
}

int GroomInstance::getPointCount() const {
    int total = 0;
    for (size_t gi = 0; gi < asset_.groupCount(); ++gi) {
        const GroomGroup *g = asset_.groupAt(gi);
        if (g) total += int(g->strands.pointCount());
    }
    return total;
}

}  // namespace eve::graphics::hair
