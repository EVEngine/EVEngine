#include "procgen/editing/RoadNetworkEditTargetInternal.inc"

#include <cmath>
#include <limits>

namespace eve::procgen_editing::detail {
using RoadCodecValue = EditorValue;
#include "procgen/road/RoadConnectionCodec.inc"
}  // namespace eve::procgen_editing::detail

namespace eve::procgen_editing {
EditorResult<editing::DomainOperation> RoadNetworkEditTarget::makeSetNodeConnection(
    std::uint32_t nodeId, procgen::road::RoadConnection connection) const {
    using namespace detail;
    auto node = network_->nodeResult(nodeId);
    if (!node.ok()) return roadFailure<editing::DomainOperation>(node.status());
    auto candidate = *network_;
    auto changed   = candidate.setNodeConnection(nodeId, connection);
    if (!changed.ok()) return roadFailure<editing::DomainOperation>(changed.status());
    editing::DomainOperation operation;
    operation.type        = kSetNodeConnection;
    operation.inverseType = kSetNodeConnection;
    operation.target      = targetId();
    operation.payload = EditorValue::Object{{"id", std::int64_t{nodeId}}, {"connection", encodeConnection(connection)}};
    operation.inverse =
        EditorValue::Object{{"id", std::int64_t{nodeId}}, {"connection", encodeConnection(node.value().connection)}};
    operation.hasInverse = true;
    operation.affectedObjects.emplace_back(targetId(), "node/" + std::to_string(nodeId));
    operation.affectedProperties.emplace_back("connection");
    return editing::applied(std::move(operation));
}
}  // namespace eve::procgen_editing
