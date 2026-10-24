#include "procgen/editing/RoadPresetDocument.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include "common/Diagnostic.h"
#include "editing/EditingProperty.h"
#include "procgen/editing/RoadNetworkEditTargetInternal.inc"
namespace eve::procgen_editing {
namespace {
using procgen::road::RoadNetwork;
using procgen::road::RoadStyle;
using V                              = EditorValue;
constexpr std::size_t maximumHistory = 128, maximumJsonBytes = 8u * 1024u * 1024u;
bool                  boundedValue(const eve::Value& value) {
    std::vector<std::pair<const eve::Value*, std::size_t>> pending{{&value, 0}};
    std::size_t                                            elements = 0, bytes = 0;
    while (!pending.empty()) {
        const auto [entry, depth] = pending.back();
        pending.pop_back();
        if (depth > 64 || ++elements > 262144) return false;
        if (const auto* text = entry->getIf<std::string>()) bytes += text->size();
        if (const auto* array = entry->getIf<eve::Value::Array>()) {
            if (array->size() > 262144 - elements - pending.size()) return false;
            for (const auto& child : *array) pending.emplace_back(&child, depth + 1);
        }
        if (const auto* object = entry->getIf<eve::Value::Object>()) {
            if (object->size() > 262144 - elements - pending.size()) return false;
            for (const auto& [key, child] : *object) {
                bytes += key.size();
                pending.emplace_back(&child, depth + 1);
            }
        }
        if (bytes > maximumJsonBytes) return false;
    }
    return true;
}
template <class T>
eve::Result<T> fail(std::string message) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message), "road.presets"));
}
void apply(RoadStyle& style, const RoadStyleOverrides& patch) {
    if (patch.terrainPolicy) style.terrainPolicy = *patch.terrainPolicy;
    if (patch.sideTerrain) style.sideTerrain = *patch.sideTerrain;
    if (patch.samplingPolicy) style.samplingPolicy = *patch.samplingPolicy;
    if (patch.samplingSpacingMeters) style.samplingSpacingMeters = *patch.samplingSpacingMeters;
    if (patch.samplingAngleDegrees) style.samplingAngleDegrees = *patch.samplingAngleDegrees;
    if (patch.crossSectionSubdivisions) style.crossSectionSubdivisions = *patch.crossSectionSubdivisions;
    if (patch.rollInterpolation) style.rollInterpolation = *patch.rollInterpolation;
    if (patch.frameOrientation) style.frameOrientation = *patch.frameOrientation;
    if (patch.widthInterpolation) style.widthInterpolation = *patch.widthInterpolation;
    if (patch.crossSection) style.crossSection = *patch.crossSection;
    if (patch.crossSectionClosed) style.crossSectionClosed = *patch.crossSectionClosed;
    if (patch.automaticStructures) style.automaticStructures = *patch.automaticStructures;
    if (patch.structures) style.structures = *patch.structures;
    if (patch.facilities) style.facilities = *patch.facilities;
    if (patch.laneWidth) style.laneWidth = *patch.laneWidth;
    if (patch.curbWidth) style.curbWidth = *patch.curbWidth;
    if (patch.curbHeight) style.curbHeight = *patch.curbHeight;
    if (patch.sidewalkWidth) style.sidewalkWidth = *patch.sidewalkWidth;
    if (patch.sidewalkHeight) style.sidewalkHeight = *patch.sidewalkHeight;
    if (patch.deckThickness) style.deckThickness = *patch.deckThickness;
    if (patch.pierWidth) style.pierWidth = *patch.pierWidth;
    if (patch.pierDepth) style.pierDepth = *patch.pierDepth;
    if (patch.pierSpacing) style.pierSpacing = *patch.pierSpacing;
    if (patch.pierClearance) style.pierClearance = *patch.pierClearance;
    if (patch.markingWidth) style.markingWidth = *patch.markingWidth;
    if (patch.dashLength) style.dashLength = *patch.dashLength;
    if (patch.dashGap) style.dashGap = *patch.dashGap;
    if (patch.uvMeters) style.uvMeters = *patch.uvMeters;
    if (patch.speedLimitMps) style.speedLimitMps = *patch.speedLimitMps;
    if (patch.sideObjectStartOffset) style.sideObjectStartOffset = *patch.sideObjectStartOffset;
    if (patch.sideObjectEndOffset) style.sideObjectEndOffset = *patch.sideObjectEndOffset;
    if (patch.trafficPriority) style.trafficPriority = *patch.trafficPriority;
    if (patch.sideObjectsLeft) style.sideObjectsLeft = *patch.sideObjectsLeft;
    if (patch.sidePlacement) style.sidePlacement = *patch.sidePlacement;
    if (patch.sideObjectDistribution) style.sideObjectDistribution = *patch.sideObjectDistribution;
    if (patch.sideObjectsRight) style.sideObjectsRight = *patch.sideObjectsRight;
}
V encodePatch(const RoadStyleOverrides& patch) {
    RoadStyle style;
    apply(style, patch);
    const auto full = *detail::encodeStyle(1, style).getIf<V::Object>();
    V::Object  out;
    if (patch.terrainPolicy) out.emplace("terrainPolicy", full.at("terrainPolicy"));
    if (patch.samplingPolicy) out.emplace("samplingPolicy", full.at("samplingPolicy"));
    if (patch.samplingSpacingMeters) out.emplace("samplingSpacingMeters", full.at("samplingSpacingMeters"));
    if (patch.samplingAngleDegrees) out.emplace("samplingAngleDegrees", full.at("samplingAngleDegrees"));
    if (patch.crossSectionSubdivisions) out.emplace("crossSectionSubdivisions", full.at("crossSectionSubdivisions"));
    if (patch.sideTerrain) out.emplace("sideTerrain", full.at("sideTerrain"));
    if (patch.rollInterpolation) out.emplace("rollInterpolation", full.at("rollInterpolation"));
    if (patch.frameOrientation) out.emplace("frameOrientation", full.at("frameOrientation"));
    if (patch.widthInterpolation) out.emplace("widthInterpolation", full.at("widthInterpolation"));
    if (patch.crossSection) out.emplace("crossSection", full.at("crossSection"));
    if (patch.crossSectionClosed) out.emplace("crossSectionClosed", full.at("crossSectionClosed"));
    if (patch.automaticStructures) out.emplace("automaticStructures", full.at("automaticStructures"));
    if (patch.structures) out.emplace("structures", full.at("structures"));
    if (patch.facilities) out.emplace("facilities", full.at("facilities"));
    if (patch.laneWidth) out.emplace("laneWidth", full.at("laneWidth"));
    if (patch.curbWidth) out.emplace("curbWidth", full.at("curbWidth"));
    if (patch.curbHeight) out.emplace("curbHeight", full.at("curbHeight"));
    if (patch.sidewalkWidth) out.emplace("sidewalkWidth", full.at("sidewalkWidth"));
    if (patch.sidewalkHeight) out.emplace("sidewalkHeight", full.at("sidewalkHeight"));
    if (patch.deckThickness) out.emplace("deckThickness", full.at("deckThickness"));
    if (patch.pierWidth) out.emplace("pierWidth", full.at("pierWidth"));
    if (patch.pierDepth) out.emplace("pierDepth", full.at("pierDepth"));
    if (patch.pierSpacing) out.emplace("pierSpacing", full.at("pierSpacing"));
    if (patch.pierClearance) out.emplace("pierClearance", full.at("pierClearance"));
    if (patch.markingWidth) out.emplace("markingWidth", full.at("markingWidth"));
    if (patch.dashLength) out.emplace("dashLength", full.at("dashLength"));
    if (patch.dashGap) out.emplace("dashGap", full.at("dashGap"));
    if (patch.uvMeters) out.emplace("uvMeters", full.at("uvMeters"));
    if (patch.speedLimitMps) out.emplace("speedLimitMps", full.at("speedLimitMps"));
    if (patch.sideObjectStartOffset) out.emplace("sideObjectStartOffset", full.at("sideObjectStartOffset"));
    if (patch.sideObjectEndOffset) out.emplace("sideObjectEndOffset", full.at("sideObjectEndOffset"));
    if (patch.trafficPriority) out.emplace("trafficPriority", full.at("trafficPriority"));
    if (patch.sideObjectsLeft) out.emplace("sideObjectsLeft", full.at("sideObjectsLeft"));
    if (patch.sidePlacement) out.emplace("sidePlacement", full.at("sidePlacement"));
    if (patch.sideObjectDistribution) out.emplace("sideObjectDistribution", full.at("sideObjectDistribution"));
    if (patch.sideObjectsRight) out.emplace("sideObjectsRight", full.at("sideObjectsRight"));
    return out;
}
eve::Result<RoadStyleOverrides> decodePatch(const V& value) {
    const auto* object = value.getIf<V::Object>();
    if (!object) return fail<RoadStyleOverrides>("override must be an object");
    auto merged = *detail::encodeStyle(1, RoadStyle{}).getIf<V::Object>();
    for (const auto& [key, entry] : *object) {
        if (key == "id" || !merged.contains(key)) continue;
        merged[key] = entry;
    }
    RoadStyle     style;
    std::uint32_t ignored = 0;
    if (!detail::decodeStyle(V(std::move(merged)), ignored, style))
        return fail<RoadStyleOverrides>("invalid override field type or value");
    RoadStyleOverrides patch;
    if (object->contains("terrainPolicy")) patch.terrainPolicy = style.terrainPolicy;
    if (object->contains("sideTerrain")) patch.sideTerrain = style.sideTerrain;
    if (object->contains("rollInterpolation")) patch.rollInterpolation = style.rollInterpolation;
    if (object->contains("samplingSpacingMeters")) patch.samplingSpacingMeters = style.samplingSpacingMeters;
    if (object->contains("samplingPolicy")) patch.samplingPolicy = style.samplingPolicy;
    if (object->contains("samplingAngleDegrees")) patch.samplingAngleDegrees = style.samplingAngleDegrees;
    if (object->contains("crossSectionSubdivisions")) patch.crossSectionSubdivisions = style.crossSectionSubdivisions;
    if (object->contains("frameOrientation")) patch.frameOrientation = style.frameOrientation;
    if (object->contains("widthInterpolation")) patch.widthInterpolation = style.widthInterpolation;
    if (object->contains("crossSection")) patch.crossSection = style.crossSection;
    if (object->contains("crossSectionClosed")) patch.crossSectionClosed = style.crossSectionClosed;
    if (object->contains("automaticStructures")) patch.automaticStructures = style.automaticStructures;
    if (object->contains("structures")) patch.structures = style.structures;
    if (object->contains("facilities")) patch.facilities = style.facilities;
    if (object->contains("laneWidth")) patch.laneWidth = style.laneWidth;
    if (object->contains("curbWidth")) patch.curbWidth = style.curbWidth;
    if (object->contains("curbHeight")) patch.curbHeight = style.curbHeight;
    if (object->contains("sidewalkWidth")) patch.sidewalkWidth = style.sidewalkWidth;
    if (object->contains("sidewalkHeight")) patch.sidewalkHeight = style.sidewalkHeight;
    if (object->contains("deckThickness")) patch.deckThickness = style.deckThickness;
    if (object->contains("pierWidth")) patch.pierWidth = style.pierWidth;
    if (object->contains("pierDepth")) patch.pierDepth = style.pierDepth;
    if (object->contains("pierSpacing")) patch.pierSpacing = style.pierSpacing;
    if (object->contains("pierClearance")) patch.pierClearance = style.pierClearance;
    if (object->contains("markingWidth")) patch.markingWidth = style.markingWidth;
    if (object->contains("dashLength")) patch.dashLength = style.dashLength;
    if (object->contains("dashGap")) patch.dashGap = style.dashGap;
    if (object->contains("uvMeters")) patch.uvMeters = style.uvMeters;
    if (object->contains("speedLimitMps")) patch.speedLimitMps = style.speedLimitMps;
    if (object->contains("sideObjectStartOffset")) patch.sideObjectStartOffset = style.sideObjectStartOffset;
    if (object->contains("sideObjectEndOffset")) patch.sideObjectEndOffset = style.sideObjectEndOffset;
    if (object->contains("trafficPriority")) patch.trafficPriority = style.trafficPriority;
    if (object->contains("sideObjectsLeft")) patch.sideObjectsLeft = style.sideObjectsLeft;
    if (object->contains("sidePlacement")) patch.sidePlacement = style.sidePlacement;
    if (object->contains("sideObjectDistribution")) patch.sideObjectDistribution = style.sideObjectDistribution;
    if (object->contains("sideObjectsRight")) patch.sideObjectsRight = style.sideObjectsRight;
    return eve::Result<RoadStyleOverrides>::success(std::move(patch));
}
void mergePoints(RoadPointOverrides& into, const RoadPointOverrides& patch) {
    if (patch.rollCenterOffset) into.rollCenterOffset = patch.rollCenterOffset;
    if (patch.circularFromEnd) into.circularFromEnd = patch.circularFromEnd;
    if (patch.alignStraightStart) into.alignStraightStart = patch.alignStraightStart;
    if (patch.alignStraightEnd) into.alignStraightEnd = patch.alignStraightEnd;
    if (patch.pathControl) into.pathControl = patch.pathControl;
    if (patch.splineStrength) into.splineStrength = patch.splineStrength;
    if (patch.widthScale) into.widthScale = patch.widthScale;
    if (patch.rollDegrees) into.rollDegrees = patch.rollDegrees;
}
void applyPoint(procgen::road::RoadControlPoint& point, const RoadPointOverrides& patch) {
    if (patch.rollCenterOffset) point.rollCenterOffset = *patch.rollCenterOffset;
    if (patch.circularFromEnd) point.circularFromEnd = *patch.circularFromEnd;
    if (patch.alignStraightStart) point.alignStraightStart = *patch.alignStraightStart;
    if (patch.alignStraightEnd) point.alignStraightEnd = *patch.alignStraightEnd;
    if (patch.pathControl) point.pathControl = *patch.pathControl;
    if (patch.splineStrength) point.splineStrength = *patch.splineStrength;
    if (patch.widthScale) point.widthScale = *patch.widthScale;
    if (patch.rollDegrees) point.rollDegrees = *patch.rollDegrees;
}
V encodePoints(const RoadPointOverrides& patch) {
    V::Object out;
    if (patch.rollCenterOffset) out.emplace("rollCenterOffset", *patch.rollCenterOffset);
    if (patch.circularFromEnd) out.emplace("circularFromEnd", *patch.circularFromEnd);
    if (patch.alignStraightStart) out.emplace("alignStraightStart", *patch.alignStraightStart);
    if (patch.alignStraightEnd) out.emplace("alignStraightEnd", *patch.alignStraightEnd);
    if (patch.pathControl) out.emplace("pathControl", std::int64_t{static_cast<int>(*patch.pathControl)});
    if (patch.splineStrength) out.emplace("splineStrength", *patch.splineStrength);
    if (patch.widthScale) out.emplace("widthScale", *patch.widthScale);
    if (patch.rollDegrees) out.emplace("rollDegrees", *patch.rollDegrees);
    return out;
}
eve::Result<RoadPointOverrides> decodePoints(const V* value) {
    RoadPointOverrides patch;
    if (!value) return eve::Result<RoadPointOverrides>::success(patch);
    if (!value->getIf<V::Object>()) return fail<RoadPointOverrides>("marker overrides must be an object");
    if (detail::field(*value, "circularFromEnd")) {
        bool fromEnd = false;
        if (!detail::readOptionalBool(*value, "circularFromEnd", fromEnd))
            return fail<RoadPointOverrides>("invalid circular reference");
        patch.circularFromEnd = fromEnd;
    }
    float number = 0;
    if (detail::field(*value, "rollCenterOffset")) {
        if (!detail::readNumber(*value, "rollCenterOffset", number))
            return fail<RoadPointOverrides>("invalid marker roll center");
        patch.rollCenterOffset = number;
    }
    bool flag = false;
    if (detail::field(*value, "alignStraightStart")) {
        if (!detail::readOptionalBool(*value, "alignStraightStart", flag))
            return fail<RoadPointOverrides>("invalid start alignment");
        patch.alignStraightStart = flag;
    }
    if (detail::field(*value, "alignStraightEnd")) {
        if (!detail::readOptionalBool(*value, "alignStraightEnd", flag))
            return fail<RoadPointOverrides>("invalid end alignment");
        patch.alignStraightEnd = flag;
    }
    if (detail::field(*value, "pathControl")) {
        int control = 0;
        if (!detail::readSmallInt(*value, "pathControl", 0, 3, control))
            return fail<RoadPointOverrides>("invalid outgoing controller");
        patch.pathControl = static_cast<procgen::road::RoadPathControl>(control);
    }
    if (detail::field(*value, "splineStrength")) {
        if (!detail::readNumber(*value, "splineStrength", number))
            return fail<RoadPointOverrides>("invalid marker spline strength");
        patch.splineStrength = number;
    }
    if (detail::field(*value, "widthScale")) {
        if (!detail::readNumber(*value, "widthScale", number)) return fail<RoadPointOverrides>("invalid marker width");
        patch.widthScale = number;
    }
    if (detail::field(*value, "rollDegrees")) {
        if (!detail::readNumber(*value, "rollDegrees", number)) return fail<RoadPointOverrides>("invalid marker roll");
        patch.rollDegrees = number;
    }
    return eve::Result<RoadPointOverrides>::success(std::move(patch));
}
void applyFacility(procgen::road::RoadContinuousFacility& facility, const RoadFacilityOverrides& patch) {
    if (patch.start) facility.start = *patch.start;
    if (patch.end) facility.end = *patch.end;
    if (patch.lateralOffset) facility.lateralOffset = *patch.lateralOffset;
    if (patch.verticalOffset) facility.verticalOffset = *patch.verticalOffset;
    if (patch.uvMeters) facility.uvMeters = *patch.uvMeters;
    if (patch.capStart) facility.capStart = *patch.capStart;
    if (patch.capEnd) facility.capEnd = *patch.capEnd;
    if (patch.profile) facility.profile = *patch.profile;
    if (patch.prefab) facility.prefab = *patch.prefab;
}
V encodeFacility(const RoadFacilityOverrides& patch) {
    procgen::road::RoadContinuousFacility facility;
    facility.id = patch.id;
    applyFacility(facility, patch);
    RoadStyleOverrides fields;
    fields.facilities   = std::vector<procgen::road::RoadContinuousFacility>{facility};
    auto        encoded = encodePatch(fields);
    const auto& full    = *encoded.getIf<V::Object>()->at("facilities").getIf<V::Array>()->at(0).getIf<V::Object>();
    V::Object   out{{"id", static_cast<std::int64_t>(patch.id)}};
    if (patch.start) out.emplace("start", full.at("start"));
    if (patch.end) out.emplace("end", full.at("end"));
    if (patch.lateralOffset) out.emplace("lateralOffset", full.at("lateralOffset"));
    if (patch.verticalOffset) out.emplace("verticalOffset", full.at("verticalOffset"));
    if (patch.uvMeters) out.emplace("uvMeters", full.at("uvMeters"));
    if (patch.capStart) out.emplace("capStart", full.at("capStart"));
    if (patch.capEnd) out.emplace("capEnd", full.at("capEnd"));
    if (patch.profile) out.emplace("profile", full.at("profile"));
    if (patch.prefab) out.emplace("prefab", full.at("prefab"));
    return out;
}
eve::Result<RoadFacilityOverrides> decodeFacility(const V& value) {
    const auto*           object = value.getIf<V::Object>();
    RoadFacilityOverrides patch;
    if (!object || !detail::readId(value, "id", patch.id))
        return fail<RoadFacilityOverrides>("invalid facility override identity");
    RoadStyleOverrides                    fields;
    procgen::road::RoadContinuousFacility baseline;
    baseline.id       = patch.id;
    fields.facilities = std::vector<procgen::road::RoadContinuousFacility>{baseline};
    auto  encoded     = encodePatch(fields);
    auto& merged      = *encoded.getIf<V::Object>()->at("facilities").getIf<V::Array>()->at(0).getIf<V::Object>();
    for (const auto& [key, entry] : *object)
        if (merged.contains(key)) merged[key] = entry;
    auto decoded = decodePatch(encoded);
    if (!decoded.ok()) return eve::Result<RoadFacilityOverrides>::failure(decoded.status());
    const auto& facility = decoded.value().facilities->at(0);
    if (object->contains("start")) patch.start = facility.start;
    if (object->contains("end")) patch.end = facility.end;
    if (object->contains("lateralOffset")) patch.lateralOffset = facility.lateralOffset;
    if (object->contains("verticalOffset")) patch.verticalOffset = facility.verticalOffset;
    if (object->contains("uvMeters")) patch.uvMeters = facility.uvMeters;
    if (object->contains("capStart")) patch.capStart = facility.capStart;
    if (object->contains("capEnd")) patch.capEnd = facility.capEnd;
    if (object->contains("profile")) patch.profile = facility.profile;
    if (object->contains("prefab")) patch.prefab = facility.prefab;
    return eve::Result<RoadFacilityOverrides>::success(std::move(patch));
}
V encodeBinding(const RoadTypeBinding& binding) {
    V::Array markers, facilities;
    for (const auto& marker : binding.markers)
        markers.emplace_back(V::Object{{"markerIndex", static_cast<std::int64_t>(marker.markerIndex)},
                                       {"overrides", encodePoints(marker.overrides)}});
    for (const auto& facility : binding.facilities) facilities.push_back(encodeFacility(facility));
    return V::Object{{"edgeId", static_cast<std::int64_t>(binding.edgeId)},
                     {"typeId", binding.typeId},
                     {"overrides", encodePatch(binding.overrides)},
                     {"pointDefaults", encodePoints(binding.pointDefaults)},
                     {"markers", std::move(markers)},
                     {"facilities", std::move(facilities)}};
}
struct State {
    RoadPresetDraft draft;
    RoadNetwork     resolved;
};
eve::Result<std::shared_ptr<State>> resolve(RoadPresetDraft draft) {
    if (draft.types.size() > 256 || draft.bindings.size() > 4096 || draft.source.edges().size() > 4096 ||
        draft.source.nodes().size() > 4096 || draft.metadata.size() > 4096)
        return fail<std::shared_ptr<State>>("author graph exceeds preset document budget");
    for (const auto& edge : draft.source.edges())
        if (edge.controlPoints.size() > 4096)
            return fail<std::shared_ptr<State>>("source marker count exceeds the persistent codec budget");
    auto valid = draft.source.validate();
    if (!valid.ok()) return eve::Result<std::shared_ptr<State>>::failure(valid.status());
    std::map<std::string, const RoadTypePreset*> definitions;
    for (const auto& type : draft.types) {
        if (type.id.empty() || type.id.size() > 256 || type.parentId.size() > 256 ||
            !definitions.emplace(type.id, &type).second)
            return fail<std::shared_ptr<State>>("invalid or duplicate preset identity");
    }
    std::map<std::string, RoadStyle>                          styles;
    std::map<std::string, RoadPointOverrides>                 pointDefaults;
    std::set<std::string>                                     visiting;
    std::function<eve::Result<RoadStyle>(const std::string&)> visit =
        [&](const std::string& id) -> eve::Result<RoadStyle> {
        if (const auto found = styles.find(id); found != styles.end())
            return eve::Result<RoadStyle>::success(found->second);
        const auto found = definitions.find(id);
        if (found == definitions.end() || !visiting.insert(id).second)
            return fail<RoadStyle>("missing preset parent or inheritance cycle");
        RoadStyle style;
        if (!found->second->parentId.empty()) {
            auto parent = visit(found->second->parentId);
            if (!parent.ok()) return parent;
            style = std::move(parent).takeValue();
        }
        apply(style, found->second->overrides);
        RoadPointOverrides points;
        if (!found->second->parentId.empty()) points = pointDefaults.at(found->second->parentId);
        mergePoints(points, found->second->pointDefaults);
        // Validate even unused presets through the same native road style boundary.
        auto probe = RoadNetwork::makeStraight(36.f, 2);
        if (!probe.ok()) return eve::Result<RoadStyle>::failure(probe.status());
        auto checked = probe.value().setEdgeStyle(probe.value().edges()[0].id, style);
        if (!checked.ok()) return eve::Result<RoadStyle>::failure(checked.status());
        auto probePoints = probe.value().edges()[0].controlPoints;
        for (auto& point : probePoints) applyPoint(point, points);
        auto checkedPoints = probe.value().setEdgeControlPoints(probe.value().edges()[0].id, std::move(probePoints));
        if (!checkedPoints.ok()) return eve::Result<RoadStyle>::failure(checkedPoints.status());
        pointDefaults.emplace(id, points);
        visiting.erase(id);
        styles.emplace(id, style);
        return eve::Result<RoadStyle>::success(std::move(style));
    };
    for (const auto& [id, definition] : definitions) {
        (void)definition;
        auto checked = visit(id);
        if (!checked.ok()) return eve::Result<std::shared_ptr<State>>::failure(checked.status());
    }
    auto                                 edges = draft.source.edges();
    std::map<std::uint32_t, std::size_t> edgeIndices;
    for (std::size_t i = 0; i < edges.size(); ++i) edgeIndices.emplace(edges[i].id, i);
    std::set<std::uint32_t> bound;
    for (const auto& binding : draft.bindings) {
        if (!bound.insert(binding.edgeId).second || !styles.contains(binding.typeId))
            return fail<std::shared_ptr<State>>("duplicate binding or missing preset identity");
        auto style = styles.at(binding.typeId);
        apply(style, binding.overrides);
        if (binding.facilities.size() > 64 || binding.markers.size() > 4096)
            return fail<std::shared_ptr<State>>("binding override budget exceeded");
        std::set<std::uint32_t> facilityIds;
        for (const auto& patch : binding.facilities) {
            auto found = std::find_if(style.facilities.begin(), style.facilities.end(),
                                      [&](const auto& facility) { return facility.id == patch.id; });
            if (found == style.facilities.end() || !facilityIds.insert(patch.id).second)
                return fail<std::shared_ptr<State>>("missing or duplicate facility override identity");
            applyFacility(*found, patch);
        }
        if (!edgeIndices.contains(binding.edgeId))
            return fail<std::shared_ptr<State>>("binding refers to a missing source edge");
        auto& edge     = edges[edgeIndices.at(binding.edgeId)];
        auto  points   = edge.controlPoints;
        auto  defaults = pointDefaults.at(binding.typeId);
        mergePoints(defaults, binding.pointDefaults);
        for (auto& point : points) applyPoint(point, defaults);
        std::set<std::uint32_t> markerIds;
        for (const auto& marker : binding.markers) {
            if (marker.markerIndex >= points.size() || !markerIds.insert(marker.markerIndex).second)
                return fail<std::shared_ptr<State>>("missing or duplicate marker override index");
            applyPoint(points[marker.markerIndex], marker.overrides);
        }
        edge.controlPoints = std::move(points);
        edge.style         = std::move(style);
    }
    std::set<std::uint32_t> metadataIds;
    for (const auto& item : draft.metadata) {
        auto edge = draft.source.edgeResult(item.edgeId);
        if (!edge.ok()) return eve::Result<std::shared_ptr<State>>::failure(edge.status());
        if (item.name.size() > 256 || !metadataIds.insert(item.edgeId).second)
            return fail<std::shared_ptr<State>>("invalid author metadata");
    }
    // Construct complete final records before checking cross-edge seams; sequential setters
    // would reject a valid combined width/profile update against intermediate geometry.
    RoadNetwork resolved;
    for (const auto& node : draft.source.nodes()) {
        auto restored = resolved.restoreNode(node);
        if (!restored.ok()) return eve::Result<std::shared_ptr<State>>::failure(restored.status());
    }
    for (auto& edge : edges) {
        auto restored = resolved.restoreEdge(std::move(edge));
        if (!restored.ok()) return eve::Result<std::shared_ptr<State>>::failure(restored.status());
    }
    for (const auto& link : draft.source.laneLinks()) {
        auto restored = resolved.addLaneLink(link);
        if (!restored.ok()) return eve::Result<std::shared_ptr<State>>::failure(restored.status());
    }
    for (const auto& link : draft.source.blockedLaneLinks()) {
        auto restored = resolved.blockLaneLink(link);
        if (!restored.ok()) return eve::Result<std::shared_ptr<State>>::failure(restored.status());
    }
    auto checked = resolved.validate();
    if (!checked.ok()) return eve::Result<std::shared_ptr<State>>::failure(checked.status());
    std::sort(draft.types.begin(), draft.types.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(draft.metadata.begin(), draft.metadata.end(),
              [](const auto& a, const auto& b) { return a.edgeId < b.edgeId; });
    std::sort(draft.bindings.begin(), draft.bindings.end(),
              [](const auto& a, const auto& b) { return a.edgeId < b.edgeId; });
    for (auto& binding : draft.bindings) {
        std::sort(binding.markers.begin(), binding.markers.end(),
                  [](const auto& a, const auto& b) { return a.markerIndex < b.markerIndex; });
        std::sort(binding.facilities.begin(), binding.facilities.end(),
                  [](const auto& a, const auto& b) { return a.id < b.id; });
    }
    return eve::Result<std::shared_ptr<State>>::success(
        std::make_shared<State>(State{std::move(draft), std::move(resolved)}));
}
V encodeDraft(const RoadPresetDraft& draft) {
    auto                  source = draft.source;
    RoadNetworkEditTarget target("road:preset-codec", source);
    V::Array              types, bindings;
    for (const auto& type : draft.types)
        types.emplace_back(V::Object{{"id", type.id},
                                     {"parentId", type.parentId},
                                     {"overrides", encodePatch(type.overrides)},
                                     {"pointDefaults", encodePoints(type.pointDefaults)}});
    for (const auto& binding : draft.bindings) bindings.push_back(encodeBinding(binding));
    V::Array metadata;
    for (const auto& item : draft.metadata)
        metadata.emplace_back(V::Object{
            {"edgeId", static_cast<std::int64_t>(item.edgeId)}, {"name", item.name}, {"locked", item.locked}});
    return V::Object{{"source", target.snapshotValue()},
                     {"types", std::move(types)},
                     {"bindings", std::move(bindings)},
                     {"metadata", std::move(metadata)}};
}
eve::Result<RoadPresetDraft> decodeDraft(const V& value) {
    const auto* source       = detail::field(value, "source");
    const auto* types        = detail::field(value, "types");
    const auto* bindings     = detail::field(value, "bindings");
    const auto* typeArray    = types ? types->getIf<V::Array>() : nullptr;
    const auto* bindingArray = bindings ? bindings->getIf<V::Array>() : nullptr;
    if (!source || !typeArray || !bindingArray || typeArray->size() > 256 || bindingArray->size() > 4096)
        return fail<RoadPresetDraft>("invalid preset document frame");
    RoadPresetDraft       draft;
    RoadNetworkEditTarget target("road:preset-codec", draft.source);
    auto                  loaded = target.loadSnapshot(*source);
    if (!loaded.ok()) return fail<RoadPresetDraft>("invalid author source snapshot");
    for (const auto& encoded : *typeArray) {
        const auto* id     = detail::field(encoded, "id");
        const auto* parent = detail::field(encoded, "parentId");
        const auto* patch  = detail::field(encoded, "overrides");
        if (!id || !id->getIf<std::string>() || !parent || !parent->getIf<std::string>() || !patch)
            return fail<RoadPresetDraft>("invalid type record");
        auto decoded = decodePatch(*patch);
        if (!decoded.ok()) return eve::Result<RoadPresetDraft>::failure(decoded.status());
        draft.types.push_back(
            {*id->getIf<std::string>(), *parent->getIf<std::string>(), std::move(decoded).takeValue()});
    }
    for (const auto& encoded : *bindingArray) {
        std::uint32_t edgeId = 0;
        const auto*   id     = detail::field(encoded, "typeId");
        const auto*   patch  = detail::field(encoded, "overrides");
        if (!detail::readId(encoded, "edgeId", edgeId) || !id || !id->getIf<std::string>() || !patch)
            return fail<RoadPresetDraft>("invalid binding record");
        auto decoded = decodePatch(*patch);
        if (!decoded.ok()) return eve::Result<RoadPresetDraft>::failure(decoded.status());
        draft.bindings.push_back({edgeId, *id->getIf<std::string>(), std::move(decoded).takeValue()});
    }
    for (std::size_t i = 0; i < typeArray->size(); ++i) {
        auto points = decodePoints(detail::field((*typeArray)[i], "pointDefaults"));
        if (!points.ok()) return eve::Result<RoadPresetDraft>::failure(points.status());
        draft.types[i].pointDefaults = std::move(points).takeValue();
    }
    for (std::size_t i = 0; i < bindingArray->size(); ++i) {
        const auto& encoded = (*bindingArray)[i];
        auto&       binding = draft.bindings[i];
        auto        points  = decodePoints(detail::field(encoded, "pointDefaults"));
        if (!points.ok()) return eve::Result<RoadPresetDraft>::failure(points.status());
        binding.pointDefaults = std::move(points).takeValue();
        if (const auto* markers = detail::field(encoded, "markers")) {
            const auto* array = markers->getIf<V::Array>();
            if (!array || array->size() > 4096) return fail<RoadPresetDraft>("invalid marker override array");
            for (const auto& marker : *array) {
                int index = 0;
                if (!detail::readSmallInt(marker, "markerIndex", 0, 4095, index))
                    return fail<RoadPresetDraft>("invalid marker override index");
                auto patch = decodePoints(detail::field(marker, "overrides"));
                if (!patch.ok()) return eve::Result<RoadPresetDraft>::failure(patch.status());
                binding.markers.push_back({static_cast<std::uint32_t>(index), std::move(patch).takeValue()});
            }
        }
        if (const auto* facilities = detail::field(encoded, "facilities")) {
            const auto* array = facilities->getIf<V::Array>();
            if (!array || array->size() > 64) return fail<RoadPresetDraft>("invalid facility override array");
            for (const auto& facility : *array) {
                auto patch = decodeFacility(facility);
                if (!patch.ok()) return eve::Result<RoadPresetDraft>::failure(patch.status());
                binding.facilities.push_back(std::move(patch).takeValue());
            }
        }
    }
    if (const auto* metadata = detail::field(value, "metadata")) {
        const auto* array = metadata->getIf<V::Array>();
        if (!array || array->size() > 4096) return fail<RoadPresetDraft>("invalid author metadata array");
        for (const auto& entry : *array) {
            RoadAuthorMetadata item;
            const auto*        name = detail::field(entry, "name");
            if (!detail::readId(entry, "edgeId", item.edgeId) || !name || !name->getIf<std::string>() ||
                !detail::readOptionalBool(entry, "locked", item.locked))
                return fail<RoadPresetDraft>("invalid author metadata record");
            item.name = *name->getIf<std::string>();
            draft.metadata.push_back(std::move(item));
        }
    }
    return eve::Result<RoadPresetDraft>::success(std::move(draft));
}
}  // namespace
struct PreparedRoadPresetEdit::Impl {
    std::shared_ptr<const int>          owner;
    std::uint64_t                       revision = 0;
    std::vector<std::shared_ptr<State>> states;
    std::size_t                         cursor  = 0;
    bool                                restore = false;
};
struct RoadPresetDocument::Impl {
    std::shared_ptr<const int>          owner    = std::make_shared<const int>(0);
    std::uint64_t                       revision = 0;
    std::vector<std::shared_ptr<State>> history;
    std::size_t                         cursor = 0;
};
PreparedRoadPresetEdit::PreparedRoadPresetEdit(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedRoadPresetEdit::~PreparedRoadPresetEdit()                                            = default;
PreparedRoadPresetEdit::PreparedRoadPresetEdit(PreparedRoadPresetEdit&&) noexcept            = default;
PreparedRoadPresetEdit& PreparedRoadPresetEdit::operator=(PreparedRoadPresetEdit&&) noexcept = default;
RoadPresetDocument::RoadPresetDocument(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
RoadPresetDocument::~RoadPresetDocument() = default;
eve::Result<std::unique_ptr<RoadPresetDocument>> RoadPresetDocument::create(RoadPresetDraft draft) {
    auto state = resolve(std::move(draft));
    if (!state.ok()) return eve::Result<std::unique_ptr<RoadPresetDocument>>::failure(state.status());
    auto impl = std::make_unique<Impl>();
    impl->history.push_back(std::move(state).takeValue());
    return eve::Result<std::unique_ptr<RoadPresetDocument>>::success(
        std::unique_ptr<RoadPresetDocument>(new RoadPresetDocument(std::move(impl))));
}
RoadPresetDraft    RoadPresetDocument::beginEdit() const { return impl_->history[impl_->cursor]->draft; }
const RoadNetwork& RoadPresetDocument::network() const { return impl_->history[impl_->cursor]->resolved; }
eve::Result<PreparedRoadPresetEdit> RoadPresetDocument::prepare(RoadPresetDraft draft) const {
    if (impl_->cursor + 1 >= maximumHistory) return fail<PreparedRoadPresetEdit>("preset undo history budget exceeded");
    const auto& previous = impl_->history[impl_->cursor]->draft;
    for (const auto& item : previous.metadata) {
        if (!item.locked) continue;
        const auto nextMeta = std::find_if(draft.metadata.begin(), draft.metadata.end(),
                                           [&](const auto& next) { return next.edgeId == item.edgeId; });
        if (nextMeta == draft.metadata.end())
            return fail<PreparedRoadPresetEdit>("locked edge metadata must be explicitly unlocked");
        if (!nextMeta->locked) continue;
        auto oldEdge = previous.source.edgeResult(item.edgeId);
        if (!oldEdge.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(oldEdge.status());
        auto nextEdge = draft.source.edgeResult(item.edgeId);
        if (!nextEdge.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(nextEdge.status());
        if (detail::encodeEdgeRecord(oldEdge.value(), {}) != detail::encodeEdgeRecord(nextEdge.value(), {}))
            return fail<PreparedRoadPresetEdit>("locked road source cannot be directly edited");
        for (const auto nodeId : {oldEdge.value().from, oldEdge.value().to}) {
            auto oldNode = previous.source.nodeResult(nodeId);
            if (!oldNode.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(oldNode.status());
            auto nextNode = draft.source.nodeResult(nodeId);
            if (!nextNode.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(nextNode.status());
            if (detail::encodeNodeRecord(oldNode.value()) != detail::encodeNodeRecord(nextNode.value()))
                return fail<PreparedRoadPresetEdit>("locked road endpoint cannot be directly edited");
        }
        const auto oldBinding = std::find_if(previous.bindings.begin(), previous.bindings.end(),
                                             [&](const auto& binding) { return binding.edgeId == item.edgeId; });
        const auto newBinding = std::find_if(draft.bindings.begin(), draft.bindings.end(),
                                             [&](const auto& binding) { return binding.edgeId == item.edgeId; });
        if ((oldBinding == previous.bindings.end()) != (newBinding == draft.bindings.end()) ||
            (oldBinding != previous.bindings.end() && encodeBinding(*oldBinding) != encodeBinding(*newBinding)))
            return fail<PreparedRoadPresetEdit>("locked road binding cannot be directly edited");
    }
    auto state = resolve(std::move(draft));
    if (!state.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(state.status());
    auto impl      = std::make_unique<PreparedRoadPresetEdit::Impl>();
    impl->owner    = impl_->owner;
    impl->revision = impl_->revision;
    impl->states.push_back(std::move(state).takeValue());
    return eve::Result<PreparedRoadPresetEdit>::success(PreparedRoadPresetEdit(std::move(impl)));
}
eve::Result<void> RoadPresetDocument::commit(PreparedRoadPresetEdit edit) {
    if (!edit.impl_) return fail<void>("consumed preset edit");
    if (edit.impl_->owner != impl_->owner)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "preset edit belongs to another document", "road.presets"));
    if (edit.impl_->revision != impl_->revision)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "preset document revision changed", "road.presets"));
    if (edit.impl_->restore) {
        impl_->history.swap(edit.impl_->states);
        impl_->cursor = edit.impl_->cursor;
    } else {
        std::vector<std::shared_ptr<State>> next(
            impl_->history.begin(), impl_->history.begin() + static_cast<std::ptrdiff_t>(impl_->cursor + 1));
        next.push_back(std::move(edit.impl_->states.front()));
        impl_->history.swap(next);
        ++impl_->cursor;
    }
    ++impl_->revision;
    return eve::Result<void>::success();
}
eve::Result<void> RoadPresetDocument::undo() {
    if (impl_->cursor == 0) return fail<void>("no preset undo state");
    --impl_->cursor;
    ++impl_->revision;
    return eve::Result<void>::success();
}
eve::Result<void> RoadPresetDocument::redo() {
    if (impl_->cursor + 1 >= impl_->history.size()) return fail<void>("no preset redo state");
    ++impl_->cursor;
    ++impl_->revision;
    return eve::Result<void>::success();
}
eve::Result<eve::Value> RoadPresetDocument::capture() const {
    V::Array history;
    for (const auto& state : impl_->history) history.push_back(encodeDraft(state->draft));
    V value = V::Object{{"schema", "eve.procgen.roadPresets"},
                        {"schemaVersion", std::int64_t{3}},
                        {"history", std::move(history)},
                        {"cursor", static_cast<std::int64_t>(impl_->cursor)}};
    if (!value.isWithinLimits(64, 262144, maximumJsonBytes))
        return fail<eve::Value>("preset snapshot exceeds tree budget");
    auto canonical = editing::toPresentationValue(value);
    auto json      = canonical.toJson();
    if (!json.ok()) return eve::Result<eve::Value>::failure(json.status());
    if (json.value().size() > maximumJsonBytes) return fail<eve::Value>("preset snapshot exceeds JSON budget");
    return eve::Result<eve::Value>::success(std::move(canonical));
}
eve::Result<PreparedRoadPresetEdit> RoadPresetDocument::prepareRestore(const eve::Value& canonical) const {
    if (!boundedValue(canonical)) return fail<PreparedRoadPresetEdit>("preset snapshot exceeds tree budget");
    auto json = canonical.toJson();
    if (!json.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(json.status());
    if (json.value().size() > maximumJsonBytes)
        return fail<PreparedRoadPresetEdit>("preset snapshot exceeds JSON budget");
    const auto  value   = editing::toEditingValue(canonical);
    const auto* schema  = detail::field(value, "schema");
    const auto* version = detail::field(value, "schemaVersion");
    const auto* history = detail::field(value, "history");
    const auto* cursor  = detail::field(value, "cursor");
    const auto* array   = history ? history->getIf<V::Array>() : nullptr;
    if (!schema || !schema->getIf<std::string>() || *schema->getIf<std::string>() != "eve.procgen.roadPresets" ||
        !version || !version->getIf<std::int64_t>() ||
        (*version->getIf<std::int64_t>() < 1 || *version->getIf<std::int64_t>() > 3) || !array || array->empty() ||
        array->size() > maximumHistory || !cursor || !cursor->getIf<std::int64_t>() ||
        *cursor->getIf<std::int64_t>() < 0 || static_cast<std::size_t>(*cursor->getIf<std::int64_t>()) >= array->size())
        return fail<PreparedRoadPresetEdit>("unsupported or invalid preset snapshot");
    auto prepared      = std::make_unique<PreparedRoadPresetEdit::Impl>();
    prepared->owner    = impl_->owner;
    prepared->revision = impl_->revision;
    prepared->restore  = true;
    prepared->cursor   = static_cast<std::size_t>(*cursor->getIf<std::int64_t>());
    for (const auto& frame : *array) {
        auto draft = decodeDraft(frame);
        if (!draft.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(draft.status());
        if (*version->getIf<std::int64_t>() == 1) {
            for (auto& type : draft.value().types)
                if (type.parentId.empty() && !type.overrides.rollInterpolation)
                    type.overrides.rollInterpolation = procgen::road::RoadRollInterpolation::Linear;
        }
        if (*version->getIf<std::int64_t>() < 3) {
            for (auto& type : draft.value().types) {
                if (!type.parentId.empty()) continue;
                if (!type.overrides.samplingSpacingMeters) type.overrides.samplingSpacingMeters = 0.f;
                if (!type.overrides.samplingAngleDegrees) type.overrides.samplingAngleDegrees = 4.f;
                if (!type.overrides.samplingPolicy)
                    type.overrides.samplingPolicy = procgen::road::RoadSamplingPolicy::MaximumIntervals;
            }
        }
        auto state = resolve(std::move(draft).takeValue());
        if (!state.ok()) return eve::Result<PreparedRoadPresetEdit>::failure(state.status());
        prepared->states.push_back(std::move(state).takeValue());
    }
    return eve::Result<PreparedRoadPresetEdit>::success(PreparedRoadPresetEdit(std::move(prepared)));
}
}  // namespace eve::procgen_editing
