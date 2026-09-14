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

Result<void> GroomInstance::bakeFromStrands(StrandsDatas strands, const char *debugName) {
    auto ok = strands.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());

    GroomGroup group;
    group.name = debugName ? debugName : "default";
    group.groupId = 0;
    group.strands = std::move(strands);

    GroomLod nearLod;
    nearLod.screenSize = 1.f;
    nearLod.representation = Representation::Strands;
    nearLod.curveFraction = 1.f;
    GroomLod farLod;
    farLod.screenSize = 0.25f;
    farLod.representation = Representation::Strands;
    farLod.curveFraction = 0.35f;
    farLod.thicknessScale = 1.6f;
    group.lods = {nearLod, farLod};

    GroomAsset asset;
    auto add = asset.addGroup(std::move(group));
    if (!add.ok()) return Result<void>::failure(add.status());
    return setAsset(asset);
}

Result<void> GroomInstance::setAsset(const GroomAsset &asset) {
    auto ok = asset.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());
    asset_ = asset;
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

void GroomInstance::setScreenSize(float screenSize) {
    screenSize_ = std::clamp(screenSize, 0.f, 1.f);
}

void GroomInstance::setWidthScale(float scale) { widthScale_ = scale > 1e-6f ? scale : 1e-6f; }

void GroomInstance::setSideHint(float x, float y, float z) { sideHint_ = glm::vec3(x, y, z); }

Result<void> GroomInstance::rebuild() {
    const GroomGroup *group = primaryGroup();
    if (!group) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomInstance::rebuild: no groups", "hair.groom"));
    }

    size_t lodIndex = 0;
    if (forcedLod_ >= 0) {
        lodIndex = size_t(forcedLod_);
        if (lodIndex >= group->lods.size()) lodIndex = group->lods.size() - 1;
    } else {
        lodIndex = selectLodIndex(group->lods, screenSize_);
    }
    const GroomLod &lod = group->lods[lodIndex];
    if (lod.representation == Representation::None) {
        mesh_ = nullptr;
        return Result<void>::success();
    }

    StrandsDatas strands = decimatedStrands(group->strands, lod.curveFraction);
    auto ok = strands.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());

    RibbonParams ribbon;
    ribbon.sideHint = sideHint_;
    ribbon.widthScale = widthScale_ * lod.thicknessScale;
    auto meshData = buildRibbons(strands, ribbon);
    if (!meshData.ok()) return Result<void>::failure(meshData.status());
    const RibbonMesh &rm = meshData.value();

    mesh_ = gfx_->newMeshFromArrays(rm.posXYZ.data(), rm.nrmXYZ.data(), rm.uvST.data(),
                                    rm.vertexCount(), rm.indices.data(), rm.indexCount());
    if (!mesh_) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "GroomInstance::rebuild: mesh upload failed", "hair.groom.mesh"));
    }

    if (!shader_) {
        shader_ = createShader(gfx_);
        if (!shader_) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "GroomInstance::rebuild: hair shader failed",
                "hair.groom.shader"));
        }
    }

    if (!texture_) {
        const uint8_t white[4] = {210, 170, 120, 255};
        texture_ = gfx_->newTexture(1, 1, white);
        if (!texture_) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "GroomInstance::rebuild: texture failed",
                "hair.groom.texture"));
        }
    }
    return Result<void>::success();
}

void GroomInstance::draw() { draw(lastModel_); }

void GroomInstance::draw(const glm::mat4 &model) {
    lastModel_ = model;
    if (!gfx_ || !mesh_ || !shader_ || !texture_) return;
    const Color tint(1.f, 1.f, 1.f, 1.f);
    gfx_->drawMeshShader(mesh_, model, texture_, tint, shader_);
}

int GroomInstance::getCurveCount() const {
    const GroomGroup *g = primaryGroup();
    return g ? int(g->strands.curveCount()) : 0;
}

int GroomInstance::getPointCount() const {
    const GroomGroup *g = primaryGroup();
    return g ? int(g->strands.pointCount()) : 0;
}

}  // namespace eve::graphics::hair
