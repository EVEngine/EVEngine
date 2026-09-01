#include "asset_procgen/EvpackPointGraphLoader.h"

#include "common/Value.h"
#include "asset/RuntimeDefinition.h"
#include "procgen/PointGraph.h"
#include "procgen/SpatialData.h"

namespace eve::asset_procgen {
namespace {

template <class T>
Result<T> failure(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path), {},
                                                "asset.procgen"));
}

const Value* field(const Value::Object& object, std::string_view name) {
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found->second;
}

}  // namespace

Result<LoadedPointGraph> EvpackPointGraphLoader::load(
    const AssetRef& graphRef, const asset::EvpackCapabilities& capabilities,
    const procgen::SpatialData& terrain, std::uint64_t maximumDecodedBytes) const {
    auto payload = reader_.read(graphRef, "eve.pcg-graph/1", capabilities, maximumDecodedBytes);
    if (!payload) return Result<LoadedPointGraph>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    const asset::RuntimeAssetChunk* plan = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind == asset::EvpackChunkKind::Definition) {
            if (definition) return failure<LoadedPointGraph>(DiagnosticCode::Conflict,
                                                              "PCG asset has duplicate definitions");
            definition = &chunk;
        } else if (chunk.kind == asset::EvpackChunkKind::Bulk) {
            if (plan) return failure<LoadedPointGraph>(DiagnosticCode::Conflict,
                                                       "PCG asset has duplicate execution plans");
            plan = &chunk;
        }
    }
    if (!definition || !plan || plan->chunkId != 1)
        return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                         "PCG definition or execution plan is missing");
    asset::RuntimeDefinitionLimits definitionLimits;
    definitionLimits.maximumBytes = maximumDecodedBytes;
    auto metadata = asset::decodeRuntimeDefinition(definition->bytes, definitionLimits);
    if (!metadata) return Result<LoadedPointGraph>::failure(metadata.status());
    const auto* root = metadata.value().getIf<Value::Object>();
    const Value* schema = root ? field(*root, "schema") : nullptr;
    const Value* version = root ? field(*root, "schemaVersion") : nullptr;
    const Value* planReference = root ? field(*root, "executionPlan") : nullptr;
    const Value* format = root ? field(*root, "executionPlanFormat") : nullptr;
    const Value* output = root ? field(*root, "outputNode") : nullptr;
    const Value* slotsValue = root ? field(*root, "spatialSlots") : nullptr;
    const auto* slots = slotsValue ? slotsValue->getIf<Value::Array>() : nullptr;
    if (!schema || !schema->isString() || schema->asString() != "eve.pcg-graph" ||
        !version || !version->isInt64() || version->asInt() != 1 || !planReference ||
        !planReference->isString() || planReference->asString() != "chunk:1" || !format ||
        !format->isString() || format->asString() != "EVPCG_POINT_GRAPH/1" ||
        !output || !output->isString() || output->asString().empty() || !slots || slots->empty())
        return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                         "PCG runtime metadata is invalid");
    auto graph = std::make_unique<procgen::PointGraph>();
    const std::string planText(reinterpret_cast<const char*>(plan->bytes.data()), plan->bytes.size());
    if (!graph->deserializeDefinition(planText))
        return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                         "PCG execution plan is invalid: " + graph->getError());
    if (!graph->hasNode(output->asString()))
        return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                         "PCG output node does not exist", "$.outputNode");
    procgen::SpatialData terrainCopy = terrain;
    for (std::size_t i = 0; i < slots->size(); ++i) {
        const auto* slot = (*slots)[i].getIf<Value::Object>();
        const Value* node = slot ? field(*slot, "node") : nullptr;
        const Value* role = slot ? field(*slot, "role") : nullptr;
        if (!node || !node->isString() || !role || !role->isString() ||
            role->asString() != "terrain" ||
            !graph->setNodeSpatial(node->asString(), &terrainCopy))
            return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                             "PCG spatial slot is invalid",
                                             "$.spatialSlots[" + std::to_string(i) + "]");
    }
    if (!graph->validate())
        return failure<LoadedPointGraph>(DiagnosticCode::ParseError,
                                         "bound PCG graph is invalid: " + graph->getError());
    return Result<LoadedPointGraph>::success(
        {graphRef, std::move(graph), output->asString(), std::move(payload).takeValue().variant});
}

}  // namespace eve::asset_procgen
