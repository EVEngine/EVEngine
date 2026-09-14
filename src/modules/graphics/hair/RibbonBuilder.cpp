#include "graphics/hair/RibbonBuilder.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace eve::graphics::hair {
namespace {

glm::vec3 safeNormalize(const glm::vec3 &v, const glm::vec3 &fallback) {
    const float len2 = glm::dot(v, v);
    if (len2 < 1e-12f) return fallback;
    return v * (1.f / std::sqrt(len2));
}

}  // namespace

Result<RibbonMesh> buildRibbons(const StrandsDatas &strands, const RibbonParams &params) {
    auto validated = strands.validate();
    if (!validated.ok()) return Result<RibbonMesh>::failure(validated.status());

    if (params.widthScale <= 0.f || !std::isfinite(params.widthScale)) {
        return Result<RibbonMesh>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "buildRibbons: widthScale must be positive",
            "hair.ribbon.widthScale"));
    }

    const glm::vec3 sideHint = safeNormalize(params.sideHint, glm::vec3(1.f, 0.f, 0.f));

    RibbonMesh mesh;
    const size_t curveN = strands.curveCount();
    mesh.posXYZ.reserve(curveN * 8u * 3u);
    mesh.nrmXYZ.reserve(curveN * 8u * 3u);
    mesh.uvST.reserve(curveN * 8u * 2u);
    mesh.indices.reserve(curveN * 7u * 6u);

    for (size_t ci = 0; ci < curveN; ++ci) {
        const auto pts = strands.curvePoints(ci);
        if (pts.size() < 2) continue;

        const uint32_t baseVert = uint32_t(mesh.posXYZ.size() / 3u);
        for (size_t pi = 0; pi < pts.size(); ++pi) {
            const StrandPoint &p = pts[pi];
            glm::vec3 tangent(0.f, 1.f, 0.f);
            if (pi + 1 < pts.size()) {
                tangent = pts[pi + 1].position - p.position;
            } else {
                tangent = p.position - pts[pi - 1].position;
            }
            tangent = safeNormalize(tangent, glm::vec3(0.f, 1.f, 0.f));

            glm::vec3 side = glm::cross(tangent, sideHint);
            if (glm::dot(side, side) < 1e-10f) {
                side = glm::cross(tangent, glm::vec3(0.f, 0.f, 1.f));
            }
            side = safeNormalize(side, glm::vec3(1.f, 0.f, 0.f));

            const float halfW = std::max(p.radius * params.widthScale, params.minWidth);
            const glm::vec3 left  = p.position - side * halfW;
            const glm::vec3 right = p.position + side * halfW;

            auto pushVert = [&](const glm::vec3 &pos, float vSide) {
                mesh.posXYZ.push_back(pos.x);
                mesh.posXYZ.push_back(pos.y);
                mesh.posXYZ.push_back(pos.z);
                mesh.nrmXYZ.push_back(tangent.x);
                mesh.nrmXYZ.push_back(tangent.y);
                mesh.nrmXYZ.push_back(tangent.z);
                mesh.uvST.push_back(vSide);
                mesh.uvST.push_back(p.u);
            };
            pushVert(left, 0.f);
            pushVert(right, 1.f);
        }

        const uint32_t segs = uint32_t(pts.size() - 1u);
        for (uint32_t s = 0; s < segs; ++s) {
            const uint32_t i0 = baseVert + s * 2u;
            const uint32_t i1 = i0 + 1u;
            const uint32_t i2 = i0 + 2u;
            const uint32_t i3 = i0 + 3u;
            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    if (mesh.indices.empty()) {
        return Result<RibbonMesh>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "buildRibbons: produced empty mesh", "hair.ribbon"));
    }
    return Result<RibbonMesh>::success(std::move(mesh));
}

void appendRibbonMesh(RibbonMesh &dst, const RibbonMesh &src) {
    const uint32_t base = uint32_t(dst.vertexCount());
    dst.posXYZ.insert(dst.posXYZ.end(), src.posXYZ.begin(), src.posXYZ.end());
    dst.nrmXYZ.insert(dst.nrmXYZ.end(), src.nrmXYZ.begin(), src.nrmXYZ.end());
    dst.uvST.insert(dst.uvST.end(), src.uvST.begin(), src.uvST.end());
    dst.indices.reserve(dst.indices.size() + src.indices.size());
    for (uint32_t idx : src.indices) dst.indices.push_back(base + idx);
}

}  // namespace eve::graphics::hair
