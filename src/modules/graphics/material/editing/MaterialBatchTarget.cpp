#include "graphics/material/editing/MaterialBatchTarget.h"

#include <algorithm>
#include <optional>
#include <set>

namespace eve::material_editing {
namespace {
const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

SelectionSnapshot one(const MaterialDocumentTarget& material) {
    SelectionSnapshot      result;
    editing::SelectionItem item;
    item.domain = editing::SelectionDomain::Asset;
    item.target = material.targetId();
    item.item   = editing::StableId(material.targetId().value());
    item.type   = "graphics.material";
    result.items.push_back(item);
    result.primary = item;
    return result;
}

EditorValue content(const std::vector<MaterialDocumentTarget>& materials) {
    EditorValue::Array result;
    result.reserve(materials.size());
    for (const auto& material : materials)
        result.emplace_back(
            EditorValue::Object{{"id", material.targetId().value()}, {"snapshot", material.snapshotValue()}});
    return result;
}
}  // namespace

MaterialBatchTarget::MaterialBatchTarget(std::string id, std::vector<MaterialDocumentTarget> materials,
                                         IMaterialBatchRuntimeSink* sink)
    : id_(std::move(id)), materials_(std::move(materials)), sink_(sink) {}

TargetDescriptor MaterialBatchTarget::describe() const {
    return {targetId(),
            "material-batch",
            revisionValue(),
            false,
            {IPropertyProvider::editingCapabilityId(), editing::IEditingSnapshotProvider::editingCapabilityId()}};
}

void* MaterialBatchTarget::queryCapability(const CapabilityId& capability) {
    if (capability == IPropertyProvider::editingCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (capability == editing::IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<editing::IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorResult<std::vector<std::size_t>> MaterialBatchTarget::selectedIndices(const SelectionSnapshot& selection) const {
    if (selection.items.empty())
        return editing::failed<std::vector<std::size_t>>(EditorStatus::Rejected,
                                                         RuleId("editor.material-batch.empty-selection"),
                                                         "Material batch selection is empty");
    std::vector<std::size_t> result;
    std::set<std::size_t>    unique;
    for (const auto& item : selection.items) {
        if (item.target != targetId())
            return editing::failed<std::vector<std::size_t>>(EditorStatus::Rejected,
                                                             RuleId("editor.material-batch.target-mismatch"),
                                                             "Material batch selection targets another document");
        const auto found = std::find_if(materials_.begin(), materials_.end(), [&](const auto& material) {
            return material.targetId().value() == item.item.value();
        });
        if (found == materials_.end())
            return editing::failed<std::vector<std::size_t>>(EditorStatus::NotFound,
                                                             RuleId("editor.material-batch.material-not-found"),
                                                             "Selected material is not in this batch");
        const auto index = static_cast<std::size_t>(found - materials_.begin());
        if (!unique.insert(index).second)
            return editing::failed<std::vector<std::size_t>>(EditorStatus::Rejected,
                                                             RuleId("editor.material-batch.duplicate-selection"),
                                                             "Material batch selection contains a duplicate");
        result.push_back(index);
    }
    return editing::applied<std::vector<std::size_t>>(std::move(result));
}

eve::Result<eve::Revision> MaterialBatchTarget::currentRevision(const SelectionSnapshot& selection) const {
    const auto indices = selectedIndices(selection);
    if (!indices.ok()) return eve::Result<eve::Revision>::failure(indices.status());
    return eve::Result<eve::Revision>::success(eve::Revision(revisionValue()));
}

PropertySchema MaterialBatchTarget::schema(const SelectionSnapshot&) const {
    return materials_.empty() ? PropertySchema{} : materials_.front().schema({});
}

PropertyReadResult MaterialBatchTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    const auto indices = selectedIndices(selection);
    if (!indices.ok()) return {PropertyReadState::Error, {}, indices.status().diagnostics()};
    std::optional<EditorValue> common;
    for (const auto index : indices.value()) {
        const auto value = materials_[index].read(one(materials_[index]), path);
        if (value.state != PropertyReadState::Value) return value;
        if (!common)
            common = value.value;
        else if (*common != value.value)
            return {PropertyReadState::Mixed, {}, {}};
    }
    return {PropertyReadState::Value, *common, {}};
}

EditorResult<DomainOperation> MaterialBatchTarget::replacement(std::vector<MaterialDocumentTarget> candidates,
                                                               const PropertyPath&                 path) const {
    DomainOperation operation;
    operation.type        = "material.batch.replace.v1";
    operation.inverseType = operation.type;
    operation.target      = targetId();
    operation.payload     = EditorValue::Object{{"materials", content(candidates)}};
    operation.inverse     = EditorValue::Object{{"materials", content(materials_)}};
    operation.hasInverse  = true;
    operation.affectedProperties.push_back(path.value());
    operation.mergeKey = "material-batch:" + id_ + ":" + path.value();
    return editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> MaterialBatchTarget::makeSet(const SelectionSnapshot& selection, const PropertyPath& path,
                                                           const EditorValue& value, PropertySetMode mode) const {
    if (mode == PropertySetMode::Reset) return makeReset(selection, path);
    const auto indices = selectedIndices(selection);
    if (!indices.ok()) return EditorResult<DomainOperation>::failure(indices.status());
    auto candidates = materials_;
    for (const auto index : indices.value()) {
        auto operation = candidates[index].makeSet(one(candidates[index]), path, value, mode);
        if (!operation.ok()) return EditorResult<DomainOperation>::failure(operation.status());
        auto applied = candidates[index].applyDomainOperation(operation.value());
        if (!applied.ok()) return EditorResult<DomainOperation>::failure(applied.status());
    }
    return replacement(std::move(candidates), path);
}

EditorResult<DomainOperation> MaterialBatchTarget::makeReset(const SelectionSnapshot& selection,
                                                             const PropertyPath&      path) const {
    const auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return editing::failed<DomainOperation>(EditorStatus::Unsupported, RuleId("editor.material-batch.property"),
                                                "Unknown material batch property");
    return makeSet(selection, path, descriptor->defaultValue, PropertySetMode::Absolute);
}

EditorResult<void> MaterialBatchTarget::publishAndAdopt(std::vector<MaterialDocumentTarget> candidates,
                                                        editing::Revision candidateRevision,
                                                        const EditRegion& candidateDirty) {
    if (sink_) {
        auto published = sink_->publish(candidates);
        if (!published.ok()) return published;
    }
    materials_ = std::move(candidates);
    setRevision(candidateRevision);
    widenDirty(candidateDirty);
    return editing::applied<void>();
}

EditorResult<void> MaterialBatchTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != targetId() || operation.type != "material.batch.replace.v1")
        return editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material-batch.operation"),
                                     "Material batch operation mismatch");
    const auto* values = field(operation.payload, "materials");
    const auto* array  = values ? values->getIf<EditorValue::Array>() : nullptr;
    if (!array || array->size() != materials_.size() || !operation.payload.isWithinLimits(16, 200000, 16 * 1024 * 1024))
        return editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material-batch.payload"),
                                     "Material batch payload is incomplete or exceeds limits");
    std::vector<MaterialDocumentTarget> candidates;
    candidates.reserve(materials_.size());
    std::set<std::string> ids;
    for (const auto& item : *array) {
        const auto* idValue  = field(item, "id");
        const auto* snapshot = field(item, "snapshot");
        const auto* id       = idValue ? idValue->getIf<std::string>() : nullptr;
        if (!id || !snapshot || !ids.insert(*id).second)
            return editing::failed<void>(EditorStatus::Rejected, RuleId("editor.material-batch.entry"),
                                         "Material batch entry is invalid or duplicated");
        const auto original = std::find_if(materials_.begin(), materials_.end(),
                                           [&](const auto& material) { return material.targetId().value() == *id; });
        if (original == materials_.end())
            return editing::failed<void>(EditorStatus::Conflict, RuleId("editor.material-batch.identity"),
                                         "Material batch identities changed");
        MaterialDocumentTarget candidate(*id);
        auto                   loaded = candidate.loadSnapshot(*snapshot);
        if (!loaded.ok()) return EditorResult<void>::failure(loaded.status());
        candidates.push_back(std::move(candidate));
    }
    EditRegion dirty;
    dirty.include(0, 0);
    return publishAndAdopt(std::move(candidates), revisionValue() + 1, dirty);
}

std::unique_ptr<IDomainOperationTarget> MaterialBatchTarget::cloneDomainState() const {
    auto candidate   = std::make_unique<MaterialBatchTarget>(*this);
    candidate->sink_ = nullptr;
    return candidate;
}

EditorResult<void> MaterialBatchTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<MaterialBatchTarget*>(candidate.get());
    if (!typed || typed->id_ != id_ || typed->materials_.size() != materials_.size())
        return editing::failed<void>(EditorStatus::Conflict, RuleId("editor.material-batch.candidate"),
                                     "Material batch candidate does not match this target");
    return publishAndAdopt(std::move(typed->materials_), typed->revisionValue(), typed->dirtyRegion());
}

EditorValue MaterialBatchTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"materials", content(materials_)}};
}

std::vector<editing::StableId> MaterialBatchTarget::materialIds() const {
    std::vector<editing::StableId> result;
    result.reserve(materials_.size());
    for (const auto& material : materials_) result.emplace_back(material.targetId().value());
    return result;
}

}  // namespace eve::material_editing
