#pragma once
#include "building/BuildingSnapshot.h"
#include "building/editing/BuildingTarget.h"
#include "editing/EditingProperty.h"
namespace eve::building_editing::target_detail {
inline EditorValue instanceValue(const BuildingInstanceSnapshot& p) {
    return editing::toEditingValue(building::codec::instanceValue(p));
}
inline EditorValue edgeCurveValue(const building::EdgeCurveGroup& g, const std::vector<BuildingInstanceSnapshot>& p) {
    return editing::toEditingValue(building::codec::edgeCurveValue(g, p));
}
inline auto parseInstance(const EditorValue& v) {
    return building::codec::parseInstance(editing::toPresentationValue(v));
}
inline auto parseInstances(const EditorValue& v) {
    return building::codec::parseInstances(editing::toPresentationValue(v));
}
inline auto parseEdgeCurve(const EditorValue& v) {
    return building::codec::parseEdgeCurve(editing::toPresentationValue(v));
}
DomainOperation instanceOperation(const std::string& target, std::string type, std::string inverseType,
                                  const BuildingInstanceSnapshot& payload, const BuildingInstanceSnapshot& inverse);
}  // namespace eve::building_editing::target_detail
