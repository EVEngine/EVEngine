#include "asset/procgen/TerrainVegetationRuntime.h"

#include "asset/procgen/EvpackTerrainMaterialLoader.h"
#include "asset/procgen/TerrainMaterialAtlas.h"

#include "procgen/PointGraph.h"
#include "procgen/PointSet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>

namespace eve::asset_procgen {
namespace {

std::array<float, 4> quaternionDegrees(float pitch, float yaw, float roll) {
    constexpr float halfRadians = 0.008726646259971648f;
    const float     px = pitch * halfRadians, yy = yaw * halfRadians, rz = roll * halfRadians;
    const float     sx = std::sin(px), cx = std::cos(px);
    const float     sy = std::sin(yy), cy = std::cos(yy);
    const float     sz = std::sin(rz), cz = std::cos(rz);
    return {sx * cy * cz - cx * sy * sz, cx * sy * cz + sx * cy * sz, cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz};
}

bool finite(const TerrainVegetationInstance& instance) {
    for (float value : instance.position)
        if (!std::isfinite(value)) return false;
    for (float value : instance.rotation)
        if (!std::isfinite(value)) return false;
    for (float value : instance.scale)
        if (!std::isfinite(value) || value == 0.f) return false;
    for (float value : instance.normal)
        if (!std::isfinite(value)) return false;
    return true;
}

void rebuildBuckets(TerrainVegetationRealization& result) {
    result.buckets.clear();
    std::stable_sort(result.instances.begin(), result.instances.end(),
                     [](const auto& left, const auto& right) { return left.prototype < right.prototype; });
    for (std::size_t first = 0; first < result.instances.size();) {
        std::size_t end = first + 1;
        while (end < result.instances.size() && result.instances[end].prototype == result.instances[first].prototype)
            ++end;
        result.buckets.push_back({result.instances[first].prototype, static_cast<std::uint32_t>(first),
                                  static_cast<std::uint32_t>(end - first)});
        first = end;
    }
}

float stableUnit(std::uint64_t value) {
    value ^= value >> 33;
    value *= 0xff51afd7ed558ccdULL;
    value ^= value >> 33;
    value *= 0xc4ceb9fe1a85ec53ULL;
    value ^= value >> 33;
    return float(value >> 40) * (1.f / 16777216.f);
}

float sampleControl(const TerrainAtlasImage& image, std::uint32_t channel, float u, float v) {
    const float x  = std::clamp(u, 0.f, 1.f) * float(image.width - 1);
    const float y  = std::clamp(v, 0.f, 1.f) * float(image.height - 1);
    const auto  x0 = static_cast<std::uint32_t>(std::floor(x));
    const auto  y0 = static_cast<std::uint32_t>(std::floor(y));
    const auto  x1 = std::min(x0 + 1, image.width - 1);
    const auto  y1 = std::min(y0 + 1, image.height - 1);
    const float tx = x - float(x0), ty = y - float(y0);
    const auto  value = [&](std::uint32_t px, std::uint32_t py) {
        return float(image.pixels[(std::size_t(py) * image.width + px) * 4 + channel]) / 255.f;
    };
    const float top    = value(x0, y0) + (value(x1, y0) - value(x0, y0)) * tx;
    const float bottom = value(x0, y1) + (value(x1, y1) - value(x0, y1)) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace

Result<TerrainVegetationRealization> realizeTerrainVegetation(LoadedPointGraph*              graph,
                                                              const LoadedInstanceSet*       explicitInstances,
                                                              const TerrainVegetationLimits& limits) {
    if (!graph && !explicitInstances)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "terrain has no PCG or baked vegetation provider", {}, {},
                              "asset.procgen.terrainVegetation"));
    if (limits.maximumGeneratedInstances == 0 || limits.maximumTotalInstances == 0 || limits.maximumGraphNodes == 0)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain vegetation limits must be non-zero", {}, {},
                              "asset.procgen.terrainVegetation"));

    TerrainVegetationRealization result;
    if (graph) {
        if (!graph->graph || graph->outputNode.empty())
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "loaded terrain PCG graph is incomplete", {}, {},
                                  "asset.procgen.terrainVegetation"));
        graph->graph->setExecutionNodeBudget(static_cast<int>(
            std::min<std::uint32_t>(limits.maximumGraphNodes, std::uint32_t(std::numeric_limits<int>::max()))));
        graph->graph->setMaxNodeOutputPoints(static_cast<int>(
            std::min<std::uint32_t>(limits.maximumGeneratedInstances, std::uint32_t(std::numeric_limits<int>::max()))));
        const auto began  = std::chrono::steady_clock::now();
        auto       points = graph->graph->executeResult(graph->outputNode);
        const auto ended  = std::chrono::steady_clock::now();
        if (!points) return Result<TerrainVegetationRealization>::failure(points.status());
        if (points.value().points().size() > limits.maximumGeneratedInstances)
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "generated terrain vegetation exceeds budget", {},
                                  {}, "asset.procgen.terrainVegetation"));
        result.generatedCount = static_cast<std::uint32_t>(points.value().points().size());
        result.instances.reserve(result.generatedCount + (explicitInstances ? explicitInstances->instances.size() : 0));
        for (std::size_t index = 0; index < points.value().points().size(); ++index) {
            const auto&               point = points.value().points()[index];
            TerrainVegetationInstance instance;
            instance.id        = point.id != 0 ? point.id : procgen::derivePointId(0x7465727261696eULL, index);
            instance.prototype = points.value().getStringAttribute(static_cast<int>(index), "prototype", {});
            instance.layer     = points.value().getStringAttribute(static_cast<int>(index), "layer", {});
            instance.position  = {point.x, point.y, point.z};
            instance.rotation  = quaternionDegrees(point.pitch, point.yaw, point.roll);
            instance.scale     = {point.scaleX, point.scaleY, point.scaleZ};
            instance.normal    = {point.normalX, point.normalY, point.normalZ};
            instance.seed      = point.seed;
            if (instance.prototype.empty() || !finite(instance))
                return Result<TerrainVegetationRealization>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "generated vegetation instance is invalid",
                                      std::to_string(index), {}, "asset.procgen.terrainVegetation"));
            result.instances.push_back(std::move(instance));
        }
        result.graphNodesEvaluated = static_cast<std::uint32_t>(graph->graph->getMetricCount());
        result.graphMilliseconds   = std::chrono::duration<double, std::milli>(ended - began).count();
    }

    if (explicitInstances) {
        result.wavingGrassAmount   = explicitInstances->wavingGrassAmount;
        result.wavingGrassSpeed    = explicitInstances->wavingGrassSpeed;
        result.wavingGrassStrength = explicitInstances->wavingGrassStrength;
        result.wavingGrassTint     = explicitInstances->wavingGrassTint;
        result.prototypes    = explicitInstances->prototypes;
        result.explicitCount = static_cast<std::uint32_t>(explicitInstances->instances.size());
        if (std::uint64_t(result.instances.size()) + explicitInstances->instances.size() > limits.maximumTotalInstances)
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "total terrain vegetation exceeds budget", {}, {},
                                  "asset.procgen.terrainVegetation"));
        result.instances.reserve(result.instances.size() + explicitInstances->instances.size());
        for (std::size_t index = 0; index < explicitInstances->instances.size(); ++index) {
            const auto&               source = explicitInstances->instances[index];
            TerrainVegetationInstance instance;
            instance.id        = procgen::derivePointId(0x6578706c69636974ULL, index);
            instance.prototype = source.prototype;
            instance.position  = source.position;
            instance.rotation  = source.rotation;
            instance.scale     = source.scale;
            if (instance.prototype.empty() || !finite(instance))
                return Result<TerrainVegetationRealization>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "baked vegetation instance is invalid",
                                      std::to_string(index), {}, "asset.procgen.terrainVegetation"));
            result.instances.push_back(std::move(instance));
        }
    }
    if (result.instances.size() > limits.maximumTotalInstances)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "total terrain vegetation exceeds budget", {}, {},
                              "asset.procgen.terrainVegetation"));

    rebuildBuckets(result);
    return Result<TerrainVegetationRealization>::success(std::move(result));
}

Result<TerrainVegetationRealization> filterTerrainVegetationByLayerWeights(
    const TerrainVegetationRealization& realization, const LoadedTerrainMaterial& material,
    const TerrainMaterialAtlases& atlases, const TerrainVegetationWeightDomain& domain) {
    if (!std::isfinite(domain.minimumX) || !std::isfinite(domain.minimumZ) || !std::isfinite(domain.maximumX) ||
        !std::isfinite(domain.maximumZ) || domain.maximumX <= domain.minimumX || domain.maximumZ <= domain.minimumZ)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain vegetation weight domain is invalid", {}, {},
                              "asset.procgen.terrainVegetation"));
    if (material.layers.empty() || atlases.groups.size() != (material.layers.size() + 3) / 4)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain material control groups are incomplete", {}, {},
                              "asset.procgen.terrainVegetation"));
    std::map<std::string, std::uint32_t, std::less<>> layerIndices;
    for (std::uint32_t index = 0; index < material.layers.size(); ++index)
        if (material.layers[index].name.empty() || !layerIndices.emplace(material.layers[index].name, index).second)
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::Conflict, "terrain layer names must be non-empty and unique", {}, {},
                                  "asset.procgen.terrainVegetation"));
    for (std::size_t index = 0; index < atlases.groups.size(); ++index) {
        const auto&         group = atlases.groups[index];
        const std::uint32_t expected =
            static_cast<std::uint32_t>(std::min<std::size_t>(4, material.layers.size() - index * 4));
        if (group.firstLayer != index * 4 || group.layerCount != expected || group.control.width == 0 ||
            group.control.height == 0 ||
            group.control.pixels.size() != std::size_t(group.control.width) * group.control.height * 4)
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain control image is invalid",
                                  std::to_string(index), {}, "asset.procgen.terrainVegetation"));
    }

    TerrainVegetationRealization result = realization;
    result.instances.clear();
    result.instances.reserve(realization.instances.size());
    result.weightCulledCount = realization.weightCulledCount;
    for (const auto& instance : realization.instances) {
        if (instance.layer.empty()) {
            result.instances.push_back(instance);
            continue;
        }
        const auto found = layerIndices.find(instance.layer);
        if (found == layerIndices.end())
            return Result<TerrainVegetationRealization>::failure(
                Diagnostic::error(DiagnosticCode::NotFound, "vegetation rule references an unknown terrain layer",
                                  instance.layer, {}, "asset.procgen.terrainVegetation"));
        const std::uint32_t layer = found->second;
        const float         u     = (instance.position[0] - domain.minimumX) / (domain.maximumX - domain.minimumX);
        float               v     = (instance.position[2] - domain.minimumZ) / (domain.maximumZ - domain.minimumZ);
        if (domain.flipV) v = 1.f - v;
        const float weight = sampleControl(atlases.groups[layer / 4].control, layer % 4, u, v);
        if (stableUnit(instance.id ^ (std::uint64_t(instance.seed) << 32) ^ layer) < weight)
            result.instances.push_back(instance);
        else
            ++result.weightCulledCount;
    }
    if (result.weightCulledCount - realization.weightCulledCount > result.generatedCount)
        return Result<TerrainVegetationRealization>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "terrain layer filtering culled non-procedural instances", {},
                              {}, "asset.procgen.terrainVegetation"));
    result.generatedCount -= result.weightCulledCount - realization.weightCulledCount;
    rebuildBuckets(result);
    return Result<TerrainVegetationRealization>::success(std::move(result));
}

}  // namespace eve::asset_procgen
