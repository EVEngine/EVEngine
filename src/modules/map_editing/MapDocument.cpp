#include "map_editing/MapDocument.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <utility>

namespace eve::map_editing {
namespace {

template <class T>
EditorResult<T> mapError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

EditorValue layerValue(const MapLayerRecord& layer) {
    return EditorValue::Object{{"id", layer.id.value()},       {"name", layer.name},     {"kind", layer.kind},
                               {"visible", layer.visible},     {"locked", layer.locked}, {"opacity", layer.opacity},
                               {"order", int64_t{layer.order}}};
}

EditorResult<MapLayerRecord> parseLayer(const EditorValue& value) {
    const auto* idValue      = field(value, "id");
    const auto* nameValue    = field(value, "name");
    const auto* kindValue    = field(value, "kind");
    const auto* visibleValue = field(value, "visible");
    const auto* lockedValue  = field(value, "locked");
    const auto* opacityValue = field(value, "opacity");
    const auto* orderValue   = field(value, "order");

    const auto* id   = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* name = nameValue ? nameValue->getIf<std::string>() : nullptr;
    const auto* kind = kindValue ? kindValue->getIf<std::string>() : nullptr;

    const auto* visible = visibleValue ? visibleValue->getIf<bool>() : nullptr;
    const auto* locked  = lockedValue ? lockedValue->getIf<bool>() : nullptr;
    const auto* opacity = opacityValue ? opacityValue->getIf<double>() : nullptr;
    const auto* order   = orderValue ? orderValue->getIf<int64_t>() : nullptr;

    static const std::set<std::string> kinds{"tile", "object", "road", "group"};
    if (!id || id->empty() || !name || name->empty() || !kind || !kinds.contains(*kind) || !visible || !locked ||
        !opacity || *opacity < 0.0 || *opacity > 1.0 || !order)
        return mapError<MapLayerRecord>(EditorStatus::Rejected, "editor.map.invalid-layer",
                                        "Map layer has invalid identity, kind, opacity or order");
    return eve::editing::applied<MapLayerRecord>(
        {StableId(*id), *name, *kind, *visible, *locked, *opacity, static_cast<int>(*order)});
}

EditorValue pointValue(const MapSplinePointRecord& point) {
    return EditorValue::Object{
        {"id", point.id.value()}, {"x", point.x}, {"y", point.y}, {"z", point.z}, {"width", point.width}};
}

EditorResult<MapSplinePointRecord> parsePoint(const EditorValue& value) {
    const auto* idValue    = field(value, "id");
    const auto* xValue     = field(value, "x");
    const auto* yValue     = field(value, "y");
    const auto* zValue     = field(value, "z");
    const auto* widthValue = field(value, "width");
    const auto* id         = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* x          = xValue ? xValue->getIf<double>() : nullptr;
    const auto* y          = yValue ? yValue->getIf<double>() : nullptr;
    const auto* z          = zValue ? zValue->getIf<double>() : nullptr;
    const auto* width      = widthValue ? widthValue->getIf<double>() : nullptr;
    if (!id || id->empty() || !x || !y || !z || !width || *width <= 0.0 || !std::isfinite(*x) || !std::isfinite(*y) ||
        !std::isfinite(*z) || !std::isfinite(*width))
        return mapError<MapSplinePointRecord>(EditorStatus::Rejected, "editor.map.invalid-spline-point",
                                              "Spline point requires finite coordinates and positive width");
    return eve::editing::applied<MapSplinePointRecord>({StableId(*id), *x, *y, *z, *width});
}

EditorValue roadValue(const MapRoadRecord& road) {
    EditorValue::Array points;
    for (const MapSplinePointRecord& point : road.points) points.push_back(pointValue(point));
    return EditorValue::Object{{"id", road.id.value()}, {"layer", road.layer.value()},
                               {"name", road.name},     {"materialAsset", road.materialAsset},
                               {"closed", road.closed}, {"points", std::move(points)}};
}

EditorResult<MapRoadRecord> parseRoad(const EditorValue& value) {
    const auto* idValue         = field(value, "id");
    const auto* layerValueEntry = field(value, "layer");
    const auto* nameValue       = field(value, "name");
    const auto* materialValue   = field(value, "materialAsset");
    const auto* closedValue     = field(value, "closed");
    const auto* pointsValue     = field(value, "points");

    const auto* id       = idValue ? idValue->getIf<std::string>() : nullptr;
    const auto* layer    = layerValueEntry ? layerValueEntry->getIf<std::string>() : nullptr;
    const auto* name     = nameValue ? nameValue->getIf<std::string>() : nullptr;
    const auto* material = materialValue ? materialValue->getIf<std::string>() : nullptr;
    const auto* closed   = closedValue ? closedValue->getIf<bool>() : nullptr;
    const auto* points   = pointsValue ? pointsValue->getIf<EditorValue::Array>() : nullptr;

    if (!id || id->empty() || !layer || layer->empty() || !name || name->empty() || !material || !closed || !points ||
        points->size() < 2)
        return mapError<MapRoadRecord>(EditorStatus::Rejected, "editor.map.invalid-road",
                                       "Road requires identity, layer, name and at least two points");
    MapRoadRecord      result{StableId(*id), StableId(*layer), *name, *material, *closed, {}};
    std::set<StableId> pointIds;
    for (const EditorValue& pointValueEntry : *points) {
        auto point = parsePoint(pointValueEntry);
        if (!point.ok() || !pointIds.insert(point.value().id).second)
            return mapError<MapRoadRecord>(EditorStatus::Rejected, "editor.map.duplicate-spline-point",
                                           "Road spline point ids must be unique");
        result.points.push_back(std::move(point).value());
    }
    if (result.closed && result.points.size() < 3)
        return mapError<MapRoadRecord>(EditorStatus::Rejected, "editor.map.closed-road-point-count",
                                       "Closed road requires at least three points");
    return eve::editing::applied<MapRoadRecord>(std::move(result));
}

EditorValue placementValue(const MapPlacementRecord& placement) {
    return EditorValue::Object{{"id", placement.id.value()},
                               {"layer", placement.layer.value()},
                               {"asset", placement.asset},
                               {"x", placement.x},
                               {"y", placement.y},
                               {"z", placement.z},
                               {"rotationX", placement.rotationX},
                               {"rotationY", placement.rotationY},
                               {"rotationZ", placement.rotationZ},
                               {"scaleX", placement.scaleX},
                               {"scaleY", placement.scaleY},
                               {"scaleZ", placement.scaleZ}};
}

EditorResult<MapPlacementRecord> parsePlacement(const EditorValue& value) {
    const auto string = [&](const char* key) -> const std::string* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<std::string>() : nullptr;
    };
    const auto number = [&](const char* key) -> const double* {
        const EditorValue* entry = field(value, key);
        return entry ? entry->getIf<double>() : nullptr;
    };
    const auto* id    = string("id");
    const auto* layer = string("layer");
    const auto* asset = string("asset");
    const auto* x     = number("x");
    const auto* y     = number("y");
    const auto* z     = number("z");
    const auto* rx    = number("rotationX");
    const auto* ry    = number("rotationY");
    const auto* rz    = number("rotationZ");
    const auto* sx    = number("scaleX");
    const auto* sy    = number("scaleY");
    const auto* sz    = number("scaleZ");
    if (!id || id->empty() || !layer || layer->empty() || !asset || asset->empty() || !x || !y || !z || !rx || !ry ||
        !rz || !sx || !sy || !sz || *sx <= 0.0 || *sy <= 0.0 || *sz <= 0.0)
        return mapError<MapPlacementRecord>(EditorStatus::Rejected, "editor.map.invalid-placement",
                                            "Placement requires ids, asset, complete TRS and positive scale");
    for (const double* component : {x, y, z, rx, ry, rz, sx, sy, sz})
        if (!std::isfinite(*component))
            return mapError<MapPlacementRecord>(EditorStatus::Rejected, "editor.map.nonfinite-placement",
                                                "Placement transform must be finite");
    return eve::editing::applied<MapPlacementRecord>(
        {StableId(*id), StableId(*layer), *asset, *x, *y, *z, *rx, *ry, *rz, *sx, *sy, *sz});
}

DomainOperation operation(std::string type, std::string inverseType, const std::string& target, EditorValue payload,
                          EditorValue inverse, const StableId& affected) {
    DomainOperation result;
    result.type        = std::move(type);
    result.inverseType = std::move(inverseType);
    result.target      = TargetId(target);
    result.payload     = std::move(payload);
    result.inverse     = std::move(inverse);
    result.hasInverse  = true;
    result.affectedObjects.push_back({TargetId(target), affected.value(), 0});
    return result;
}

}  // namespace

MapDocumentTarget::MapDocumentTarget(std::string id) : id_(std::move(id)) {}

TargetDescriptor MapDocumentTarget::describe() const {
    TargetDescriptor result;
    result.id           = TargetId(id_);
    result.type         = "map-document";
    result.revision     = revision_;
    result.capabilities = {IMapStructureEditTarget::editorCapabilityId()};
    return result;
}

void* MapDocumentTarget::queryCapability(const CapabilityId& capability) {
    return capability == IMapStructureEditTarget::editorCapabilityId() ? static_cast<IMapStructureEditTarget*>(this)
                                                                       : nullptr;
}

EditorResult<void> MapDocumentTarget::applyDomainOperation(const DomainOperation& domainOperation) {
    if (domainOperation.target != TargetId(id_))
        return mapError<void>(EditorStatus::Rejected, "editor.map.target-mismatch",
                              "Map operation targets another document");
    if (domainOperation.type == "map.layer.create.v1") {
        auto layer = parseLayer(domainOperation.payload);
        if (!layer.ok()) return mapError<void>(layer.code(), "editor.map.invalid-layer", "Layer payload is invalid");
        if (layers_.contains(layer.value().id))
            return mapError<void>(EditorStatus::Conflict, "editor.map.layer-exists", "Map layer already exists");
        for (const auto& [id, current] : layers_) {
            static_cast<void>(id);
            if (current.order == layer.value().order)
                return mapError<void>(EditorStatus::Conflict, "editor.map.layer-order-conflict",
                                      "Map layer visual order is already occupied");
        }
        auto layerValue = std::move(layer).value();
        layers_.emplace(layerValue.id, std::move(layerValue));
    } else if (domainOperation.type == "map.layer.delete.v1") {
        auto layer = parseLayer(domainOperation.payload);
        if (!layer.ok() || !layers_.contains(layer.value().id))
            return mapError<void>(EditorStatus::NotFound, "editor.map.layer-not-found", "Map layer was not found");
        if (layers_.at(layer.value().id).locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked", "Map layer is locked");
        for (const auto& [id, road] : roads_) {
            static_cast<void>(id);
            if (road.layer == layer.value().id)
                return mapError<void>(EditorStatus::Rejected, "editor.map.layer-not-empty",
                                      "Map layer still contains roads");
        }
        for (const auto& [id, placement] : placements_) {
            static_cast<void>(id);
            if (placement.layer == layer.value().id)
                return mapError<void>(EditorStatus::Rejected, "editor.map.layer-not-empty",
                                      "Map layer still contains placements");
        }
        layers_.erase(layer.value().id);
    } else if (domainOperation.type == "map.layer.set.v1") {
        auto layer = parseLayer(domainOperation.payload);
        if (!layer.ok() || !layers_.contains(layer.value().id))
            return mapError<void>(EditorStatus::NotFound, "editor.map.layer-not-found", "Map layer was not found");
        for (const auto& [id, current] : layers_)
            if (id != layer.value().id && current.order == layer.value().order)
                return mapError<void>(EditorStatus::Conflict, "editor.map.layer-order-conflict",
                                      "Map layer visual order is already occupied");
        if (layers_.at(layer.value().id).kind != layer.value().kind) {
            for (const auto& [id, road] : roads_) {
                static_cast<void>(id);
                if (road.layer == layer.value().id)
                    return mapError<void>(EditorStatus::Rejected, "editor.map.layer-kind-in-use",
                                          "Cannot change kind of a layer containing roads");
            }
            for (const auto& [id, placement] : placements_) {
                static_cast<void>(id);
                if (placement.layer == layer.value().id)
                    return mapError<void>(EditorStatus::Rejected, "editor.map.layer-kind-in-use",
                                          "Cannot change kind of a layer containing placements");
            }
        }
        auto layerValue = std::move(layer).value();
        layers_[layerValue.id] = std::move(layerValue);
    } else if (domainOperation.type == "map.road.set.v1") {
        auto road = parseRoad(domainOperation.payload);
        if (!road.ok()) return mapError<void>(road.code(), "editor.map.invalid-road", "Road payload is invalid");
        const auto layer = layers_.find(road.value().layer);
        if (layer == layers_.end() || layer->second.kind != "road")
            return mapError<void>(EditorStatus::Rejected, "editor.map.road-layer-required",
                                  "Road must reference a road layer");
        if (layer->second.locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked", "Road layer is locked");
        const auto existing = roads_.find(road.value().id);
        if (existing != roads_.end() && layers_.at(existing->second.layer).locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked",
                                  "Road cannot be moved out of a locked layer");
        auto roadValue = std::move(road).value();
        roads_[roadValue.id] = std::move(roadValue);
    } else if (domainOperation.type == "map.road.delete.v1") {
        auto road = parseRoad(domainOperation.payload);
        if (!road.ok() || !roads_.contains(road.value().id))
            return mapError<void>(EditorStatus::NotFound, "editor.map.road-not-found", "Road was not found");
        if (layers_.at(roads_.at(road.value().id).layer).locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked", "Road layer is locked");
        roads_.erase(road.value().id);
    } else if (domainOperation.type == "map.placement.set.v1") {
        auto placement = parsePlacement(domainOperation.payload);
        if (!placement.ok())
            return mapError<void>(placement.code(), "editor.map.invalid-placement", "Placement payload is invalid");
        const auto layer = layers_.find(placement.value().layer);
        if (layer == layers_.end() || layer->second.kind != "object")
            return mapError<void>(EditorStatus::Rejected, "editor.map.object-layer-required",
                                  "Placement must reference an object layer");
        if (layer->second.locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked", "Object layer is locked");
        const auto existing = placements_.find(placement.value().id);
        if (existing != placements_.end() && layers_.at(existing->second.layer).locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked",
                                  "Placement cannot be moved out of a locked layer");
        auto placementValue = std::move(placement).value();
        placements_[placementValue.id] = std::move(placementValue);
    } else if (domainOperation.type == "map.placement.delete.v1") {
        auto placement = parsePlacement(domainOperation.payload);
        if (!placement.ok() || !placements_.contains(placement.value().id))
            return mapError<void>(EditorStatus::NotFound, "editor.map.placement-not-found", "Placement was not found");
        if (layers_.at(placements_.at(placement.value().id).layer).locked)
            return mapError<void>(EditorStatus::Rejected, "editor.map.layer-locked", "Object layer is locked");
        placements_.erase(placement.value().id);
    } else {
        return mapError<void>(EditorStatus::Unsupported, "editor.map.operation-unsupported",
                              "Map operation is unsupported: " + domainOperation.type);
    }
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> MapDocumentTarget::cloneDomainState() const {
    return std::make_unique<MapDocumentTarget>(*this);
}

EditorResult<void> MapDocumentTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* staged = dynamic_cast<MapDocumentTarget*>(candidate.get());
    if (!staged || staged->id_ != id_)
        return mapError<void>(EditorStatus::Conflict, "editor.map.candidate-mismatch",
                              "Map candidate belongs to another target");
    layers_.swap(staged->layers_);
    roads_.swap(staged->roads_);
    placements_.swap(staged->placements_);
    revision_ = staged->revision_;
    dirty_    = staged->dirty_;
    return eve::editing::applied<void>();
}

std::vector<MapLayerRecord> MapDocumentTarget::mapLayers() const {
    std::vector<MapLayerRecord> result;
    for (const auto& [id, layer] : layers_) {
        static_cast<void>(id);
        result.push_back(layer);
    }
    std::sort(result.begin(), result.end(), [](const MapLayerRecord& left, const MapLayerRecord& right) {
        return left.order != right.order ? left.order < right.order : left.id < right.id;
    });
    return result;
}

std::vector<MapRoadRecord> MapDocumentTarget::mapRoads() const {
    std::vector<MapRoadRecord> result;
    for (const auto& [id, road] : roads_) {
        static_cast<void>(id);
        result.push_back(road);
    }
    return result;
}

std::vector<MapPlacementRecord> MapDocumentTarget::mapPlacements() const {
    std::vector<MapPlacementRecord> result;
    for (const auto& [id, placement] : placements_) {
        static_cast<void>(id);
        result.push_back(placement);
    }
    return result;
}

EditorResult<DomainOperation> MapDocumentTarget::makeCreateLayer(const MapLayerRecord& layer) const {
    auto parsed = parseLayer(layerValue(layer));
    if (!parsed.ok()) return mapError<DomainOperation>(parsed.code(), "editor.map.invalid-layer", "Layer is invalid");
    if (layers_.contains(layer.id))
        return mapError<DomainOperation>(EditorStatus::Conflict, "editor.map.layer-exists", "Map layer already exists");
    for (const auto& [id, current] : layers_) {
        static_cast<void>(id);
        if (current.order == layer.order)
            return mapError<DomainOperation>(EditorStatus::Conflict, "editor.map.layer-order-conflict",
                                             "Map layer visual order is already occupied");
    }
    return eve::editing::applied<DomainOperation>(
        operation("map.layer.create.v1", "map.layer.delete.v1", id_, layerValue(layer), layerValue(layer), layer.id));
}

EditorResult<DomainOperation> MapDocumentTarget::makeDeleteLayer(const StableId& layer) const {
    const auto found = layers_.find(layer);
    if (found == layers_.end())
        return mapError<DomainOperation>(EditorStatus::NotFound, "editor.map.layer-not-found",
                                         "Map layer was not found");
    if (found->second.locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked", "Map layer is locked");
    for (const auto& [id, road] : roads_) {
        static_cast<void>(id);
        if (road.layer == layer)
            return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-not-empty",
                                             "Delete roads before deleting their layer");
    }
    for (const auto& [id, placement] : placements_) {
        static_cast<void>(id);
        if (placement.layer == layer)
            return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-not-empty",
                                             "Delete placements before deleting their layer");
    }
    return eve::editing::applied<DomainOperation>(operation("map.layer.delete.v1", "map.layer.create.v1", id_,
                                                            layerValue(found->second), layerValue(found->second),
                                                            layer));
}

EditorResult<DomainOperation> MapDocumentTarget::makeSetLayer(const MapLayerRecord& layer) const {
    const auto found = layers_.find(layer.id);
    if (found == layers_.end())
        return mapError<DomainOperation>(EditorStatus::NotFound, "editor.map.layer-not-found",
                                         "Map layer was not found");
    auto parsed = parseLayer(layerValue(layer));
    if (!parsed.ok()) return mapError<DomainOperation>(parsed.code(), "editor.map.invalid-layer", "Layer is invalid");
    for (const auto& [id, current] : layers_)
        if (id != layer.id && current.order == layer.order)
            return mapError<DomainOperation>(EditorStatus::Conflict, "editor.map.layer-order-conflict",
                                             "Map layer visual order is already occupied");
    if (found->second.kind != layer.kind) {
        for (const auto& [id, road] : roads_) {
            static_cast<void>(id);
            if (road.layer == layer.id)
                return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-kind-in-use",
                                                 "Cannot change kind of a layer containing roads");
        }
        for (const auto& [id, placement] : placements_) {
            static_cast<void>(id);
            if (placement.layer == layer.id)
                return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-kind-in-use",
                                                 "Cannot change kind of a layer containing placements");
        }
    }
    return eve::editing::applied<DomainOperation>(
        operation("map.layer.set.v1", "map.layer.set.v1", id_, layerValue(layer), layerValue(found->second), layer.id));
}

EditorResult<DomainOperation> MapDocumentTarget::makeSetRoad(const MapRoadRecord& road) const {
    auto parsed = parseRoad(roadValue(road));
    if (!parsed.ok()) return mapError<DomainOperation>(parsed.code(), "editor.map.invalid-road", "Road is invalid");
    const auto layer = layers_.find(road.layer);
    if (layer == layers_.end() || layer->second.kind != "road")
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.road-layer-required",
                                         "Road must reference a road layer");
    if (layer->second.locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked", "Road layer is locked");
    const auto found = roads_.find(road.id);
    if (found != roads_.end() && layers_.at(found->second.layer).locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked",
                                         "Road cannot be moved out of a locked layer");
    const EditorValue inverse     = found == roads_.end() ? roadValue(road) : roadValue(found->second);
    const std::string inverseType = found == roads_.end() ? "map.road.delete.v1" : "map.road.set.v1";
    return eve::editing::applied<DomainOperation>(
        operation("map.road.set.v1", inverseType, id_, roadValue(road), inverse, road.id));
}

EditorResult<DomainOperation> MapDocumentTarget::makeDeleteRoad(const StableId& road) const {
    const auto found = roads_.find(road);
    if (found == roads_.end())
        return mapError<DomainOperation>(EditorStatus::NotFound, "editor.map.road-not-found", "Road was not found");
    if (layers_.at(found->second.layer).locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked", "Road layer is locked");
    return eve::editing::applied<DomainOperation>(operation("map.road.delete.v1", "map.road.set.v1", id_,
                                                            roadValue(found->second), roadValue(found->second), road));
}

EditorResult<DomainOperation> MapDocumentTarget::makeSetPlacement(const MapPlacementRecord& placement) const {
    auto parsed = parsePlacement(placementValue(placement));
    if (!parsed.ok())
        return mapError<DomainOperation>(parsed.code(), "editor.map.invalid-placement", "Placement is invalid");
    const auto layer = layers_.find(placement.layer);
    if (layer == layers_.end() || layer->second.kind != "object")
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.object-layer-required",
                                         "Placement must reference an object layer");
    if (layer->second.locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked", "Object layer is locked");
    const auto found = placements_.find(placement.id);
    if (found != placements_.end() && layers_.at(found->second.layer).locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked",
                                         "Placement cannot be moved out of a locked layer");
    const EditorValue inverse = found == placements_.end() ? placementValue(placement) : placementValue(found->second);
    const std::string inverseType = found == placements_.end() ? "map.placement.delete.v1" : "map.placement.set.v1";
    return eve::editing::applied<DomainOperation>(
        operation("map.placement.set.v1", inverseType, id_, placementValue(placement), inverse, placement.id));
}

EditorResult<DomainOperation> MapDocumentTarget::makeDeletePlacement(const StableId& placement) const {
    const auto found = placements_.find(placement);
    if (found == placements_.end())
        return mapError<DomainOperation>(EditorStatus::NotFound, "editor.map.placement-not-found",
                                         "Placement was not found");
    if (layers_.at(found->second.layer).locked)
        return mapError<DomainOperation>(EditorStatus::Rejected, "editor.map.layer-locked", "Object layer is locked");
    return eve::editing::applied<DomainOperation>(operation("map.placement.delete.v1", "map.placement.set.v1", id_,
                                                            placementValue(found->second),
                                                            placementValue(found->second), placement));
}

std::vector<EditorDiagnostic> MapDocumentTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    std::set<int>                 orders;
    for (const auto& [id, layer] : layers_) {
        static_cast<void>(id);
        if (!orders.insert(layer.order).second)
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvariantViolation, RuleId("editor.map.duplicate-layer-order"),
                DiagnosticSeverity::Error, "Multiple map layers share visual order " + std::to_string(layer.order)));
    }
    for (const auto& [id, road] : roads_) {
        const auto layer = layers_.find(road.layer);
        if (layer == layers_.end() || layer->second.kind != "road")
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvariantViolation, RuleId("editor.map.invalid-road-layer"),
                DiagnosticSeverity::Error, "Road references a missing or non-road layer: " + id.value()));
        double lengthSquared = 0.0;
        for (size_t index = 1; index < road.points.size(); ++index) {
            const double dx = road.points[index].x - road.points[index - 1].x;
            const double dy = road.points[index].y - road.points[index - 1].y;
            const double dz = road.points[index].z - road.points[index - 1].z;
            lengthSquared += dx * dx + dy * dy + dz * dz;
        }
        if (lengthSquared <= 1e-12)
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvariantViolation, RuleId("editor.map.zero-length-road"),
                DiagnosticSeverity::Error, "Road has no geometric length: " + id.value()));
        if (road.materialAsset.empty())
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::PreconditionViolation, RuleId("editor.map.road-material-missing"),
                DiagnosticSeverity::Warning, "Road has no material asset: " + id.value()));
    }
    for (const auto& [id, placement] : placements_) {
        const auto layer = layers_.find(placement.layer);
        if (layer == layers_.end() || layer->second.kind != "object")
            diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvariantViolation, RuleId("editor.map.invalid-placement-layer"),
                DiagnosticSeverity::Error, "Placement references a missing or non-object layer: " + id.value()));
    }
    return diagnostics;
}

MapRoadPreviewResult MapDocumentTarget::previewRoad(const StableId& roadId, int triangleBudget) const {
    MapRoadPreviewResult result;
    result.documentRevision = revision_;
    result.road             = roadId;
    const auto found        = roads_.find(roadId);
    if (found == roads_.end()) {
        result.status = EditorStatus::NotFound;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::NotFound, RuleId("editor.map.road-not-found"), DiagnosticSeverity::Error,
            "Road preview target was not found"));
        return result;
    }
    if (triangleBudget <= 0) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::InvalidArgument, RuleId("editor.map.invalid-preview-budget"),
            DiagnosticSeverity::Error, "Road preview triangle budget must be positive"));
        return result;
    }
    const MapRoadRecord& road          = found->second;
    const int            segmentCount  = static_cast<int>(road.points.size()) - (road.closed ? 0 : 1);
    const int            triangleCount = segmentCount * 2;
    if (triangleCount > triangleBudget) {
        result.status = EditorStatus::Rejected;
        result.diagnostics.push_back(eve::editing::ruleDiagnostic(
            eve::DiagnosticCode::PreconditionViolation, RuleId("editor.map.road-preview-budget"),
            DiagnosticSeverity::Error, "Road strip exceeds preview triangle budget"));
        return result;
    }
    EditorValue::Array vertices;
    EditorValue::Array indices;
    for (size_t index = 0; index < road.points.size(); ++index) {
        const MapSplinePointRecord& point = road.points[index];
        const MapSplinePointRecord& previous =
            road.points[index == 0 ? (road.closed ? road.points.size() - 1 : 0) : index - 1];
        const MapSplinePointRecord& next =
            road.points[index + 1 == road.points.size() ? (road.closed ? 0 : road.points.size() - 1) : index + 1];
        double       tangentX = next.x - previous.x;
        double       tangentZ = next.z - previous.z;
        const double length   = std::sqrt(tangentX * tangentX + tangentZ * tangentZ);
        if (length <= 1e-9) {
            result.status = EditorStatus::Failed;
            result.diagnostics.push_back(eve::editing::ruleDiagnostic(
                eve::DiagnosticCode::InvariantViolation, RuleId("editor.map.degenerate-road-tangent"),
                DiagnosticSeverity::Error,
                "Road preview contains a degenerate tangent at point " + point.id.value()));
            return result;
        }
        tangentX /= length;
        tangentZ /= length;
        const double halfWidth = point.width * 0.5;
        vertices.emplace_back(
            EditorValue::Array{point.x - tangentZ * halfWidth, point.y, point.z + tangentX * halfWidth});
        vertices.emplace_back(
            EditorValue::Array{point.x + tangentZ * halfWidth, point.y, point.z - tangentX * halfWidth});
        if (index > 0) {
            const int64_t base = static_cast<int64_t>(index * 2);
            for (const int64_t value : {base - 2, base - 1, base, base, base - 1, base + 1})
                indices.emplace_back(value);
            const double dx = point.x - previous.x;
            const double dy = point.y - previous.y;
            const double dz = point.z - previous.z;
            result.centerlineLength += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    if (road.closed) {
        const int64_t last = static_cast<int64_t>(road.points.size() * 2 - 2);
        for (const int64_t value : {last, last + 1, int64_t{0}, int64_t{0}, last + 1, int64_t{1}})
            indices.emplace_back(value);
        const MapSplinePointRecord& first     = road.points.front();
        const MapSplinePointRecord& lastPoint = road.points.back();
        const double                dx        = first.x - lastPoint.x;
        const double                dy        = first.y - lastPoint.y;
        const double                dz        = first.z - lastPoint.z;
        result.centerlineLength += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    result.mesh   = EditorValue::Object{{"primitive", "triangles"},
                                        {"vertices", std::move(vertices)},
                                        {"indices", std::move(indices)},
                                        {"materialAsset", road.materialAsset}};
    result.status = EditorStatus::Applied;
    return result;
}

EditorValue MapDocumentTarget::snapshotValue() const {
    EditorValue::Array layers;
    for (const MapLayerRecord& layer : mapLayers()) layers.push_back(layerValue(layer));
    EditorValue::Array roads;
    for (const MapRoadRecord& road : mapRoads()) roads.push_back(roadValue(road));
    EditorValue::Array placements;
    for (const MapPlacementRecord& placement : mapPlacements()) placements.push_back(placementValue(placement));
    return EditorValue::Object{{"schemaVersion", int64_t{1}},
                               {"layers", std::move(layers)},
                               {"roads", std::move(roads)},
                               {"placements", std::move(placements)}};
}

EditorResult<void> MapDocumentTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* versionValue    = field(snapshot, "schemaVersion");
    const auto* layersValue     = field(snapshot, "layers");
    const auto* roadsValue      = field(snapshot, "roads");
    const auto* placementsValue = field(snapshot, "placements");
    const auto* version         = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    const auto* layers          = layersValue ? layersValue->getIf<EditorValue::Array>() : nullptr;
    const auto* roads           = roadsValue ? roadsValue->getIf<EditorValue::Array>() : nullptr;
    const auto* placements      = placementsValue ? placementsValue->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !layers || !roads || !placements)
        return mapError<void>(EditorStatus::Unsupported, "editor.map.invalid-snapshot",
                              "Map snapshot schema is invalid or unsupported");
    MapDocumentTarget candidate(id_);
    for (const EditorValue& value : *layers) {
        auto layer = parseLayer(value);
        if (!layer.ok()) return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-layer", "Invalid layer");
        auto create = candidate.makeCreateLayer(layer.value());
        if (!create.ok() || !candidate.applyDomainOperation(create.value()).ok())
            return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-layer-set",
                                  "Map snapshot contains conflicting layers");
    }
    for (const EditorValue& value : *roads) {
        auto road = parseRoad(value);
        if (!road.ok()) return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-road", "Invalid road");
        auto set = candidate.makeSetRoad(road.value());
        if (!set.ok() || !candidate.applyDomainOperation(set.value()).ok())
            return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-road-set",
                                  "Map snapshot contains an invalid road layer reference");
    }
    for (const EditorValue& value : *placements) {
        auto placement = parsePlacement(value);
        if (!placement.ok())
            return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-placement", "Invalid placement");
        auto set = candidate.makeSetPlacement(placement.value());
        if (!set.ok() || !candidate.applyDomainOperation(set.value()).ok())
            return mapError<void>(EditorStatus::Rejected, "editor.map.invalid-placement-set",
                                  "Map snapshot contains an invalid placement layer reference");
    }
    layers_.swap(candidate.layers_);
    roads_.swap(candidate.roads_);
    placements_.swap(candidate.placements_);
    ++revision_;
    dirty_.clear();
    return eve::editing::applied<void>();
}

}  // namespace eve::map_editing
