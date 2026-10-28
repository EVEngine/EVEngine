#include "graphics/hair/Binding.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

namespace eve::graphics::hair {
namespace {

bool meshLooksValid(const SkinTriMesh &mesh) {
    return mesh.posXYZ && mesh.indices && mesh.vertexCount > 0 && mesh.indexCount >= 3 &&
           (mesh.indexCount % 3) == 0;
}

glm::vec3 vertexAt(const SkinTriMesh &mesh, uint32_t index) {
    const float *p = mesh.posXYZ + size_t(index) * 3u;
    return {p[0], p[1], p[2]};
}

bool triangleVerts(const SkinTriMesh &mesh, uint32_t tri, glm::vec3 &a, glm::vec3 &b, glm::vec3 &c) {
    const uint32_t base = tri * 3u;
    if (base + 2u >= uint32_t(mesh.indexCount)) return false;
    const uint32_t i0 = mesh.indices[base];
    const uint32_t i1 = mesh.indices[base + 1u];
    const uint32_t i2 = mesh.indices[base + 2u];
    if (i0 >= uint32_t(mesh.vertexCount) || i1 >= uint32_t(mesh.vertexCount) ||
        i2 >= uint32_t(mesh.vertexCount)) {
        return false;
    }
    a = vertexAt(mesh, i0);
    b = vertexAt(mesh, i1);
    c = vertexAt(mesh, i2);
    return true;
}

glm::vec3 triangleNormal(const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c) {
    const glm::vec3 n = glm::cross(b - a, c - a);
    const float len2 = glm::dot(n, n);
    if (len2 < 1e-20f) return {0.f, 1.f, 0.f};
    return n * (1.f / std::sqrt(len2));
}

/** Closest point on triangle; returns squared distance and barycentrics. */
float closestOnTriangle(const glm::vec3 &p, const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c,
                        glm::vec3 &bary, glm::vec3 &closest) {
    const glm::vec3 ab = b - a;
    const glm::vec3 ac = c - a;
    const glm::vec3 ap = p - a;

    const float d1 = glm::dot(ab, ap);
    const float d2 = glm::dot(ac, ap);
    if (d1 <= 0.f && d2 <= 0.f) {
        bary = {1.f, 0.f, 0.f};
        closest = a;
        return glm::dot(ap, ap);
    }

    const glm::vec3 bp = p - b;
    const float d3 = glm::dot(ab, bp);
    const float d4 = glm::dot(ac, bp);
    if (d3 >= 0.f && d4 <= d3) {
        bary = {0.f, 1.f, 0.f};
        closest = b;
        return glm::dot(bp, bp);
    }

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) {
        const float v = d1 / (d1 - d3);
        bary = {1.f - v, v, 0.f};
        closest = a + v * ab;
        const glm::vec3 diff = p - closest;
        return glm::dot(diff, diff);
    }

    const glm::vec3 cp = p - c;
    const float d5 = glm::dot(ab, cp);
    const float d6 = glm::dot(ac, cp);
    if (d6 >= 0.f && d5 <= d6) {
        bary = {0.f, 0.f, 1.f};
        closest = c;
        return glm::dot(cp, cp);
    }

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) {
        const float w = d2 / (d2 - d6);
        bary = {1.f - w, 0.f, w};
        closest = a + w * ac;
        const glm::vec3 diff = p - closest;
        return glm::dot(diff, diff);
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
        const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        bary = {0.f, 1.f - w, w};
        closest = b + w * (c - b);
        const glm::vec3 diff = p - closest;
        return glm::dot(diff, diff);
    }

    const float denom = 1.f / (va + vb + vc);
    const float v = vb * denom;
    const float w = vc * denom;
    bary = {1.f - v - w, v, w};
    closest = a + ab * v + ac * w;
    const glm::vec3 diff = p - closest;
    return glm::dot(diff, diff);
}

glm::vec3 evaluateRoot(const SkinTriMesh &mesh, const RootAttach &attach) {
    glm::vec3 a, b, c;
    if (!triangleVerts(mesh, attach.triangleIndex, a, b, c)) return attach.restRoot;
    return attach.barycentric.x * a + attach.barycentric.y * b + attach.barycentric.z * c;
}

glm::mat3 rotationFromTo(const glm::vec3 &from, const glm::vec3 &to) {
    const glm::vec3 f = glm::normalize(from);
    const glm::vec3 t = glm::normalize(to);
    const float cosA = std::clamp(glm::dot(f, t), -1.f, 1.f);
    if (cosA > 0.9999f) return glm::mat3(1.f);
    if (cosA < -0.9999f) {
        // 180°: pick a stable perpendicular and reflect through that plane twice.
        glm::vec3 axis = glm::cross(f, glm::vec3(1.f, 0.f, 0.f));
        if (glm::dot(axis, axis) < 1e-8f) axis = glm::cross(f, glm::vec3(0.f, 0.f, 1.f));
        axis = glm::normalize(axis);
        glm::mat3 r(0.f);
        r[0][0] = 2.f * axis.x * axis.x - 1.f;
        r[1][0] = 2.f * axis.x * axis.y;
        r[2][0] = 2.f * axis.x * axis.z;
        r[0][1] = 2.f * axis.y * axis.x;
        r[1][1] = 2.f * axis.y * axis.y - 1.f;
        r[2][1] = 2.f * axis.y * axis.z;
        r[0][2] = 2.f * axis.z * axis.x;
        r[1][2] = 2.f * axis.z * axis.y;
        r[2][2] = 2.f * axis.z * axis.z - 1.f;
        return r;
    }
    const glm::vec3 axis = glm::normalize(glm::cross(f, t));
    const float s = std::sqrt(std::max(0.f, 1.f - cosA * cosA));
    const float cx = 1.f - cosA;
    const float x = axis.x, y = axis.y, z = axis.z;
    glm::mat3 r(1.f);
    r[0][0] = cosA + x * x * cx;
    r[0][1] = y * x * cx + z * s;
    r[0][2] = z * x * cx - y * s;
    r[1][0] = x * y * cx - z * s;
    r[1][1] = cosA + y * y * cx;
    r[1][2] = z * y * cx + x * s;
    r[2][0] = x * z * cx + y * s;
    r[2][1] = y * z * cx - x * s;
    r[2][2] = cosA + z * z * cx;
    return r;
}

}  // namespace

Result<void> GroomBinding::build(const StrandsDatas &strands, const SkinTriMesh &restMesh) {
    auto ok = strands.validate();
    if (!ok.ok()) return Result<void>::failure(ok.status());
    if (!meshLooksValid(restMesh)) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomBinding::build: invalid skin mesh",
            "hair.binding.mesh"));
    }

    const uint32_t triCount = uint32_t(restMesh.indexCount / 3);
    roots_.clear();
    roots_.reserve(strands.curveCount());

    for (size_t ci = 0; ci < strands.curveCount(); ++ci) {
        const auto pts = strands.curvePoints(ci);
        if (pts.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvariantViolation, "GroomBinding::build: empty curve",
                "hair.binding.curve"));
        }
        const glm::vec3 root = pts[0].position;

        RootAttach attach;
        attach.restRoot = root;
        float bestDist2 = std::numeric_limits<float>::max();
        bool found = false;
        for (uint32_t ti = 0; ti < triCount; ++ti) {
            glm::vec3 a, b, c;
            if (!triangleVerts(restMesh, ti, a, b, c)) {
                return Result<void>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "GroomBinding::build: bad triangle index",
                    "hair.binding.triangle"));
            }
            glm::vec3 bary, closest;
            const float d2 = closestOnTriangle(root, a, b, c, bary, closest);
            if (d2 < bestDist2) {
                bestDist2 = d2;
                attach.triangleIndex = ti;
                attach.barycentric = bary;
                attach.restNormal = triangleNormal(a, b, c);
                found = true;
            }
        }
        if (!found) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "GroomBinding::build: no triangle found",
                "hair.binding"));
        }
        roots_.push_back(attach);
    }
    return Result<void>::success();
}

void GroomBinding::clear() { roots_.clear(); }

const RootAttach *GroomBinding::rootAt(size_t index) const {
    if (index >= roots_.size()) return nullptr;
    return &roots_[index];
}

Result<StrandsDatas> GroomBinding::deform(const StrandsDatas &restStrands,
                                          const SkinTriMesh &deformedMesh,
                                          BindingDeformMode mode) const {
    auto ok = restStrands.validate();
    if (!ok.ok()) return Result<StrandsDatas>::failure(ok.status());
    if (!meshLooksValid(deformedMesh)) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GroomBinding::deform: invalid skin mesh",
            "hair.binding.mesh"));
    }
    if (roots_.size() != restStrands.curveCount()) {
        return Result<StrandsDatas>::failure(Diagnostic::error(
            DiagnosticCode::InvariantViolation,
            "GroomBinding::deform: root count mismatch (rebuild binding)", "hair.binding.roots"));
    }

    std::vector<StrandPoint> points(restStrands.points().begin(), restStrands.points().end());
    std::vector<StrandCurve> curves(restStrands.curves().begin(), restStrands.curves().end());

    for (size_t ci = 0; ci < roots_.size(); ++ci) {
        const RootAttach &attach = roots_[ci];
        const StrandCurve &curve = curves[ci];
        if (curve.pointCount == 0) continue;

        glm::vec3 a, b, c;
        if (!triangleVerts(deformedMesh, attach.triangleIndex, a, b, c)) {
            return Result<StrandsDatas>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "GroomBinding::deform: bad deformed triangle",
                "hair.binding.triangle"));
        }
        const glm::vec3 skinRoot =
            attach.barycentric.x * a + attach.barycentric.y * b + attach.barycentric.z * c;
        const glm::vec3 delta = skinRoot - attach.restRoot;

        if (mode == BindingDeformMode::Rigid) {
            for (uint32_t pi = 0; pi < curve.pointCount; ++pi) {
                points[curve.pointOffset + pi].position += delta;
            }
        } else {
            const glm::vec3 defN = triangleNormal(a, b, c);
            const glm::mat3 rot = rotationFromTo(attach.restNormal, defN);
            for (uint32_t pi = 0; pi < curve.pointCount; ++pi) {
                StrandPoint &pt = points[curve.pointOffset + pi];
                const glm::vec3 local = pt.position - attach.restRoot;
                pt.position = skinRoot + rot * local;
            }
        }
    }

    StrandsDatas out;
    out.setPoints(std::move(points));
    out.setCurves(std::move(curves));
    auto valid = out.validate();
    if (!valid.ok()) return Result<StrandsDatas>::failure(valid.status());
    return Result<StrandsDatas>::success(std::move(out));
}

}  // namespace eve::graphics::hair
