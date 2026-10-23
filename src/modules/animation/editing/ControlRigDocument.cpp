#include "animation/editing/ControlRigDocument.h"
#include <algorithm>
#include <map>
#include "animation/ControlRig.h"
#include "common/Value.h"
#include "editing/EditingGraph.h"
#include "editing/EditingValueJson.h"

namespace eve::animation_editing {
struct ControlRigDocument::Impl {
    std::string              id;
    animation::RigDefinition definition;
};
ControlRigDocument::ControlRigDocument(std::string id) : impl_(std::make_unique<Impl>()) { impl_->id = std::move(id); }
ControlRigDocument::~ControlRigDocument() = default;
editing::TargetId         ControlRigDocument::targetId() const { return editing::TargetId(impl_->id); }
editing::TargetDescriptor ControlRigDocument::describe() const {
    auto result = editing::IEditableTarget::describe();
    result.type = "control-rig";
    return result;
}
animation::RigDefinition         ControlRigDocument::definition() const { return impl_->definition; }
Result<editing::DomainOperation> ControlRigDocument::replace(const animation::RigDefinition& candidate) const {
    auto encoded = animation::encodeControlRig(candidate);
    if (!encoded.ok()) return Result<editing::DomainOperation>::failure(encoded.status());
    auto inverse = animation::encodeControlRig(impl_->definition);
    if (!inverse.ok()) return Result<editing::DomainOperation>::failure(inverse.status());
    editing::DomainOperation op;
    op.type          = "animation.rig.replace.v1";
    op.inverseType   = op.type;
    op.target        = targetId();
    auto payloadJson = encoded.value().toJson();
    if (!payloadJson.ok()) return Result<editing::DomainOperation>::failure(payloadJson.status());
    auto inverseJson = inverse.value().toJson();
    if (!inverseJson.ok()) return Result<editing::DomainOperation>::failure(inverseJson.status());
    auto payload = editing::editorValueFromJson(payloadJson.value());
    if (!payload.ok()) return Result<editing::DomainOperation>::failure(payload.status());
    auto inverseValue = editing::editorValueFromJson(inverseJson.value());
    if (!inverseValue.ok()) return Result<editing::DomainOperation>::failure(inverseValue.status());
    op.payload    = std::move(payload).takeValue();
    op.inverse    = std::move(inverseValue).takeValue();
    op.hasInverse = true;
    op.affectedObjects.push_back({targetId(), "rig", 0});
    return Result<editing::DomainOperation>::success(std::move(op));
}
Result<void> ControlRigDocument::applyDomainOperation(const editing::DomainOperation& op) {
    if (op.target != targetId() || op.type != "animation.rig.replace.v1")
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "wrong target or operation", "controlRig"));
    if (!op.payload.isWithinLimits(32, 2000000, 16000000))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "rig payload exceeds limits", "controlRig"));
    auto common = Value::fromJson(editing::editorValueToJson(op.payload));
    if (!common.ok()) return Result<void>::failure(common.status());
    auto decoded = animation::decodeControlRig(common.value());
    if (!decoded.ok()) return Result<void>::failure(decoded.status());
    impl_->definition = std::move(decoded).takeValue();
    bumpRevision();
    widenDirty(0, 0);
    return editing::applied<void>();
}
std::unique_ptr<editing::IDomainOperationTarget> ControlRigDocument::cloneDomainState() const {
    auto candidate                                         = std::make_unique<ControlRigDocument>(impl_->id);
    static_cast<editing::EditableTargetState&>(*candidate) = static_cast<const editing::EditableTargetState&>(*this);
    *candidate->impl_                                      = *impl_;
    return candidate;
}
Result<void> ControlRigDocument::commitDomainState(std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* rig = dynamic_cast<ControlRigDocument*>(candidate.get());
    if (!rig || rig->targetId() != targetId())
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "wrong rig candidate", "controlRig"));
    static_cast<editing::EditableTargetState&>(*this) = static_cast<const editing::EditableTargetState&>(*rig);
    impl_                                             = std::move(rig->impl_);
    return editing::applied<void>();
}
Result<editing::GraphDocumentData> ControlRigDocument::graph() const {
    auto encoded = animation::encodeControlRig(impl_->definition);
    if (!encoded.ok()) return Result<editing::GraphDocumentData>::failure(encoded.status());
    editing::GraphDocument model;
    const auto*            nodes = encoded.value().find("nodes");
    for (size_t i = 0; i < impl_->definition.nodes.size(); ++i) {
        const auto& node = impl_->definition.nodes[i];
        auto        text = nodes->at(i).toJson();
        if (!text.ok()) return Result<editing::GraphDocumentData>::failure(text.status());
        auto properties = editing::editorValueFromJson(text.value());
        if (!properties.ok()) return Result<editing::GraphDocumentData>::failure(properties.status());
        editing::GraphNodeRecord record;
        record.id         = editing::GraphNodeId(node.id);
        record.type       = "rig.solve";
        record.properties = std::move(properties).takeValue();
        record.pins       = {
            {editing::GraphPinId(node.id + ":in"), record.id, "rig.execution", editing::GraphPinDirection::Input},
            {editing::GraphPinId(node.id + ":out"), record.id, "rig.execution", editing::GraphPinDirection::Output}};
        auto added = model.createNode(std::move(record));
        if (!added.ok()) return Result<editing::GraphDocumentData>::failure(added.status());
    }
    size_t edgeIndex = 0;
    for (const auto& node : impl_->definition.nodes)
        for (const auto& dependency : node.after) {
            editing::GraphEdgeRecord edge{editing::StableId("edge:" + std::to_string(edgeIndex++)),
                                          editing::GraphPinId(dependency + ":out"),
                                          editing::GraphPinId(node.id + ":in")};
            auto                     connected = model.connect(std::move(edge), {true, {}});
            if (!connected.ok()) return Result<editing::GraphDocumentData>::failure(connected.status());
        }
    auto snapshot     = model.snapshot("animation.control-rig");
    snapshot.revision = revision();
    std::map<std::string, size_t> order;
    for (size_t i = 0; i < impl_->definition.nodes.size(); ++i) order.emplace(impl_->definition.nodes[i].id, i);
    std::sort(snapshot.nodes.begin(), snapshot.nodes.end(),
              [&](const auto& a, const auto& b) { return order.at(a.id.value()) < order.at(b.id.value()); });
    return Result<editing::GraphDocumentData>::success(std::move(snapshot));
}
Result<editing::DomainOperation> ControlRigDocument::replaceGraph(const editing::GraphDocumentData& graph) const {
    auto fail = [](std::string message) {
        return Result<editing::DomainOperation>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "controlRig.graph"));
    };
    if (graph.domain != "animation.control-rig" || graph.schemaVersion != 1 || graph.revision != revision())
        return fail("graph schema or revision mismatch");
    auto encoded = animation::encodeControlRig(impl_->definition);
    if (!encoded.ok()) return Result<editing::DomainOperation>::failure(encoded.status());
    Value                         nodes = Value::array({});
    std::map<std::string, size_t> ids;
    editing::GraphDocument        validation;
    for (const auto& node : graph.nodes) {
        if (node.type != "rig.solve" || !ids.emplace(node.id.value(), nodes.arraySize()).second)
            return fail("invalid graph node");
        if (node.pins.size() != 2 || !node.properties.isWithinLimits(16, 100000, 16000000))
            return fail("invalid solve pins or properties");
        bool input = false, output = false;
        for (const auto& pin : node.pins) {
            if (pin.node != node.id || pin.type != "rig.execution") return fail("invalid solve pin owner or type");
            if (pin.id.value() == node.id.value() + ":in" && pin.direction == editing::GraphPinDirection::Input &&
                !input)
                input = true;
            else if (pin.id.value() == node.id.value() + ":out" &&
                     pin.direction == editing::GraphPinDirection::Output && !output)
                output = true;
            else
                return fail("invalid solve pin");
        }
        auto added = validation.createNode(node);
        if (!added.ok()) return Result<editing::DomainOperation>::failure(added.status());
        auto properties = Value::fromJson(editing::editorValueToJson(node.properties));
        if (!properties.ok()) return Result<editing::DomainOperation>::failure(properties.status());
        auto* id = properties.value().find("id");
        if (!id || !id->isString() || id->asString() != node.id.value()) return fail("node identity mismatch");
        properties.value().set("after", Value::array({}));
        nodes.pushBack(std::move(properties).takeValue());
    }
    for (const auto& edge : graph.edges) {
        auto connected = validation.connect(edge, {true, {}});
        if (!connected.ok()) return Result<editing::DomainOperation>::failure(connected.status());
        std::string from, to;
        for (const auto& [id, index] : ids) {
            (void)index;
            if (edge.from.value() == id + ":out") from = id;
            if (edge.to.value() == id + ":in") to = id;
        }
        if (from.empty() || to.empty()) return fail("edge must connect solve output to input");
        nodes.at(ids.at(to)).find("after")->pushBack(from);
    }
    encoded.value().set("nodes", std::move(nodes));
    auto candidate = animation::decodeControlRig(encoded.value());
    if (!candidate.ok()) return Result<editing::DomainOperation>::failure(candidate.status());
    return replace(candidate.value());
}
}  // namespace eve::animation_editing
