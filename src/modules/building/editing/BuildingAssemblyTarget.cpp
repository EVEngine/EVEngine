#include "building/editing/BuildingAssemblyTarget.h"
#include "building/BuildingAssembly.h"
#include "building/PlacementWorld.h"
#include "editing/EditingProperty.h"
namespace eve::building_editing {
struct BuildingAssemblyTarget::State {
    std::string                                      id;
    std::reference_wrapper<building::PlacementWorld> world;
    std::unique_ptr<building::PlacementWorld>        owned;
    Value                                            expected;
};
BuildingAssemblyTarget::BuildingAssemblyTarget(std::string id, building::PlacementWorld& world)
    : state_(std::make_unique<State>(State{std::move(id), world, {}, {}})) {}
BuildingAssemblyTarget::~BuildingAssemblyTarget() = default;
editing::TargetId         BuildingAssemblyTarget::targetId() const { return editing::TargetId(state_->id); }
editing::TargetDescriptor BuildingAssemblyTarget::describe() const {
    return {targetId(), "building.assembly", revision(), false, {}};
}
editing::Result<editing::DomainOperation> BuildingAssemblyTarget::makeOperation(
    building::PreparedAssembly prepared) const {
    auto candidate = state_->world.get().cloneState();
    auto committed = building::AssemblyService::commit(*candidate, std::move(prepared));
    if (!committed.ok()) return editing::Result<editing::DomainOperation>::failure(committed.status());
    auto                     receipt = std::move(committed).takeValue();
    editing::DomainOperation op;
    op.target      = targetId();
    op.type        = "building.assembly.exchange.v1";
    op.inverseType = op.type;
    op.hasInverse  = true;
    op.payload     = editing::toEditingValue(Value::Object{{"expected", receipt.before}, {"desired", receipt.after}});
    op.inverse     = editing::toEditingValue(
        Value::Object{{"expected", std::move(receipt.after)}, {"desired", std::move(receipt.before)}});
    op.affectedObjects.push_back({targetId(), receipt.assembly.format(), 0});
    return editing::Result<editing::DomainOperation>::success(std::move(op));
}
editing::Result<void> BuildingAssemblyTarget::applyDomainOperation(const editing::DomainOperation& op) {
    if (op.target != targetId() || op.type != "building.assembly.exchange.v1")
        return editing::Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "Wrong house operation target/type"));
    auto value    = editing::toPresentationValue(op.payload);
    auto expected = value.find("expected"), desired = value.find("desired");
    if (!expected || !desired)
        return editing::Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Missing house state endpoints"));
    auto result = building::AssemblyService::applyReceipt(state_->world.get(), *expected, *desired);
    if (result.ok()) markDirty();
    return result;
}
std::unique_ptr<editing::IDomainOperationTarget> BuildingAssemblyTarget::cloneDomainState() const {
    auto before = building::AssemblyService::snapshotWorld(state_->world.get());
    if (!before.ok()) return {};
    auto world             = state_->world.get().cloneState();
    auto copy              = std::make_unique<BuildingAssemblyTarget>(state_->id, *world);
    copy->state_->owned    = std::move(world);
    copy->state_->expected = std::move(before).takeValue();
    copy->setRevision(revision());
    return copy;
}
editing::Result<void> BuildingAssemblyTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* c = dynamic_cast<BuildingAssemblyTarget*>(candidate.get());
    if (!c || !c->state_->owned || c->targetId() != targetId())
        return editing::Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "Invalid house staging target"));
    auto desired = building::AssemblyService::snapshotWorld(*c->state_->owned);
    if (!desired.ok()) return editing::Result<void>::failure(desired.status());
    auto result = building::AssemblyService::applyReceipt(state_->world.get(), c->state_->expected, desired.value());
    if (result.ok()) markDirty();
    return result;
}
}  // namespace eve::building_editing
