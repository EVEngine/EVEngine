#include "graphics/editing/VegetationFieldTarget.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics_editing {
namespace {
template <class T>
editing::Result<T> fail(editing::Status status, const char* rule, std::string message) {
    return editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

editing::Value vec3(glm::vec3 value) { return editing::Value::Array{value.x, value.y, value.z}; }

bool readVec3(const editing::Value& value, glm::vec3& output) {
    const auto* array = value.getIf<editing::Value::Array>();
    if (!array || array->size() != 3) return false;
    for (std::size_t index = 0; index < 3; ++index) {
        const auto* number = (*array)[index].getIf<double>();
        if (!number || !std::isfinite(*number)) return false;
        output[static_cast<int>(index)] = static_cast<float>(*number);
    }
    return true;
}

const editing::Value* field(const editing::Value& value, const char* key) {
    const auto* object = value.getIf<editing::Value::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

editing::Value transformValue(const editing::StableId& id, const graphics::VegetationElement& element) {
    return editing::Value::Object{
        {"id", id.value()}, {"center", vec3(element.center)}, {"extents", vec3(element.extents)}, {"yaw", element.yaw}};
}

editing::PropertyDescriptor property(std::string path, editing::PropertyType type, editing::Value defaultValue,
                                     double minimum, double maximum, const char* units = "") {
    editing::PropertyDescriptor result;
    result.path            = editing::PropertyPath(path);
    result.displayNameKey  = "editor.vegetation-field." + path;
    result.descriptionKey  = result.displayNameKey + ".description";
    result.category        = "vegetation-field-transform";
    result.type            = type;
    result.flags           = editing::PropertyFlag::Runtime;
    result.defaultValue    = std::move(defaultValue);
    result.numeric.minimum = minimum;
    result.numeric.maximum = maximum;
    result.numeric.units   = units;
    return result;
}
}  // namespace

VegetationFieldTarget::VegetationFieldTarget(std::string id, graphics::VegetationField* field,
                                             graphics::VegetationGlobals globals, std::vector<Entry> entries,
                                             std::uint64_t fieldRevision)
    : id_(std::move(id)),
      field_(field),
      globals_(globals),
      entries_(std::move(entries)),
      fieldRevision_(fieldRevision) {}

editing::Result<std::unique_ptr<VegetationFieldTarget>> VegetationFieldTarget::create(
    std::string id, graphics::VegetationField& fieldValue) {
    if (id.empty())
        return fail<std::unique_ptr<VegetationFieldTarget>>(
            editing::Status::Rejected, "editor.vegetation-field.empty-id", "Vegetation field target ID is empty");
    const auto revision = fieldValue.revision();
    auto       elements = fieldValue.snapshotElements();
    if (!elements.ok()) return editing::Result<std::unique_ptr<VegetationFieldTarget>>::failure(elements.status());
    std::vector<Entry> entries;
    try {
        entries.reserve(elements.value().size());
        for (std::size_t index = 0; index < elements.value().size(); ++index)
            entries.push_back(
                {editing::StableId("element-" + std::to_string(index + 1)), std::move(elements.value()[index])});
        return editing::applied<std::unique_ptr<VegetationFieldTarget>>(
            std::unique_ptr<VegetationFieldTarget>(new VegetationFieldTarget(
                std::move(id), &fieldValue, fieldValue.globalValues(), std::move(entries), revision)));
    } catch (const std::bad_alloc&) {
        return fail<std::unique_ptr<VegetationFieldTarget>>(
            editing::Status::Failed, "editor.vegetation-field.allocation", "Vegetation field target allocation failed");
    }
}

editing::TargetDescriptor VegetationFieldTarget::describe() const {
    return {targetId(),
            "vegetation-field",
            revision_,
            false,
            {editing::CapabilityId("eve.editor.target.vegetation-field-properties")}};
}

void* VegetationFieldTarget::queryCapability(const editing::CapabilityId& capability) {
    return capability == editing::CapabilityId("eve.editor.target.vegetation-field-properties")
               ? static_cast<editing::IPropertyProvider*>(this)
               : nullptr;
}

const VegetationFieldTarget::Entry* VegetationFieldTarget::selected(const editing::SelectionSnapshot& selection) const {
    if (selection.items.size() != 1 || selection.items.front().target != targetId()) return nullptr;
    const auto& item = selection.items.front().item;
    const auto  found =
        std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.id == item; });
    return found == entries_.end() ? nullptr : &*found;
}

VegetationFieldTarget::Entry* VegetationFieldTarget::selected(const std::string& id) {
    const auto found =
        std::find_if(entries_.begin(), entries_.end(), [&](const Entry& entry) { return entry.id.value() == id; });
    return found == entries_.end() ? nullptr : &*found;
}

eve::Result<eve::Revision> VegetationFieldTarget::currentRevision(const editing::SelectionSnapshot& selection) const {
    if (!selected(selection) || (field_ && field_->revision() != fieldRevision_))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "Vegetation field selection or runtime revision is stale",
            "editor.vegetation-field.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

editing::PropertySchema VegetationFieldTarget::schema(const editing::SelectionSnapshot&) const {
    editing::PropertySchema result;
    result.typeId     = "vegetation-field.element";
    result.properties = {
        property("element.center", editing::PropertyType::Vec3, vec3({0.f, 0.f, 0.f}), -1000000, 1000000, "m"),
        property("element.extents", editing::PropertyType::Vec3, vec3({1.f, 1.f, 1.f}), 0.0001, 1000000, "m"),
        property("element.yaw", editing::PropertyType::Float, 0.0, -1000000, 1000000, "rad")};
    return result;
}

editing::PropertyReadResult VegetationFieldTarget::read(const editing::SelectionSnapshot& selection,
                                                        const editing::PropertyPath&      path) const {
    const Entry* entry = selected(selection);
    if (!entry || !schema(selection).find(path)) return {};
    if (path == editing::PropertyPath("element.center"))
        return {editing::PropertyReadState::Value, vec3(entry->value.center), {}};
    if (path == editing::PropertyPath("element.extents"))
        return {editing::PropertyReadState::Value, vec3(entry->value.extents), {}};
    return {editing::PropertyReadState::Value, entry->value.yaw, {}};
}

editing::Result<editing::DomainOperation> VegetationFieldTarget::replacement(const Entry& before, const Entry& after,
                                                                             std::string propertyName) const {
    editing::DomainOperation operation;
    operation.type        = "vegetation-field.element.transform.v1";
    operation.inverseType = operation.type;
    operation.target      = targetId();
    operation.payload     = transformValue(after.id, after.value);
    operation.inverse     = transformValue(before.id, before.value);
    operation.hasInverse  = true;
    operation.affectedObjects.push_back({targetId(), after.id.value(), revision_});
    if (!propertyName.empty()) operation.affectedProperties.push_back(propertyName);
    operation.mergeKey = "vegetation-field:" + id_ + ":" + after.id.value() + ":transform";
    return editing::applied<editing::DomainOperation>(std::move(operation));
}

editing::Result<editing::DomainOperation> VegetationFieldTarget::makeTransform(
    const editing::SelectionSnapshot& selection, glm::vec3 center, glm::vec3 extents, float yaw) const {
    const Entry* before = selected(selection);
    if (!before || (field_ && field_->revision() != fieldRevision_))
        return fail<editing::DomainOperation>(editing::Status::Conflict, "editor.vegetation-field.stale",
                                              "Vegetation field transform targets stale state");
    Entry after         = *before;
    after.value.center  = center;
    after.value.extents = extents;
    after.value.yaw     = yaw;
    graphics::VegetationField                validator;
    std::vector<graphics::VegetationElement> values;
    values.reserve(entries_.size());
    for (const Entry& entry : entries_) values.push_back(entry.id == after.id ? after.value : entry.value);
    const auto valid = validator.replace(globals_, values);
    if (!valid.ok()) return editing::Result<editing::DomainOperation>::failure(valid.status());
    return replacement(*before, after, {});
}

editing::Result<editing::DomainOperation> VegetationFieldTarget::makeSet(const editing::SelectionSnapshot& selection,
                                                                         const editing::PropertyPath&      path,
                                                                         const editing::Value&             value,
                                                                         editing::PropertySetMode          mode) const {
    if (mode == editing::PropertySetMode::Reset) return makeReset(selection, path);
    const Entry* before     = selected(selection);
    const auto   descriptor = schema(selection).find(path);
    if (!before || !descriptor || mode != editing::PropertySetMode::Absolute)
        return fail<editing::DomainOperation>(editing::Status::Rejected, "editor.vegetation-field.property",
                                              "Vegetation field property edit is invalid");
    Entry after = *before;
    if (path == editing::PropertyPath("element.center")) {
        if (!readVec3(value, after.value.center))
            return fail<editing::DomainOperation>(editing::Status::Rejected, "editor.vegetation-field.center",
                                                  "Vegetation field center must be a finite vec3");
    } else if (path == editing::PropertyPath("element.extents")) {
        if (!readVec3(value, after.value.extents))
            return fail<editing::DomainOperation>(editing::Status::Rejected, "editor.vegetation-field.extents",
                                                  "Vegetation field extents must be a finite vec3");
    } else {
        const auto* number = value.getIf<double>();
        if (!number || !std::isfinite(*number))
            return fail<editing::DomainOperation>(editing::Status::Rejected, "editor.vegetation-field.yaw",
                                                  "Vegetation field yaw must be finite");
        after.value.yaw = static_cast<float>(*number);
    }
    graphics::VegetationField                validator;
    std::vector<graphics::VegetationElement> values;
    values.reserve(entries_.size());
    for (const Entry& entry : entries_) values.push_back(entry.id == after.id ? after.value : entry.value);
    const auto valid = validator.replace(globals_, values);
    if (!valid.ok()) return editing::Result<editing::DomainOperation>::failure(valid.status());
    return replacement(*before, after, path.value());
}

editing::Result<editing::DomainOperation> VegetationFieldTarget::makeReset(const editing::SelectionSnapshot& selection,
                                                                           const editing::PropertyPath& path) const {
    const auto descriptor = schema(selection).find(path);
    if (!descriptor)
        return fail<editing::DomainOperation>(editing::Status::Unsupported, "editor.vegetation-field.property",
                                              "Unknown vegetation field property");
    return makeSet(selection, path, descriptor->defaultValue, editing::PropertySetMode::Absolute);
}

editing::Result<void> VegetationFieldTarget::validateAndPublish(const std::vector<Entry>& entries) {
    std::vector<graphics::VegetationElement> values;
    try {
        values.reserve(entries.size());
        for (const Entry& entry : entries) values.push_back(entry.value);
    } catch (const std::bad_alloc&) {
        return fail<void>(editing::Status::Failed, "editor.vegetation-field.allocation",
                          "Vegetation field publication allocation failed");
    }
    if (!field_) {
        graphics::VegetationField validator;
        const auto                result = validator.replace(globals_, values);
        return result.ok() ? editing::applied<void>() : editing::Result<void>::failure(result.status());
    }
    if (field_->revision() != fieldRevision_)
        return fail<void>(editing::Status::Conflict, "editor.vegetation-field.runtime-stale",
                          "Vegetation field changed outside this editor target");
    const auto result = field_->replace(globals_, values);
    if (!result.ok()) return editing::Result<void>::failure(result.status());
    fieldRevision_ = field_->revision();
    return editing::applied<void>();
}

editing::Result<void> VegetationFieldTarget::applyDomainOperation(const editing::DomainOperation& operation) {
    if (operation.target != targetId() || operation.type != "vegetation-field.element.transform.v1")
        return fail<void>(editing::Status::Rejected, "editor.vegetation-field.operation",
                          "Vegetation field operation mismatch");
    const auto* idValue = field(operation.payload, "id");
    const auto* id      = idValue ? idValue->getIf<std::string>() : nullptr;
    Entry*      current = id ? selected(*id) : nullptr;
    if (!current)
        return fail<void>(editing::Status::NotFound, "editor.vegetation-field.element",
                          "Vegetation field element was not found");
    Entry       replacementEntry = *current;
    const auto* center           = field(operation.payload, "center");
    const auto* extents          = field(operation.payload, "extents");
    if (!center || !extents || !readVec3(*center, replacementEntry.value.center) ||
        !readVec3(*extents, replacementEntry.value.extents))
        return fail<void>(editing::Status::Rejected, "editor.vegetation-field.transform",
                          "Vegetation field transform payload is invalid");
    const auto* yawValue = field(operation.payload, "yaw");
    const auto* yaw      = yawValue ? yawValue->getIf<double>() : nullptr;
    if (!yaw || !std::isfinite(*yaw))
        return fail<void>(editing::Status::Rejected, "editor.vegetation-field.transform",
                          "Vegetation field transform payload is invalid");
    replacementEntry.value.yaw = static_cast<float>(*yaw);
    auto candidate             = entries_;
    *std::find_if(candidate.begin(), candidate.end(), [&](const Entry& entry) { return entry.id == current->id; }) =
        std::move(replacementEntry);
    auto published = validateAndPublish(candidate);
    if (!published.ok()) return published;
    entries_ = std::move(candidate);
    ++revision_;
    dirty_.include(0, 0);
    return editing::applied<void>();
}

std::unique_ptr<editing::IDomainOperationTarget> VegetationFieldTarget::cloneDomainState() const {
    auto clone    = std::unique_ptr<VegetationFieldTarget>(new VegetationFieldTarget(*this));
    clone->field_ = nullptr;
    return clone;
}

editing::Result<void> VegetationFieldTarget::commitDomainState(
    std::unique_ptr<editing::IDomainOperationTarget> candidate) {
    auto* typed = dynamic_cast<VegetationFieldTarget*>(candidate.get());
    if (!typed || typed->id_ != id_ || !field_)
        return fail<void>(editing::Status::Conflict, "editor.vegetation-field.candidate",
                          "Vegetation field candidate does not match this target");
    auto published = validateAndPublish(typed->entries_);
    if (!published.ok()) return published;
    globals_  = typed->globals_;
    entries_  = std::move(typed->entries_);
    revision_ = typed->revision_;
    dirty_.include(typed->dirty_);
    return editing::applied<void>();
}

std::vector<editing::StableId> VegetationFieldTarget::elementIds() const {
    std::vector<editing::StableId> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) result.push_back(entry.id);
    return result;
}

}  // namespace eve::graphics_editing
