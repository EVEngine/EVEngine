#pragma once

#include "building/BuildingAssembly.h"
#include "building/PlacementWorld.h"

namespace eve::building {
// Private owner state; no renderer or editor identity is authoritative here.
struct AssemblyState {
    uint64_t                                                    epoch    = 0;
    uint32_t                                                    nextSlot = 0;
    std::map<AssemblyId, BuildingAssembly>                      records;
    std::map<AssemblyId, eve::RuntimeHandle<AssemblyHandleTag>> handles;
};
struct PreparedAssemblyData {
    Value                           admission;
    uint64_t                        epoch = 0;
    Value                           before;
    Value                           definitions;
    std::unique_ptr<PlacementWorld> candidate;
    AssemblyId                      id;
    std::vector<PlacedBuilding>     preview;
};
namespace assembly_detail {
uint64_t               nextEpoch();
Value                  definitionValue(const BuildingDefinition& definition);
Value                  definitionsFor(const PlacementWorld& world);
Result<void>           checkDefinitions(const Value& definitions);
Result<void>           checkGrid(const PlacementWorld& world);
Result<PlacedBuilding> transformPart(const PlacedBuilding& part, const PlacementWorld& world, AssemblyPose from,
                                     AssemblyPose to);
Value                  poseValue(AssemblyPose pose);
Result<AssemblyPose>   parsePose(const Value& value);
void                   removeMembership(AssemblyState& state, int member);
template <class T>
Result<T> fail(std::string message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), {}, {}, "building.assembly"));
}
}  // namespace assembly_detail
}  // namespace eve::building
