#include "buildingfx/BuildingFx.h"

#include "building/BuildingDef.h"
#include "building/Ghost.h"
#include "building/PlacementSystem.h"
#include "building/PlacementSession.h"
#include "building/PlacementWorld.h"
#include "grid/GridConfig.h"
#include "common/ECS.h"
#include "graphics/Graphics.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>

#include <algorithm>
#include <cmath>

namespace eve::buildingfx {

// Color lives in eve::graphics (see graphics/Canvas.h); keep the unqualified form.
using eve::graphics::Color;

Module_IMPL(BuildingFx, new BuildingFx());

namespace {

float toFloat(const std::string &s, float fallback) {
    if (s.empty()) return fallback;
    try {
        return std::stof(s);
    } catch (...) {
        return fallback;
    }
}

graphics::Graphics *gfxOrNull() { return getModInst(graphics, Graphics); }

bool toBool(const std::string &value, bool fallback);

std::string visualValue(const building::BuildingDefinition &definition,
                        const building::PlacedBuilding &placed,
                        const building::PlacementWorld &world, bool use3d,
                        const std::string &key) {
    const std::string base = use3d ? definition.getVisual3d(key) : definition.getVisual2d(key);
    if (placed.placementKind != "edge") return base;
    const std::string variant = building::PlacementSystem::edgeVariant(world, placed.instanceId);
    const int mask = building::PlacementSystem::edgeConnectionMask(world, placed.instanceId);
    const std::string maskKey =
        "variant." + variant + ".mask." + std::to_string(mask) + "." + key;
    const std::string variantKey = "variant." + variant + "." + key;
    const std::string classValue = use3d ? definition.getVisual3d(variantKey, base)
                                         : definition.getVisual2d(variantKey, base);
    return use3d ? definition.getVisual3d(maskKey, classValue)
                 : definition.getVisual2d(maskKey, classValue);
}

struct TopologyTransform {
    std::string variant;
    int mask = 0;
    float rotationDegrees = 0.f;
    bool mirrorX = false;
    bool mirrorZ = false;
};

TopologyTransform topologyTransform(const building::BuildingDefinition &definition,
                                    const building::PlacedBuilding &placed,
                                    const building::PlacementWorld &world, bool use3d) {
    TopologyTransform result;
    if (placed.placementKind != "edge") return result;
    result.variant = building::PlacementSystem::edgeVariant(world, placed.instanceId);
    result.mask = building::PlacementSystem::edgeConnectionMask(world, placed.instanceId);
    if (result.variant == "end") result.mirrorX = (result.mask & 0x01) != 0;
    if (result.variant == "corner") {
        result.mirrorX = (result.mask & 0x0c) != 0;
        result.mirrorZ = (result.mask & 0x14) != 0;
    }
    result.rotationDegrees =
        toFloat(visualValue(definition, placed, world, use3d, "rotationDeg"), 0.f);
    result.mirrorX = toBool(visualValue(definition, placed, world, use3d, "mirrorX"),
                            result.mirrorX);
    result.mirrorZ = toBool(visualValue(definition, placed, world, use3d, "mirrorZ"),
                            result.mirrorZ);
    return result;
}

void placedWorldPosition(const building::PlacedBuilding &placed,
                         const building::PlacementWorld &world, float &x, float &y, float &z) {
    if (world.getGrid().plane == grid::GridPlane::XZ) {
        x = placed.worldX;
        y = placed.elevation + float(placed.level) * world.getFloorHeight();
        z = placed.worldY;
    } else {
        x = placed.worldX;
        y = placed.worldY;
        z = placed.elevation + float(placed.level) * world.getFloorHeight();
    }
}

bool toBool(const std::string &value, bool fallback) {
    if (value.empty()) return fallback;
    if (value == "1" || value == "true" || value == "yes") return true;
    if (value == "0" || value == "false" || value == "no") return false;
    return fallback;
}

float cornerVisualSize(const building::BuildingDefinition &definition,
                       const building::PlacedBuilding &placed,
                       const building::PlacementWorld &world, bool use3d,
                       const std::string &axisKey) {
    const float fallback = std::min(world.getGrid().cellW, world.getGrid().cellH) * 0.2f;
    const std::string axisValue = visualValue(definition, placed, world, use3d, axisKey);
    if (!axisValue.empty()) return toFloat(axisValue, fallback);
    return toFloat(visualValue(definition, placed, world, use3d, "size"), fallback);
}

float freeVisualSize(const building::BuildingDefinition &definition,
                     const building::PlacedBuilding &placed,
                     const building::PlacementWorld &world, bool use3d,
                     const std::string &axisKey) {
    float fallback = placed.freeRadius > 0.f
                         ? placed.freeRadius * 2.f
                         : definition.freeRadiusCells * 2.f *
                               std::min(world.getGrid().cellW, world.getGrid().cellH);
    if (placed.freeHalfWidth > 0.f && placed.freeHalfHeight > 0.f) {
        fallback = axisKey == "width" ? placed.freeHalfWidth * 2.f
                                      : placed.freeHalfHeight * 2.f;
    }
    return toFloat(visualValue(definition, placed, world, use3d, axisKey), fallback);
}

void applySurfaceRotation(const building::PlacedBuilding &placed, float rotationDegrees,
                          graphics::Renderable3D::Transform3D &transform) {
    if (placed.surfaceId.empty()) return;
    const glm::vec3 tangent(placed.surfaceTangentX, placed.surfaceTangentY,
                            placed.surfaceTangentZ);
    const glm::vec3 normal(placed.surfaceNormalX, placed.surfaceNormalY,
                           placed.surfaceNormalZ);
    const glm::vec3 bitangent = glm::normalize(glm::cross(tangent, normal));
    glm::mat4 frame(1.f);
    frame[0] = glm::vec4(tangent, 0.f);
    frame[1] = glm::vec4(normal, 0.f);
    frame[2] = glm::vec4(bitangent, 0.f);
    frame *= glm::rotate(glm::mat4(1.f), glm::radians(rotationDegrees),
                         glm::vec3(0.f, 1.f, 0.f));
    glm::extractEulerAngleYXZ(frame, transform.yaw, transform.pitch, transform.roll);
}

}  // namespace

eve::Result<BuildingFx::CurveMeshData> BuildingFx::buildEdgeCurveMesh(
    const building::PlacementWorld &world,
    const std::vector<CurveControlPoint> &controlPoints, int subdivisions, float width,
    float height, float elevation) {
    if (controlPoints.size() != 4 || subdivisions < 2 || subdivisions > 4096 ||
        !std::isfinite(width) || !std::isfinite(height) || !std::isfinite(elevation) ||
        width <= 0.f || height <= 0.f) {
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "curve mesh requires four controls, 2..4096 subdivisions, and positive dimensions",
            {}, {}, "buildingfx.edge-curve-mesh"));
    }
    for (const CurveControlPoint &point : controlPoints) {
        if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
            return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument, "curve mesh controls must be finite", {},
                {}, "buildingfx.edge-curve-mesh"));
        }
    }
    const auto layout = world.getGrid().layout;
    if (layout == grid::GridLayout::Hexagon || layout == grid::GridLayout::Staggered) {
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "continuous curve mesh requires an affine rectangle or isometric grid", {}, {},
            "buildingfx.edge-curve-mesh"));
    }

    float originX = 0.f, originY = 0.f;
    float basisXX = 0.f, basisXY = 0.f;
    float basisYX = 0.f, basisYY = 0.f;
    world.cellToWorldPlane(0, 0, originX, originY);
    world.cellToWorldPlane(1, 0, basisXX, basisXY);
    world.cellToWorldPlane(0, 1, basisYX, basisYY);
    basisXX -= originX;
    basisXY -= originY;
    basisYX -= originX;
    basisYY -= originY;

    struct Point {
        float x = 0.f;
        float y = 0.f;
    };
    const auto evaluate = [&](int sample) {
        const double t = static_cast<double>(sample) / static_cast<double>(subdivisions);
        const double u = 1.0 - t;
        const double gx = u * u * u * controlPoints[0].x +
                          3.0 * u * u * t * controlPoints[1].x +
                          3.0 * u * t * t * controlPoints[2].x +
                          t * t * t * controlPoints[3].x;
        const double gy = u * u * u * controlPoints[0].y +
                          3.0 * u * u * t * controlPoints[1].y +
                          3.0 * u * t * t * controlPoints[2].y +
                          t * t * t * controlPoints[3].y;
        return Point{originX + static_cast<float>(gx) * basisXX +
                         static_cast<float>(gy) * basisYX,
                     originY + static_cast<float>(gx) * basisXY +
                         static_cast<float>(gy) * basisYY};
    };
    std::vector<Point> samples;
    std::vector<float> distances(static_cast<size_t>(subdivisions + 1), 0.f);
    samples.reserve(static_cast<size_t>(subdivisions + 1));
    CurveMeshData mesh;
    mesh.sampleCount = subdivisions + 1;
    for (int sample = 0; sample <= subdivisions; ++sample) {
        samples.push_back(evaluate(sample));
        if (sample == 0) continue;
        distances[sample] = distances[sample - 1] +
                            std::hypot(samples[sample].x - samples[sample - 1].x,
                                       samples[sample].y - samples[sample - 1].y);
    }
    mesh.length = distances.back();
    if (!(mesh.length > 1e-5f)) {
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "curve mesh has zero projected length", {},
            {}, "buildingfx.edge-curve-mesh"));
    }

    const bool xz = world.getGrid().plane == grid::GridPlane::XZ;
    const auto appendVertex = [&](Point point, float vertical, float nx, float ny,
                                  float normalVertical, float u, float v) {
        if (xz) {
            mesh.positions.insert(mesh.positions.end(), {point.x, elevation + vertical, point.y});
            mesh.normals.insert(mesh.normals.end(), {nx, normalVertical, ny});
        } else {
            mesh.positions.insert(mesh.positions.end(), {point.x, point.y, elevation + vertical});
            mesh.normals.insert(mesh.normals.end(), {nx, ny, normalVertical});
        }
        mesh.uvs.insert(mesh.uvs.end(), {u, v});
    };
    mesh.positions.reserve(static_cast<size_t>((subdivisions + 1) * 8 + 8) * 3);
    mesh.normals.reserve(mesh.positions.capacity());
    mesh.uvs.reserve(static_cast<size_t>((subdivisions + 1) * 8 + 8) * 2);
    mesh.indices.reserve(static_cast<size_t>(subdivisions) * 24 + 12);
    const float halfWidth = width * 0.5f;
    std::vector<Point> left(samples.size()), right(samples.size()), tangents(samples.size());
    for (int sample = 0; sample <= subdivisions; ++sample) {
        const Point before = samples[static_cast<size_t>(sample == 0 ? 0 : sample - 1)];
        const Point after = samples[static_cast<size_t>(
            sample == subdivisions ? subdivisions : sample + 1)];
        float tx = after.x - before.x;
        float ty = after.y - before.y;
        const float tangentLength = std::hypot(tx, ty);
        if (!(tangentLength > 1e-5f)) {
            return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "curve mesh contains an unresolved zero-length tangent", std::to_string(sample),
                {}, "buildingfx.edge-curve-mesh"));
        }
        tx /= tangentLength;
        ty /= tangentLength;
        tangents[sample] = {tx, ty};
        const float px = -ty;
        const float py = tx;
        left[sample] = {samples[sample].x - px * halfWidth,
                        samples[sample].y - py * halfWidth};
        right[sample] = {samples[sample].x + px * halfWidth,
                         samples[sample].y + py * halfWidth};
        const float u = distances[sample] / mesh.length;
        appendVertex(left[sample], 0.f, -px, -py, 0.f, u, 0.f);
        appendVertex(left[sample], height, -px, -py, 0.f, u, 1.f);
        appendVertex(right[sample], 0.f, px, py, 0.f, u, 0.f);
        appendVertex(right[sample], height, px, py, 0.f, u, 1.f);
        appendVertex(left[sample], height, 0.f, 0.f, 1.f, u, 0.f);
        appendVertex(right[sample], height, 0.f, 0.f, 1.f, u, 1.f);
        appendVertex(left[sample], 0.f, 0.f, 0.f, -1.f, u, 0.f);
        appendVertex(right[sample], 0.f, 0.f, 0.f, -1.f, u, 1.f);
    }
    for (int segment = 0; segment < subdivisions; ++segment) {
        const uint32_t a = static_cast<uint32_t>(segment * 8);
        const uint32_t b = a + 8;
        const uint32_t faces[] = {a, b, b + 1, a, b + 1, a + 1,
                                  a + 2, a + 3, b + 3, a + 2, b + 3, b + 2,
                                  a + 4, b + 4, b + 5, a + 4, b + 5, a + 5,
                                  a + 6, a + 7, b + 7, a + 6, b + 7, b + 6};
        mesh.indices.insert(mesh.indices.end(), std::begin(faces), std::end(faces));
    }
    const auto appendCap = [&](int sample, bool start) {
        const Point tangent = tangents[static_cast<size_t>(sample)];
        const float sign = start ? -1.f : 1.f;
        const uint32_t base = static_cast<uint32_t>(mesh.positions.size() / 3);
        appendVertex(left[sample], 0.f, tangent.x * sign, tangent.y * sign, 0.f, 0.f, 0.f);
        appendVertex(right[sample], 0.f, tangent.x * sign, tangent.y * sign, 0.f, 1.f, 0.f);
        appendVertex(left[sample], height, tangent.x * sign, tangent.y * sign, 0.f, 0.f, 1.f);
        appendVertex(right[sample], height, tangent.x * sign, tangent.y * sign, 0.f, 1.f, 1.f);
        const uint32_t capStart[] = {base, base + 2, base + 3, base, base + 3, base + 1};
        const uint32_t capEnd[] = {base, base + 3, base + 2, base, base + 1, base + 3};
        const uint32_t *indices = start ? capStart : capEnd;
        mesh.indices.insert(mesh.indices.end(), indices, indices + 6);
    };
    appendCap(0, true);
    appendCap(subdivisions, false);
    return eve::Result<CurveMeshData>::success(std::move(mesh));
}

eve::Result<BuildingFx::CurveMeshData> BuildingFx::buildSurfaceCurveMesh(
    const std::vector<CurveSurfaceSample> &samples, float width, float height) {
    if (samples.size() < 2 || !std::isfinite(width) || !std::isfinite(height) ||
        width <= 0.f || height <= 0.f)
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "surface curve mesh requires at least two frames and positive dimensions", {}, {},
            "buildingfx.surface-curve-mesh"));
    std::vector<glm::vec3> centers, normals, tangents, laterals;
    std::vector<float> distances(samples.size(), 0.f);
    centers.reserve(samples.size());
    normals.reserve(samples.size());
    for (const CurveSurfaceSample &sample : samples) {
        const glm::vec3 center(sample.x, sample.y, sample.z);
        const glm::vec3 normal(sample.normalX, sample.normalY, sample.normalZ);
        if (!std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(center.z) ||
            !std::isfinite(normal.x) || !std::isfinite(normal.y) ||
            !std::isfinite(normal.z) || glm::length(normal) <= 1e-5f)
            return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "surface curve frames require finite positions and non-zero normals", {}, {},
                "buildingfx.surface-curve-mesh"));
        centers.push_back(center);
        normals.push_back(glm::normalize(normal));
    }
    for (size_t index = 1; index < centers.size(); ++index)
        distances[index] = distances[index - 1] + glm::length(centers[index] - centers[index - 1]);
    if (distances.back() <= 1e-5f)
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "surface curve has zero length", {}, {},
            "buildingfx.surface-curve-mesh"));
    for (size_t index = 0; index < centers.size(); ++index) {
        const glm::vec3 before = centers[index == 0 ? 0 : index - 1];
        const glm::vec3 after = centers[index + 1 == centers.size() ? index : index + 1];
        glm::vec3 tangent = after - before;
        tangent -= normals[index] * glm::dot(tangent, normals[index]);
        if (glm::length(tangent) <= 1e-5f)
            return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvalidArgument,
                "surface curve tangent is parallel to its normal", std::to_string(index), {},
                "buildingfx.surface-curve-mesh"));
        tangent = glm::normalize(tangent);
        tangents.push_back(tangent);
        laterals.push_back(glm::normalize(glm::cross(normals[index], tangent)));
    }
    CurveMeshData mesh;
    mesh.sampleCount = static_cast<int>(samples.size());
    mesh.length = distances.back();
    const auto append = [&](const glm::vec3 &position, const glm::vec3 &normal, float u,
                            float v) {
        mesh.positions.insert(mesh.positions.end(), {position.x, position.y, position.z});
        mesh.normals.insert(mesh.normals.end(), {normal.x, normal.y, normal.z});
        mesh.uvs.insert(mesh.uvs.end(), {u, v});
    };
    const float halfWidth = width * 0.5f;
    for (size_t index = 0; index < centers.size(); ++index) {
        const glm::vec3 left = centers[index] - laterals[index] * halfWidth;
        const glm::vec3 right = centers[index] + laterals[index] * halfWidth;
        const glm::vec3 leftTop = left + normals[index] * height;
        const glm::vec3 rightTop = right + normals[index] * height;
        const float u = distances[index] / mesh.length;
        append(left, -laterals[index], u, 0.f);
        append(leftTop, -laterals[index], u, 1.f);
        append(right, laterals[index], u, 0.f);
        append(rightTop, laterals[index], u, 1.f);
        append(leftTop, normals[index], u, 0.f);
        append(rightTop, normals[index], u, 1.f);
        append(left, -normals[index], u, 0.f);
        append(right, -normals[index], u, 1.f);
    }
    for (size_t segment = 0; segment + 1 < centers.size(); ++segment) {
        const uint32_t a = static_cast<uint32_t>(segment * 8);
        const uint32_t b = a + 8;
        const uint32_t faces[] = {a, b, b + 1, a, b + 1, a + 1,
                                  a + 2, a + 3, b + 3, a + 2, b + 3, b + 2,
                                  a + 4, b + 4, b + 5, a + 4, b + 5, a + 5,
                                  a + 6, a + 7, b + 7, a + 6, b + 7, b + 6};
        mesh.indices.insert(mesh.indices.end(), std::begin(faces), std::end(faces));
    }
    const auto cap = [&](size_t index, bool start) {
        const glm::vec3 left = centers[index] - laterals[index] * halfWidth;
        const glm::vec3 right = centers[index] + laterals[index] * halfWidth;
        const glm::vec3 capNormal = tangents[index] * (start ? -1.f : 1.f);
        const uint32_t base = static_cast<uint32_t>(mesh.positions.size() / 3);
        append(left, capNormal, 0.f, 0.f);
        append(right, capNormal, 1.f, 0.f);
        append(left + normals[index] * height, capNormal, 0.f, 1.f);
        append(right + normals[index] * height, capNormal, 1.f, 1.f);
        const uint32_t startIndices[] = {base, base + 2, base + 3, base, base + 3, base + 1};
        const uint32_t endIndices[] = {base, base + 3, base + 2, base, base + 1, base + 3};
        mesh.indices.insert(mesh.indices.end(), start ? std::begin(startIndices)
                                                      : std::begin(endIndices),
                            start ? std::end(startIndices) : std::end(endIndices));
    };
    cap(0, true);
    cap(centers.size() - 1, false);
    return eve::Result<CurveMeshData>::success(std::move(mesh));
}

eve::Result<BuildingFx::CurveMeshData> BuildingFx::buildEdgeCurveGroupMeshForInstance(
    const building::PlacementWorld &world, int instanceId, float width, float height,
    float elevation) {
    if (instanceId <= 0) {
        return eve::Result<CurveMeshData>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "curve mesh requires a positive instance id",
            std::to_string(instanceId), {}, "buildingfx.edge-curve-group-mesh"));
    }
    auto groupResult = world.edgeCurveGroupForInstance(instanceId);
    if (!groupResult) return eve::Result<CurveMeshData>::failure(*groupResult.error());

    const building::EdgeCurveGroup &group = groupResult.value();
    if (!group.surfaceSamples.empty()) {
        std::vector<CurveSurfaceSample> samples;
        samples.reserve(group.surfaceSamples.size());
        for (const building::EdgeCurveSurfaceSample &sample : group.surfaceSamples)
            samples.push_back({sample.worldX, sample.worldY, sample.worldZ, sample.normalX,
                               sample.normalY, sample.normalZ});
        return buildSurfaceCurveMesh(samples, width, height);
    }
    std::vector<CurveControlPoint> controls;
    controls.reserve(group.controlPoints.size());
    for (const building::EdgeCurveControlPoint &point : group.controlPoints)
        controls.push_back({point.x, point.y});
    return buildEdgeCurveMesh(world, controls, group.subdivisions, width, height, elevation);
}

eve::Result<void> BuildingFx::updateEdgeCurvePreview(
    building::PlacementWorld *world, const std::string &buildingId,
    const std::vector<CurveControlPoint> &controlPoints, int subdivisions, int level) {
    const auto state = states_.find(world);
    const building::BuildingDefinition *definition =
        building::BuildingRegistry::find(buildingId);
    if (!world || state == states_.end() || !definition || !is3d(*definition)) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "curve preview requires an attached world and a 3D building definition",
            buildingId, {}, "buildingfx.edge-curve-preview"));
    }
    const float width = toFloat(definition->getVisual3d("thickness"),
                                world->getCellSize() * 0.1f);
    const float height = toFloat(definition->getVisual3d("height"), 1.f);
    const float elevation = static_cast<float>(level) * world->getFloorHeight();
    auto generated = buildEdgeCurveMesh(*world, controlPoints, subdivisions, width, height,
                                        elevation);
    if (!generated.ok()) return eve::Result<void>::failure(*generated.error());

    return presentCurvePreview(state->second, std::move(generated).takeValue(), controlPoints,
                               subdivisions, width, height, elevation);
}

eve::Result<void> BuildingFx::updateEdgeCurveSurfacePreview(
    building::PlacementWorld *world, building::PlacementSession *session, int subdivisions,
    const std::string &surfaceName) {
    const auto state = states_.find(world);
    if (!world || state == states_.end() || !session || session->getWorld() != world ||
        !session->isActive()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "surface curve preview requires an attached world and its active session", {}, {},
            "buildingfx.edge-curve-surface-preview"));
    }
    const building::BuildingDefinition *definition =
        building::BuildingRegistry::find(session->getBuildingId());
    if (!definition || !is3d(*definition) ||
        session->edgeCurveControlPoints().size() != 4) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation,
            "surface curve preview requires a 3D edge definition and four controls",
            session->getBuildingId(), {}, "buildingfx.edge-curve-surface-preview"));
    }
    auto sampled = building::PlacementSystem::sampleEdgeCurveSurface(
        *world, surfaceName, session->edgeCurveControlPoints(), subdivisions);
    if (!sampled.ok()) return eve::Result<void>::failure(sampled.status());
    std::vector<CurveSurfaceSample> samples;
    samples.reserve(sampled.value().samples.size());
    for (const building::EdgeCurveSurfaceSample &sample : sampled.value().samples)
        samples.push_back({sample.worldX, sample.worldY, sample.worldZ, sample.normalX,
                           sample.normalY, sample.normalZ});
    const float width =
        toFloat(definition->getVisual3d("thickness"), world->getCellSize() * 0.1f);
    const float height = toFloat(definition->getVisual3d("height"), 1.f);
    auto generated = buildSurfaceCurveMesh(samples, width, height);
    if (!generated.ok()) return eve::Result<void>::failure(generated.status());
    std::vector<CurveControlPoint> controls;
    controls.reserve(session->edgeCurveControlPoints().size());
    for (const building::PlacementSystem::EdgeCurvePoint &point :
         session->edgeCurveControlPoints())
        controls.push_back({point.x, point.y});
    return presentCurvePreview(state->second, std::move(generated).takeValue(),
                               std::move(controls), subdivisions, width, height, 0.f,
                               sampled.value().surfaceId, sampled.value().surfaceRevision);
}

CurvePreviewUpdateStatus BuildingFx::updateEdgeCurveSurfacePreviewStatus(
    building::PlacementWorld *world, building::PlacementSession *session, int subdivisions,
    const std::string &surfaceName) {
    return updateEdgeCurveSurfacePreview(world, session, subdivisions, surfaceName).ok()
               ? CurvePreviewUpdateStatus::Updated
               : CurvePreviewUpdateStatus::Rejected;
}

eve::Result<void> BuildingFx::presentCurvePreview(
    WorldState &state, CurveMeshData mesh, std::vector<CurveControlPoint> controls,
    int subdivisions, float width, float height, float elevation, std::string surfaceId,
    std::uint64_t surfaceRevision) {
    WorldState::CurveVisual &preview = state.curvePreview;
    preview.cpuMesh = std::move(mesh);
    preview.controlPoints = std::move(controls);
    preview.subdivisions = subdivisions;
    preview.width = width;
    preview.height = height;
    preview.elevation = elevation;
    preview.surfaceId = std::move(surfaceId);
    preview.surfaceRevision = surfaceRevision;
    preview.active = true;
    preview.fallbackReason.clear();

    graphics::Graphics *gfx = gfxOrNull();
    if (!gfx) {
        preview.fallbackReason = "graphics_unavailable";
        if (preview.renderable) preview.renderable->meshRenderer()->visible = false;
        return eve::Result<void>::success();
    }
    if (preview.mesh &&
        !gfx->updateMeshVertices(
            preview.mesh, preview.cpuMesh.positions.data(), preview.cpuMesh.normals.data(),
            preview.cpuMesh.uvs.data(), static_cast<int>(preview.cpuMesh.positions.size() / 3),
            preview.cpuMesh.indices.data(), static_cast<int>(preview.cpuMesh.indices.size())))
        preview.mesh = nullptr;
    if (!preview.mesh) {
        try {
            preview.mesh = gfx->newMeshFromArrays(
                preview.cpuMesh.positions.data(), preview.cpuMesh.normals.data(),
                preview.cpuMesh.uvs.data(),
                static_cast<int>(preview.cpuMesh.positions.size() / 3),
                preview.cpuMesh.indices.data(),
                static_cast<int>(preview.cpuMesh.indices.size()));
        } catch (...) {
            preview.mesh = nullptr;
        }
    }
    if (!preview.mesh) {
        preview.fallbackReason = "mesh_upload_failed";
        return eve::Result<void>::success();
    }
    if (!preview.renderable) preview.renderable = graphics::Renderable3D::create();
    auto renderer = preview.renderable->meshRenderer();
    renderer->mesh = preview.mesh;
    renderer->r = 0.2f;
    renderer->g = 0.9f;
    renderer->b = 0.35f;
    renderer->a = 0.55f;
    renderer->visible = true;
    renderer->castShadow = false;
    renderer->receiveShadow = false;
    return eve::Result<void>::success();
}

void BuildingFx::clearEdgeCurvePreview(building::PlacementWorld *world) {
    const auto state = states_.find(world);
    if (state == states_.end()) return;
    WorldState::CurveVisual &preview = state->second.curvePreview;
    if (preview.renderable) {
        ecs::DestroyEntity(preview.renderable);
        preview.renderable = nullptr;
    }
    preview.active = false;
    preview.cpuMesh = {};
    preview.controlPoints.clear();
    preview.fallbackReason.clear();
}

bool BuildingFx::hasEdgeCurvePreview(building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    return state != states_.end() && state->second.curvePreview.active &&
           !state->second.curvePreview.cpuMesh.positions.empty();
}

std::string BuildingFx::getEdgeCurvePreviewFallbackReason(
    building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return "world_not_attached";
    return state->second.curvePreview.active
               ? state->second.curvePreview.fallbackReason
               : "curve_preview_not_active";
}

std::string BuildingFx::getEdgeCurvePreviewSurfaceId(
    building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    if (state == states_.end() || !state->second.curvePreview.active) return {};
    return state->second.curvePreview.surfaceId;
}

std::uint64_t BuildingFx::getEdgeCurvePreviewSurfaceRevision(
    building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    if (state == states_.end() || !state->second.curvePreview.active) return 0;
    return state->second.curvePreview.surfaceRevision;
}

bool BuildingFx::attach(building::PlacementWorld *world) {
    if (!world) return false;
    states_[world];  // ensure entry
    return true;
}

bool BuildingFx::detach(building::PlacementWorld *world) {
    if (!world) return false;
    auto it = states_.find(world);
    if (it == states_.end()) return false;
    destroyAll(it->second);
    states_.erase(it);
    return true;
}

bool BuildingFx::isAttached(building::PlacementWorld *world) const {
    return world != nullptr && states_.count(world) > 0;
}

int BuildingFx::getAttachedCount() const { return int(states_.size()); }

bool BuildingFx::is3d(const building::BuildingDefinition &def) const {
    return def.renderMode == "3d" || def.renderMode == "3D";
}

graphics::Mesh *BuildingFx::cubeMesh(graphics::Graphics *gfx) {
    if (!gfx) return nullptr;
    if (!cubeMesh_) cubeMesh_ = gfx->newMeshCube(1.f);
    return cubeMesh_;
}

void BuildingFx::createVisual(WorldState &st, const building::BuildingDefinition &def,
                              const building::PlacedBuilding &pb,
                              building::PlacementWorld *world, Visual &v, float alpha) {
    (void)st;
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    int effW = def.footprintW;
    int effH = def.footprintH;
    building::PlacementSystem::effectiveFootprint(def, pb.rotationDeg, &effW, &effH);
    const TopologyTransform topology = topologyTransform(def, pb, *world, is3d(def));
    v.topologyVariant = topology.variant;
    v.topologyMask = topology.mask;

    if (is3d(def)) {
        auto *r = graphics::Renderable3D::create();
        float wx = 0.f, wy = 0.f, wz = 0.f;
        placedWorldPosition(pb, *world, wx, wy, wz);
        auto tr = r->transform();
        tr->x = wx + toFloat(def.getVisual3d("offsetX"), 0.f);
        tr->y = wy + toFloat(def.getVisual3d("offsetY"), 0.f);
        tr->z = wz + toFloat(def.getVisual3d("offsetZ"), 0.f);
        const float height = toFloat(visualValue(def, pb, *world, true, "height"), 1.f);
        const bool edge = pb.placementKind == "edge";
        const bool corner = pb.placementKind == "corner";
        const bool free = pb.placementKind == "free";
        tr->sx = (free ? freeVisualSize(def, pb, *world, true, "width")
                       : corner ? cornerVisualSize(def, pb, *world, true, "width")
                         : edge ? (pb.edge.axis == building::EdgeAxis::Horizontal ? cellW : cellH)
                                : float(effW) * cellW) *
                 (topology.mirrorX ? -1.f : 1.f);
        tr->sy = height;
        tr->sz = (free ? freeVisualSize(def, pb, *world, true, "depth")
                       : corner ? cornerVisualSize(def, pb, *world, true, "depth")
                         : edge ? toFloat(visualValue(def, pb, *world, true, "thickness"),
                                          std::min(cellW, cellH) * 0.1f)
                                : float(effH) * cellH) *
                 (topology.mirrorZ ? -1.f : 1.f);
        const float visualRotation = pb.rotationDeg + topology.rotationDegrees;
        const float rad = visualRotation * 3.14159265f / 180.f;
        if (world->getGrid().plane == grid::GridPlane::XZ) {
            tr->yaw = rad;
        } else {
            tr->roll = rad;
        }
        applySurfaceRotation(pb, visualRotation, *tr);
        auto mr = r->meshRenderer();
        v.resourceId = visualValue(def, pb, *world, true, "mesh");
        graphics::Mesh *resolved = nullptr;
        if (v.resourceId.empty()) {
            v.fallbackReason = "primitive_default";
        } else if (!meshResolver_) {
            v.fallbackReason = "resolver_unavailable";
        } else {
            resolved = meshResolver_(v.resourceId);
            v.fallbackReason = resolved ? std::string{} : "resource_unresolved";
        }
        mr->mesh = resolved ? resolved : cubeMesh(gfxOrNull());
        mr->r = toFloat(visualValue(def, pb, *world, true, "colorR"), 0.62f);
        mr->g = toFloat(visualValue(def, pb, *world, true, "colorG"), 0.62f);
        mr->b = toFloat(visualValue(def, pb, *world, true, "colorB"), 0.62f);
        mr->a = alpha;
        mr->visible = true;
        mr->castShadow = true;
        mr->receiveShadow = true;
        v.r3d = r;
    } else {
        auto *r = graphics::Renderable2D::create();
        auto tr = r->transform();
        tr->x = pb.worldX + toFloat(def.getVisual2d("offsetX"), 0.f);
        tr->y = pb.worldY + toFloat(def.getVisual2d("offsetY"), 0.f);
        tr->rot = pb.rotationDeg + topology.rotationDegrees;
        tr->sx = topology.mirrorX ? -1.f : 1.f;
        tr->sy = topology.mirrorZ ? -1.f : 1.f;
        auto sp = r->sprite();
        const bool edge = pb.placementKind == "edge";
        const bool corner = pb.placementKind == "corner";
        const bool free = pb.placementKind == "free";
        sp->width = free ? freeVisualSize(def, pb, *world, false, "width")
                         : corner ? cornerVisualSize(def, pb, *world, false, "width")
                           : edge ? (pb.edge.axis == building::EdgeAxis::Horizontal ? cellW : cellH)
                                  : float(effW) * cellW;
        sp->height = free ? freeVisualSize(def, pb, *world, false, "height")
                          : corner ? cornerVisualSize(def, pb, *world, false, "height")
                            : edge ? toFloat(visualValue(def, pb, *world, false, "thickness"),
                                             std::min(cellW, cellH) * 0.1f)
                                   : float(effH) * cellH;
        sp->r = toFloat(visualValue(def, pb, *world, false, "colorR"), 0.72f);
        sp->g = toFloat(visualValue(def, pb, *world, false, "colorG"), 0.72f);
        sp->b = toFloat(visualValue(def, pb, *world, false, "colorB"), 0.72f);
        sp->a = alpha;
        sp->layer = int(toFloat(def.getVisual2d("layer"), 0.f));
        sp->visible = true;
        const std::string texPath = visualValue(def, pb, *world, false, "texture");
        v.resourceId = texPath;
        v.fallbackReason = texPath.empty() ? "primitive_default" : std::string{};
        if (!texPath.empty()) {
            if (auto *gfx = gfxOrNull()) {
                if (graphics::Texture *tex = gfx->newTextureFromFile(texPath)) {
                    sp->texture = tex;
                } else {
                    v.fallbackReason = "resource_unresolved";
                }
            } else {
                v.fallbackReason = "resolver_unavailable";
            }
        }
        v.r2d = r;
    }
}

void BuildingFx::updateVisual(const building::BuildingDefinition &def,
                              const building::PlacedBuilding &pb,
                              building::PlacementWorld *world, Visual &v) {
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    int effW = def.footprintW;
    int effH = def.footprintH;
    building::PlacementSystem::effectiveFootprint(def, pb.rotationDeg, &effW, &effH);
    const TopologyTransform topology = topologyTransform(def, pb, *world, is3d(def));
    v.topologyVariant = topology.variant;
    v.topologyMask = topology.mask;

    if (v.r3d) {
        float wx = 0.f, wy = 0.f, wz = 0.f;
        placedWorldPosition(pb, *world, wx, wy, wz);
        auto tr = v.r3d->transform();
        tr->x = wx + toFloat(def.getVisual3d("offsetX"), 0.f);
        tr->y = wy + toFloat(def.getVisual3d("offsetY"), 0.f);
        tr->z = wz + toFloat(def.getVisual3d("offsetZ"), 0.f);
        const float height = toFloat(visualValue(def, pb, *world, true, "height"), 1.f);
        const bool edge = pb.placementKind == "edge";
        const bool corner = pb.placementKind == "corner";
        const bool free = pb.placementKind == "free";
        tr->sx = (free ? freeVisualSize(def, pb, *world, true, "width")
                       : corner ? cornerVisualSize(def, pb, *world, true, "width")
                         : edge ? (pb.edge.axis == building::EdgeAxis::Horizontal ? cellW : cellH)
                                : float(effW) * cellW) *
                 (topology.mirrorX ? -1.f : 1.f);
        tr->sy = height;
        tr->sz = (free ? freeVisualSize(def, pb, *world, true, "depth")
                       : corner ? cornerVisualSize(def, pb, *world, true, "depth")
                         : edge ? toFloat(visualValue(def, pb, *world, true, "thickness"),
                                          std::min(cellW, cellH) * 0.1f)
                                : float(effH) * cellH) *
                 (topology.mirrorZ ? -1.f : 1.f);
        const float visualRotation = pb.rotationDeg + topology.rotationDegrees;
        const float rad = visualRotation * 3.14159265f / 180.f;
        if (world->getGrid().plane == grid::GridPlane::XZ) {
            tr->yaw = rad;
        } else {
            tr->roll = rad;
        }
        applySurfaceRotation(pb, visualRotation, *tr);
        auto renderer = v.r3d->meshRenderer();
        v.resourceId = visualValue(def, pb, *world, true, "mesh");
        graphics::Mesh *resolved = nullptr;
        if (v.resourceId.empty()) {
            v.fallbackReason = "primitive_default";
        } else if (!meshResolver_) {
            v.fallbackReason = "resolver_unavailable";
        } else {
            resolved = meshResolver_(v.resourceId);
            v.fallbackReason = resolved ? std::string{} : "resource_unresolved";
        }
        renderer->mesh = resolved ? resolved : cubeMesh(gfxOrNull());
        renderer->r = toFloat(visualValue(def, pb, *world, true, "colorR"), 0.62f);
        renderer->g = toFloat(visualValue(def, pb, *world, true, "colorG"), 0.62f);
        renderer->b = toFloat(visualValue(def, pb, *world, true, "colorB"), 0.62f);
    } else if (v.r2d) {
        auto tr = v.r2d->transform();
        tr->x = pb.worldX + toFloat(def.getVisual2d("offsetX"), 0.f);
        tr->y = pb.worldY + toFloat(def.getVisual2d("offsetY"), 0.f);
        tr->rot = pb.rotationDeg + topology.rotationDegrees;
        tr->sx = topology.mirrorX ? -1.f : 1.f;
        tr->sy = topology.mirrorZ ? -1.f : 1.f;
        auto sp = v.r2d->sprite();
        const bool edge = pb.placementKind == "edge";
        const bool corner = pb.placementKind == "corner";
        const bool free = pb.placementKind == "free";
        sp->width = free ? freeVisualSize(def, pb, *world, false, "width")
                         : corner ? cornerVisualSize(def, pb, *world, false, "width")
                           : edge ? (pb.edge.axis == building::EdgeAxis::Horizontal ? cellW : cellH)
                                  : float(effW) * cellW;
        sp->height = free ? freeVisualSize(def, pb, *world, false, "height")
                          : corner ? cornerVisualSize(def, pb, *world, false, "height")
                            : edge ? toFloat(visualValue(def, pb, *world, false, "thickness"),
                                             std::min(cellW, cellH) * 0.1f)
                                   : float(effH) * cellH;
        sp->r = toFloat(visualValue(def, pb, *world, false, "colorR"), 0.72f);
        sp->g = toFloat(visualValue(def, pb, *world, false, "colorG"), 0.72f);
        sp->b = toFloat(visualValue(def, pb, *world, false, "colorB"), 0.72f);
        const std::string resourceId = visualValue(def, pb, *world, false, "texture");
        if (resourceId != v.resourceId) {
            sp->texture = nullptr;
            v.resourceId = resourceId;
            if (resourceId.empty()) {
                v.fallbackReason = "primitive_default";
            } else if (auto *gfx = gfxOrNull()) {
                sp->texture = gfx->newTextureFromFile(resourceId);
                v.fallbackReason = sp->texture ? std::string{} : "resource_unresolved";
            } else {
                v.fallbackReason = "resolver_unavailable";
            }
        }
    }
}

void BuildingFx::destroyVisual(Visual &v) {
    if (v.r2d) {
        ecs::DestroyEntity(v.r2d);
        v.r2d = nullptr;
    }
    if (v.r3d) {
        ecs::DestroyEntity(v.r3d);
        v.r3d = nullptr;
    }
}

void BuildingFx::setVisible(Visual &v, bool visible) {
    if (v.r2d) v.r2d->sprite()->visible = visible;
    if (v.r3d) v.r3d->meshRenderer()->visible = visible;
}

void BuildingFx::destroyAll(WorldState &st) {
    for (auto &kv : st.visuals) destroyVisual(kv.second);
    st.visuals.clear();
    for (auto &[groupId, curve] : st.curveVisuals) {
        (void)groupId;
        if (curve.renderable) ecs::DestroyEntity(curve.renderable);
    }
    st.curveVisuals.clear();
    if (st.curvePreview.renderable) ecs::DestroyEntity(st.curvePreview.renderable);
    st.curvePreview = {};
    destroyVisual(st.ghost);
    destroyVisual(st.cursor);
    st.ghostBuildingId.clear();
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) ecs::DestroyEntity(line);
    }
    st.gridLines.clear();
    st.gridLineCount = -1;
    destroyHeatmap(st);
}

void BuildingFx::destroyHeatmap(WorldState &st) {
    for (graphics::Renderable3D *cell : st.heatCells3d)
        if (cell) ecs::DestroyEntity(cell);
    st.heatCells3d.clear();
}

void BuildingFx::sync(building::PlacementWorld *world) {
    if (!world) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;

    std::unordered_map<int, Visual> next;
    const int n = world->getBuildingCount();
    for (int i = 0; i < n; ++i) {
        const int inst = world->getBuildingInstanceAt(i);
        auto bit = world->buildings().find(inst);
        if (bit == world->buildings().end()) continue;
        const auto &pb = bit->second;
        const building::BuildingDefinition *def =
            building::BuildingRegistry::find(pb.buildingId);
        if (!def) continue;
        auto existing = st.visuals.find(inst);
        if (existing != st.visuals.end()) {
            next[inst] = existing->second;
            updateVisual(*def, pb, world, next[inst]);
        } else {
            Visual v;
            createVisual(st, *def, pb, world, v, 1.f);
            next[inst] = v;
        }
        const bool visible =
            st.levelVisibility == WorldState::LevelVisibility::All ||
            (st.levelVisibility == WorldState::LevelVisibility::Active &&
             pb.level == world->getActiveLevel()) ||
            (st.levelVisibility == WorldState::LevelVisibility::ActiveAndBelow &&
             pb.level <= world->getActiveLevel());
        setVisible(next[inst], visible);
    }
    for (auto &kv : st.visuals) {
        if (next.count(kv.first) == 0) destroyVisual(kv.second);
    }
    st.visuals = std::move(next);
    syncCurveVisuals(st, world);
}

void BuildingFx::syncCurveVisuals(WorldState &st, building::PlacementWorld *world) {
    for (auto &[groupId, curve] : st.curveVisuals) {
        (void)groupId;
        curve.active = false;
    }
    for (building::EdgeCurveGroupId groupId : world->edgeCurveGroupIds()) {
        auto groupResult = world->edgeCurveGroup(groupId);
        if (!groupResult.ok() || groupResult.value().instanceIds.empty()) continue;
        const building::EdgeCurveGroup &group = groupResult.value();
        const auto member = world->buildings().find(group.instanceIds.front());
        if (member == world->buildings().end()) continue;
        const building::PlacedBuilding &placed = member->second;
        const building::BuildingDefinition *definition =
            building::BuildingRegistry::find(group.buildingId);
        if (!definition) continue;

        WorldState::CurveVisual &curve = st.curveVisuals[groupId.value];
        curve.active = true;
        curve.memberIds = group.instanceIds;
        std::vector<CurveControlPoint> controls;
        for (const building::EdgeCurveControlPoint &point : group.controlPoints)
            controls.push_back({point.x, point.y});
        const float width = toFloat(definition->getVisual3d("thickness"),
                                    std::min(world->getGrid().cellW,
                                             world->getGrid().cellH) *
                                        0.1f);
        const float height = toFloat(definition->getVisual3d("height"), 1.f);
        const float elevation = placed.elevation +
                                static_cast<float>(placed.level) * world->getFloorHeight();
        const bool changed = curve.controlPoints != controls ||
                             curve.subdivisions != group.subdivisions || curve.width != width ||
                             curve.height != height || curve.elevation != elevation ||
                             curve.surfaceId != group.surfaceId ||
                             curve.surfaceRevision != group.surfaceRevision;
        curve.fallbackReason.clear();
        if (!is3d(*definition)) {
            curve.fallbackReason = "render_mode_2d";
        } else if (changed || curve.cpuMesh.positions.empty()) {
            eve::Result<CurveMeshData> generated = [&]() {
                if (group.surfaceSamples.empty())
                    return buildEdgeCurveMesh(*world, controls, group.subdivisions, width,
                                              height, elevation);
                std::vector<CurveSurfaceSample> samples;
                for (const building::EdgeCurveSurfaceSample &sample : group.surfaceSamples)
                    samples.push_back({sample.worldX, sample.worldY, sample.worldZ,
                                       sample.normalX, sample.normalY, sample.normalZ});
                return buildSurfaceCurveMesh(samples, width, height);
            }();
            if (!generated.ok()) {
                curve.fallbackReason = generated.error() ? generated.error()->message()
                                                         : "mesh_generation_failed";
            } else {
                curve.cpuMesh = std::move(generated).takeValue();
                curve.controlPoints = std::move(controls);
                curve.subdivisions = group.subdivisions;
                curve.width = width;
                curve.height = height;
                curve.elevation = elevation;
                curve.surfaceId = group.surfaceId;
                curve.surfaceRevision = group.surfaceRevision;
            }
        }
        graphics::Graphics *gfx = gfxOrNull();
        if (curve.fallbackReason.empty() && !gfx) curve.fallbackReason = "graphics_unavailable";
        if (curve.fallbackReason.empty() && changed && curve.mesh) {
            if (!gfx->updateMeshVertices(
                    curve.mesh, curve.cpuMesh.positions.data(), curve.cpuMesh.normals.data(),
                    curve.cpuMesh.uvs.data(), static_cast<int>(curve.cpuMesh.positions.size() / 3),
                    curve.cpuMesh.indices.data(), static_cast<int>(curve.cpuMesh.indices.size())))
                curve.mesh = nullptr;
        }
        if (curve.fallbackReason.empty() && !curve.mesh) {
            try {
                curve.mesh = gfx->newMeshFromArrays(
                    curve.cpuMesh.positions.data(), curve.cpuMesh.normals.data(),
                    curve.cpuMesh.uvs.data(), static_cast<int>(curve.cpuMesh.positions.size() / 3),
                    curve.cpuMesh.indices.data(), static_cast<int>(curve.cpuMesh.indices.size()));
            } catch (...) {
                curve.mesh = nullptr;
            }
            if (!curve.mesh) curve.fallbackReason = "mesh_upload_failed";
        }
        if (curve.fallbackReason.empty() && !curve.renderable) {
            curve.renderable = graphics::Renderable3D::create();
            curve.renderable->meshRenderer()->mesh = curve.mesh;
        }
        if (curve.renderable) {
            auto renderer = curve.renderable->meshRenderer();
            renderer->mesh = curve.mesh;
            renderer->r = toFloat(definition->getVisual3d("colorR"), 0.62f);
            renderer->g = toFloat(definition->getVisual3d("colorG"), 0.62f);
            renderer->b = toFloat(definition->getVisual3d("colorB"), 0.62f);
            renderer->a = 1.f;
            renderer->castShadow = true;
            renderer->receiveShadow = true;
            renderer->visible =
                curve.fallbackReason.empty() &&
                (st.levelVisibility == WorldState::LevelVisibility::All ||
                 (st.levelVisibility == WorldState::LevelVisibility::Active &&
                  placed.level == world->getActiveLevel()) ||
                 (st.levelVisibility == WorldState::LevelVisibility::ActiveAndBelow &&
                  placed.level <= world->getActiveLevel()));
        }
        if (curve.fallbackReason.empty()) {
            for (int instanceId : group.instanceIds) {
                const auto visual = st.visuals.find(instanceId);
                if (visual != st.visuals.end()) setVisible(visual->second, false);
            }
        }
    }
    for (auto &[groupId, curve] : st.curveVisuals) {
        (void)groupId;
        if (curve.active) continue;
        if (curve.renderable) {
            ecs::DestroyEntity(curve.renderable);
            curve.renderable = nullptr;
        }
    }
}

int BuildingFx::getVisualCount(building::PlacementWorld *world) const {
    auto it = states_.find(world);
    return it == states_.end() ? 0 : int(it->second.visuals.size());
}

int BuildingFx::getCurveGroupCount(building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return 0;
    return static_cast<int>(std::count_if(
        state->second.curveVisuals.begin(), state->second.curveVisuals.end(),
        [](const auto &entry) { return entry.second.active; }));
}

int BuildingFx::getContinuousCurveVisualCount(building::PlacementWorld *world) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return 0;
    return static_cast<int>(std::count_if(
        state->second.curveVisuals.begin(), state->second.curveVisuals.end(),
        [](const auto &entry) {
            return entry.second.active && entry.second.renderable &&
                   entry.second.fallbackReason.empty();
        }));
}

std::string BuildingFx::getCurveVisualFallbackReason(building::PlacementWorld *world,
                                                     int instanceId) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return "world_not_attached";
    for (const auto &[groupId, curve] : state->second.curveVisuals) {
        (void)groupId;
        if (curve.active &&
            std::find(curve.memberIds.begin(), curve.memberIds.end(), instanceId) !=
                curve.memberIds.end())
            return curve.fallbackReason;
    }
    return "curve_group_not_found";
}

void BuildingFx::setLevelVisibilityMode(building::PlacementWorld *world,
                                        const std::string &mode) {
    if (!world) return;
    auto found = states_.find(world);
    if (found == states_.end()) return;
    if (mode == "all") {
        found->second.levelVisibility = WorldState::LevelVisibility::All;
    } else if (mode == "active") {
        found->second.levelVisibility = WorldState::LevelVisibility::Active;
    } else if (mode == "active_and_below") {
        found->second.levelVisibility = WorldState::LevelVisibility::ActiveAndBelow;
    } else {
        return;
    }
    sync(world);
}

std::string BuildingFx::getLevelVisibilityMode(building::PlacementWorld *world) const {
    const auto found = states_.find(world);
    if (found == states_.end()) return {};
    switch (found->second.levelVisibility) {
        case WorldState::LevelVisibility::Active: return "active";
        case WorldState::LevelVisibility::ActiveAndBelow: return "active_and_below";
        default: return "all";
    }
}

bool BuildingFx::isVisualVisible(building::PlacementWorld *world, int instanceId) const {
    const auto found = states_.find(world);
    if (found == states_.end()) return false;
    const auto visual = found->second.visuals.find(instanceId);
    if (visual == found->second.visuals.end()) return false;
    if (visual->second.r2d) return visual->second.r2d->sprite()->visible;
    return visual->second.r3d && visual->second.r3d->meshRenderer()->visible;
}

std::string BuildingFx::getVisualVariant(building::PlacementWorld *world, int instanceId) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return {};
    const auto visual = state->second.visuals.find(instanceId);
    return visual == state->second.visuals.end() ? std::string{}
                                                 : visual->second.topologyVariant;
}

std::string BuildingFx::getVisualResource(building::PlacementWorld *world, int instanceId) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return {};
    const auto visual = state->second.visuals.find(instanceId);
    return visual == state->second.visuals.end() ? std::string{} : visual->second.resourceId;
}

std::string BuildingFx::getVisualFallbackReason(building::PlacementWorld *world,
                                                int instanceId) const {
    const auto state = states_.find(world);
    if (state == states_.end()) return {};
    const auto visual = state->second.visuals.find(instanceId);
    return visual == state->second.visuals.end() ? std::string{}
                                                 : visual->second.fallbackReason;
}

void BuildingFx::updateGhost(building::PlacementWorld *world, building::Ghost *ghost) {
    if (!world || !ghost) {
        hideGhost(world);
        return;
    }
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    const building::BuildingDefinition *def =
        building::BuildingRegistry::find(ghost->getBuildingId());
    if (!def) return;

    if (st.ghostBuildingId != ghost->getBuildingId()) {
        destroyVisual(st.ghost);
        destroyVisual(st.cursor);
        st.ghostBuildingId = ghost->getBuildingId();
    }
    if (!st.ghost.r2d && !st.ghost.r3d) {
        // 用 ghost 当前位姿构造临时 PlacedBuilding 以复用 createVisual。
        building::PlacedBuilding pb;
        pb.buildingId = ghost->getBuildingId();
        pb.placementKind = ghost->getPlacementKind();
        pb.originCellX = ghost->getCellX();
        pb.originCellY = ghost->getCellY();
        pb.corner = {ghost->getCellX(), ghost->getCellY()};
        pb.worldX = ghost->getWorldX();
        pb.worldY = ghost->getWorldY();
        pb.elevation = ghost->getElevation();
        pb.rotationDeg = ghost->getRotationDeg();
        pb.surfaceId = ghost->getSurfaceId();
        pb.surfaceNormalX = ghost->getSurfaceNormalX();
        pb.surfaceNormalY = ghost->getSurfaceNormalY();
        pb.surfaceNormalZ = ghost->getSurfaceNormalZ();
        pb.surfaceTangentX = ghost->getSurfaceTangentX();
        pb.surfaceTangentY = ghost->getSurfaceTangentY();
        pb.surfaceTangentZ = ghost->getSurfaceTangentZ();
        createVisual(st, *def, pb, world, st.ghost, 0.5f);
    }
    {
        building::PlacedBuilding pb;
        pb.buildingId = ghost->getBuildingId();
        pb.placementKind = ghost->getPlacementKind();
        pb.originCellX = ghost->getCellX();
        pb.originCellY = ghost->getCellY();
        pb.corner = {ghost->getCellX(), ghost->getCellY()};
        pb.worldX = ghost->getWorldX();
        pb.worldY = ghost->getWorldY();
        pb.elevation = ghost->getElevation();
        pb.rotationDeg = ghost->getRotationDeg();
        pb.surfaceId = ghost->getSurfaceId();
        pb.surfaceNormalX = ghost->getSurfaceNormalX();
        pb.surfaceNormalY = ghost->getSurfaceNormalY();
        pb.surfaceNormalZ = ghost->getSurfaceNormalZ();
        pb.surfaceTangentX = ghost->getSurfaceTangentX();
        pb.surfaceTangentY = ghost->getSurfaceTangentY();
        pb.surfaceTangentZ = ghost->getSurfaceTangentZ();
        updateVisual(*def, pb, world, st.ghost);
    }

    const bool valid = ghost->isValid();
    const float cr = valid ? 0.25f : 0.90f;
    const float cg = valid ? 0.85f : 0.25f;
    const float cb = valid ? 0.35f : 0.22f;
    if (st.ghost.r2d) {
        auto sp = st.ghost.r2d->sprite();
        sp->r = cr;
        sp->g = cg;
        sp->b = cb;
    }
    if (st.ghost.r3d) {
        auto mr = st.ghost.r3d->meshRenderer();
        mr->r = cr;
        mr->g = cg;
        mr->b = cb;
    }
    setVisible(st.ghost, true);

    // 占地光标。
    if (!st.cursor.r2d && !st.cursor.r3d) {
        const float cellW = world->getGrid().cellW;
        const float cellH = world->getGrid().cellH;
        int effW = def->footprintW;
        int effH = def->footprintH;
        building::PlacementSystem::effectiveFootprint(*def, ghost->getRotationDeg(), &effW, &effH);
        if (is3d(*def)) {
            auto *r = graphics::Renderable3D::create();
            auto tr = r->transform();
            building::PlacedBuilding placed;
            placed.worldX = ghost->getWorldX();
            placed.worldY = ghost->getWorldY();
            placed.placementKind = ghost->getPlacementKind();
            placed.elevation = ghost->getElevation();
            placed.rotationDeg = ghost->getRotationDeg();
            placed.surfaceId = ghost->getSurfaceId();
            placed.surfaceNormalX = ghost->getSurfaceNormalX();
            placed.surfaceNormalY = ghost->getSurfaceNormalY();
            placed.surfaceNormalZ = ghost->getSurfaceNormalZ();
            placed.surfaceTangentX = ghost->getSurfaceTangentX();
            placed.surfaceTangentY = ghost->getSurfaceTangentY();
            placed.surfaceTangentZ = ghost->getSurfaceTangentZ();
            placedWorldPosition(placed, *world, tr->x, tr->y, tr->z);
            tr->x += placed.surfaceNormalX * 0.03f;
            tr->y += placed.surfaceNormalY * 0.03f;
            tr->z += placed.surfaceNormalZ * 0.03f;
            applySurfaceRotation(placed, placed.rotationDeg, *tr);
            tr->sx = placed.placementKind == "free"
                         ? freeVisualSize(*def, placed, *world, true, "width")
                     : placed.placementKind == "corner"
                         ? cornerVisualSize(*def, placed, *world, true, "width")
                         : float(effW) * cellW;
            tr->sy = 0.04f;
            tr->sz = placed.placementKind == "free"
                         ? freeVisualSize(*def, placed, *world, true, "depth")
                     : placed.placementKind == "corner"
                         ? cornerVisualSize(*def, placed, *world, true, "depth")
                         : float(effH) * cellH;
            auto mr = r->meshRenderer();
            mr->mesh = cubeMesh(gfxOrNull());
            mr->r = cr;
            mr->g = cg;
            mr->b = cb;
            mr->a = 0.35f;
            st.cursor.r3d = r;
        } else {
            auto *r = graphics::Renderable2D::create();
            auto tr = r->transform();
            tr->x = ghost->getWorldX();
            tr->y = ghost->getWorldY();
            auto sp = r->sprite();
            building::PlacedBuilding placed;
            placed.placementKind = ghost->getPlacementKind();
            sp->width = placed.placementKind == "free"
                            ? freeVisualSize(*def, placed, *world, false, "width")
                        : placed.placementKind == "corner"
                            ? cornerVisualSize(*def, placed, *world, false, "width")
                            : float(effW) * cellW;
            sp->height = placed.placementKind == "free"
                             ? freeVisualSize(*def, placed, *world, false, "height")
                         : placed.placementKind == "corner"
                             ? cornerVisualSize(*def, placed, *world, false, "height")
                             : float(effH) * cellH;
            sp->r = cr;
            sp->g = cg;
            sp->b = cb;
            sp->a = 0.35f;
            st.cursor.r2d = r;
        }
    } else {
        const float cellW = world->getGrid().cellW;
        const float cellH = world->getGrid().cellH;
        int effW = def->footprintW;
        int effH = def->footprintH;
        building::PlacementSystem::effectiveFootprint(*def, ghost->getRotationDeg(), &effW, &effH);
        if (st.cursor.r3d) {
            auto tr = st.cursor.r3d->transform();
            building::PlacedBuilding placed;
            placed.worldX = ghost->getWorldX();
            placed.worldY = ghost->getWorldY();
            placed.placementKind = ghost->getPlacementKind();
            placed.elevation = ghost->getElevation();
            placed.rotationDeg = ghost->getRotationDeg();
            placed.surfaceId = ghost->getSurfaceId();
            placed.surfaceNormalX = ghost->getSurfaceNormalX();
            placed.surfaceNormalY = ghost->getSurfaceNormalY();
            placed.surfaceNormalZ = ghost->getSurfaceNormalZ();
            placed.surfaceTangentX = ghost->getSurfaceTangentX();
            placed.surfaceTangentY = ghost->getSurfaceTangentY();
            placed.surfaceTangentZ = ghost->getSurfaceTangentZ();
            placedWorldPosition(placed, *world, tr->x, tr->y, tr->z);
            tr->x += placed.surfaceNormalX * 0.03f;
            tr->y += placed.surfaceNormalY * 0.03f;
            tr->z += placed.surfaceNormalZ * 0.03f;
            applySurfaceRotation(placed, placed.rotationDeg, *tr);
            tr->sx = placed.placementKind == "free"
                         ? freeVisualSize(*def, placed, *world, true, "width")
                     : placed.placementKind == "corner"
                         ? cornerVisualSize(*def, placed, *world, true, "width")
                         : float(effW) * cellW;
            tr->sz = placed.placementKind == "free"
                         ? freeVisualSize(*def, placed, *world, true, "depth")
                     : placed.placementKind == "corner"
                         ? cornerVisualSize(*def, placed, *world, true, "depth")
                         : float(effH) * cellH;
            auto mr = st.cursor.r3d->meshRenderer();
            mr->r = cr;
            mr->g = cg;
            mr->b = cb;
        } else if (st.cursor.r2d) {
            auto tr = st.cursor.r2d->transform();
            tr->x = ghost->getWorldX();
            tr->y = ghost->getWorldY();
            auto sp = st.cursor.r2d->sprite();
            building::PlacedBuilding placed;
            placed.placementKind = ghost->getPlacementKind();
            sp->width = placed.placementKind == "free"
                            ? freeVisualSize(*def, placed, *world, false, "width")
                        : placed.placementKind == "corner"
                            ? cornerVisualSize(*def, placed, *world, false, "width")
                            : float(effW) * cellW;
            sp->height = placed.placementKind == "free"
                             ? freeVisualSize(*def, placed, *world, false, "height")
                         : placed.placementKind == "corner"
                             ? cornerVisualSize(*def, placed, *world, false, "height")
                             : float(effH) * cellH;
            sp->r = cr;
            sp->g = cg;
            sp->b = cb;
        }
    }
    setVisible(st.cursor, true);
}

void BuildingFx::hideGhost(building::PlacementWorld *world) {
    if (!world) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    setVisible(st.ghost, false);
    setVisible(st.cursor, false);
}

void BuildingFx::updateAreaPreview(building::PlacementWorld *world,
                                   building::PlacementSession *session) {
    if (!world) return;
    auto stateIt = states_.find(world);
    if (stateIt == states_.end()) return;
    WorldState &st = stateIt->second;
    destroyHeatmap(st);
    st.heatCells.clear();
    if (!session) return;
    const int count = session->getAreaPreviewCount();
    st.heatCells.reserve(size_t(count));
    for (int i = 0; i < count; ++i)
        st.heatCells.push_back({session->getAreaPreviewCellX(i), session->getAreaPreviewCellY(i),
                                session->getAreaPreviewAccepted(i)});
}

void BuildingFx::clearAreaPreview(building::PlacementWorld *world) {
    auto it = states_.find(world);
    if (it == states_.end()) return;
    destroyHeatmap(it->second);
    it->second.heatCells.clear();
}

int BuildingFx::getAreaPreviewCount(building::PlacementWorld *world) const {
    auto it = states_.find(world);
    return it == states_.end() ? 0 : int(it->second.heatCells.size());
}

bool BuildingFx::getAreaPreviewAccepted(building::PlacementWorld *world, int index) const {
    auto it = states_.find(world);
    return it != states_.end() && index >= 0 && index < int(it->second.heatCells.size()) &&
           it->second.heatCells[size_t(index)].accepted;
}

void BuildingFx::setGridVisible(building::PlacementWorld *world, bool visible) {
    if (!world) return;
    states_[world].gridVisible = visible;
}

bool BuildingFx::getGridVisible(building::PlacementWorld *world) const {
    auto it = states_.find(world);
    return it != states_.end() && it->second.gridVisible;
}

void BuildingFx::drawGrid2D(building::PlacementWorld *world, graphics::Graphics *gfx) {
    if (!world || !gfx) return;
    auto it = states_.find(world);
    if (it == states_.end() || !it->second.gridVisible) return;
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    for (int y = 0; y < world->getHeight(); ++y) {
        for (int x = 0; x < world->getWidth(); ++x) {
            float px = 0.f, py = 0.f;
            world->cellToWorldPlane(x, y, px, py);
            gfx->drawSolidRect(px, py, cellW - 1.f, cellH - 1.f,
                               Color{0.75f, 0.78f, 0.85f, 0.06f});
        }
    }
    for (const WorldState::HeatCell &cell : it->second.heatCells) {
        float px = 0.f, py = 0.f;
        world->cellToWorldPlane(cell.x, cell.y, px, py);
        gfx->drawSolidRect(px, py, cellW - 1.f, cellH - 1.f,
                           cell.accepted ? Color{0.18f, 0.9f, 0.3f, 0.28f}
                                         : Color{0.95f, 0.16f, 0.12f, 0.34f});
    }
}

void BuildingFx::rebuildGridLines(WorldState &st, building::PlacementWorld *world,
                                  graphics::Graphics *gfx, float height) {
    const int w = world->getWidth();
    const int h = world->getHeight();
    const float cellW = world->getGrid().cellW;
    const float cellH = world->getGrid().cellH;
    const int want = (w + 1) + (h + 1);
    if (int(st.gridLines.size()) == want && st.gridLineCount == want) return;
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) ecs::DestroyEntity(line);
    }
    st.gridLines.clear();
    graphics::Mesh *mesh = cubeMesh(gfx);
    const bool xz = world->getGrid().plane == grid::GridPlane::XZ;
    const float xExtent = float(w) * cellW;
    const float zExtent = float(h) * cellH;
    for (int x = 0; x <= w; ++x) {
        auto *r = graphics::Renderable3D::create();
        auto tr = r->transform();
        float px = 0.f, py = 0.f;
        world->cellToWorldPlane(x, 0, px, py);
        tr->x = px;
        tr->y = height;
        tr->z = xz ? 0.0f : 0.f;
        tr->sx = 0.03f;
        tr->sy = 0.02f;
        tr->sz = zExtent;
        auto mr = r->meshRenderer();
        mr->mesh = mesh;
        mr->r = 0.85f;
        mr->g = 0.88f;
        mr->b = 0.95f;
        mr->a = 0.5f;
        st.gridLines.push_back(r);
    }
    for (int y = 0; y <= h; ++y) {
        auto *r = graphics::Renderable3D::create();
        auto tr = r->transform();
        float px = 0.f, py = 0.f;
        world->cellToWorldPlane(0, y, px, py);
        tr->x = xz ? 0.0f : 0.f;
        tr->y = height;
        tr->z = py;
        tr->sx = xExtent;
        tr->sy = 0.02f;
        tr->sz = 0.03f;
        auto mr = r->meshRenderer();
        mr->mesh = mesh;
        mr->r = 0.85f;
        mr->g = 0.88f;
        mr->b = 0.95f;
        mr->a = 0.5f;
        st.gridLines.push_back(r);
    }
    st.gridLineCount = want;
}

void BuildingFx::drawGrid3D(building::PlacementWorld *world, graphics::Graphics *gfx,
                            float height) {
    if (!world || !gfx) return;
    auto it = states_.find(world);
    if (it == states_.end()) return;
    WorldState &st = it->second;
    rebuildGridLines(st, world, gfx, height);
    for (graphics::Renderable3D *line : st.gridLines) {
        if (line) line->meshRenderer()->visible = st.gridVisible;
    }
    if (st.heatCells3d.size() != st.heatCells.size()) {
        destroyHeatmap(st);
        for (const WorldState::HeatCell &cell : st.heatCells) {
            auto *r = graphics::Renderable3D::create();
            float wx = 0.f, wy = 0.f, wz = 0.f;
            world->cellToWorld3D(cell.x, cell.y, height + 0.015f, wx, wy, wz);
            auto tr = r->transform();
            tr->x = wx;
            tr->y = wy;
            tr->z = wz;
            tr->sx = world->getGrid().cellW * 0.92f;
            if (world->getGrid().plane == grid::GridPlane::XZ) {
                tr->sy = 0.02f;
                tr->sz = world->getGrid().cellH * 0.92f;
            } else {
                tr->sy = world->getGrid().cellH * 0.92f;
                tr->sz = 0.02f;
            }
            auto mr = r->meshRenderer();
            mr->mesh = cubeMesh(gfx);
            mr->r = cell.accepted ? 0.18f : 0.95f;
            mr->g = cell.accepted ? 0.9f : 0.16f;
            mr->b = cell.accepted ? 0.3f : 0.12f;
            mr->a = 0.35f;
            st.heatCells3d.push_back(r);
        }
    }
    for (graphics::Renderable3D *cell : st.heatCells3d)
        if (cell) cell->meshRenderer()->visible = st.gridVisible;
}

void BuildingFx::expose(ssq::Table &table) {
    auto cls = table.addClass(name, BuildingFx::create, false);
    expose(cls);
}

void BuildingFx::expose(ssq::Class &cls) {
    cls.addFunc("attach", &BuildingFx::attach);
    cls.addFunc("detach", &BuildingFx::detach);
    cls.addFunc("isAttached", &BuildingFx::isAttached);
    cls.addFunc("getAttachedCount", &BuildingFx::getAttachedCount);
    cls.addFunc("sync", &BuildingFx::sync);
    cls.addFunc("getVisualCount", &BuildingFx::getVisualCount);
    cls.addFunc("getVisualVariant", &BuildingFx::getVisualVariant);
    cls.addFunc("getVisualResource", &BuildingFx::getVisualResource);
    cls.addFunc("getVisualFallbackReason", &BuildingFx::getVisualFallbackReason);
    cls.addFunc("getCurveGroupCount", &BuildingFx::getCurveGroupCount);
    cls.addFunc("getContinuousCurveVisualCount", &BuildingFx::getContinuousCurveVisualCount);
    cls.addFunc("getCurveVisualFallbackReason", &BuildingFx::getCurveVisualFallbackReason);
    cls.addFunc("updateEdgeCurveSurfacePreview",
                &BuildingFx::updateEdgeCurveSurfacePreviewStatus);
    cls.addFunc("clearEdgeCurvePreview", &BuildingFx::clearEdgeCurvePreview);
    cls.addFunc("hasEdgeCurvePreview", &BuildingFx::hasEdgeCurvePreview);
    cls.addFunc("getEdgeCurvePreviewFallbackReason",
                &BuildingFx::getEdgeCurvePreviewFallbackReason);
    cls.addFunc("getEdgeCurvePreviewSurfaceId",
                &BuildingFx::getEdgeCurvePreviewSurfaceId);
    cls.addFunc("setLevelVisibilityMode", &BuildingFx::setLevelVisibilityMode);
    cls.addFunc("getLevelVisibilityMode", &BuildingFx::getLevelVisibilityMode);
    cls.addFunc("isVisualVisible", &BuildingFx::isVisualVisible);
    cls.addFunc("updateGhost", &BuildingFx::updateGhost);
    cls.addFunc("hideGhost", &BuildingFx::hideGhost);
    cls.addFunc("updateAreaPreview", &BuildingFx::updateAreaPreview);
    cls.addFunc("clearAreaPreview", &BuildingFx::clearAreaPreview);
    cls.addFunc("getAreaPreviewCount", &BuildingFx::getAreaPreviewCount);
    cls.addFunc("getAreaPreviewAccepted", &BuildingFx::getAreaPreviewAccepted);
    cls.addFunc("setGridVisible", &BuildingFx::setGridVisible);
    cls.addFunc("getGridVisible", &BuildingFx::getGridVisible);
    cls.addFunc("drawGrid2D", &BuildingFx::drawGrid2D);
    cls.addFunc("drawGrid3D", &BuildingFx::drawGrid3D);
}

}  // namespace eve::buildingfx
