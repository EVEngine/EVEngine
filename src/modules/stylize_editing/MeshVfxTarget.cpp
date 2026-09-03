#include "stylize_editing/MeshVfxTarget.h"

#include "stylize/MeshVfxAsset.h"

#include <utility>

namespace eve::stylize_editing {
namespace {

constexpr const char* defaultAsset =
    R"({"schema":"eve.stylize.mesh-vfx","schemaVersion":1,"layers":[{"style":"rim"}]})";

template <class T>
EditorResult<T> fail(editing::Status status, const char* rule, std::string message) {
    return editing::failed<T>(status, editing::RuleId(rule), std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* name) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(name);
    return found == object->end() ? nullptr : &found->second;
}

PropertySchema assetSchema() {
    editing::PropertyDescriptor descriptor;
    descriptor.path = PropertyPath("asset.json");
    descriptor.displayNameKey = "editor.stylize.meshVfx.assetJson";
    descriptor.category = "meshVfx";
    descriptor.type = editing::PropertyType::String;
    descriptor.flags = editing::PropertyFlag::Runtime;
    descriptor.defaultValue = defaultAsset;
    PropertySchema schema;
    schema.typeId = "stylize.mesh-vfx";
    schema.properties.push_back(std::move(descriptor));
    return schema;
}

}  // namespace

MeshVfxAssetTarget::MeshVfxAssetTarget(std::string id) : id_(std::move(id)) {
    auto parsed = stylize::MeshVfxAsset::fromJson(defaultAsset);
    asset_ = std::make_unique<stylize::MeshVfxAsset>(std::move(parsed).expect("default mesh VFX asset"));
}

MeshVfxAssetTarget::~MeshVfxAssetTarget() = default;

MeshVfxAssetTarget::MeshVfxAssetTarget(const MeshVfxAssetTarget& other)
    : id_(other.id_), revision_(other.revision_), dirty_(other.dirty_),
      asset_(std::make_unique<stylize::MeshVfxAsset>(*other.asset_)) {}

MeshVfxAssetTarget& MeshVfxAssetTarget::operator=(const MeshVfxAssetTarget& other) {
    if (this == &other) return *this;
    id_ = other.id_;
    revision_ = other.revision_;
    dirty_ = other.dirty_;
    asset_ = std::make_unique<stylize::MeshVfxAsset>(*other.asset_);
    return *this;
}

TargetDescriptor MeshVfxAssetTarget::describe() const {
    return {TargetId(id_), "stylize.mesh-vfx", revision_, false,
            {CapabilityId("eve.editor.target.stylize-mesh-vfx-properties")}};
}

void* MeshVfxAssetTarget::queryCapability(const CapabilityId& capability) {
    return capability == CapabilityId("eve.editor.target.stylize-mesh-vfx-properties")
               ? static_cast<IPropertyProvider*>(this)
               : nullptr;
}

bool MeshVfxAssetTarget::matches(const SelectionSnapshot& selection) const {
    return selection.items.size() == 1 && selection.items.front().target == TargetId(id_);
}

eve::Result<eve::Revision> MeshVfxAssetTarget::currentRevision(const SelectionSnapshot& selection) const {
    if (!matches(selection))
        return eve::Result<eve::Revision>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "Mesh VFX selection mismatch", "editor.stylize.mesh-vfx.selection"));
    return eve::Result<eve::Revision>::success(eve::Revision(revision_));
}

PropertySchema MeshVfxAssetTarget::schema(const SelectionSnapshot&) const { return assetSchema(); }

std::string MeshVfxAssetTarget::canonicalJson() const {
    return asset_->toJson().expect("serialize Mesh VFX editor asset");
}

PropertyReadResult MeshVfxAssetTarget::read(const SelectionSnapshot& selection, const PropertyPath& path) const {
    if (!matches(selection) || path != PropertyPath("asset.json")) return {};
    return {editing::PropertyReadState::Value, canonicalJson(), {}};
}

EditorResult<DomainOperation> MeshVfxAssetTarget::makeSet(const SelectionSnapshot& selection,
                                                          const PropertyPath& path, const EditorValue& value,
                                                          PropertySetMode mode) const {
    if (!matches(selection) || path != PropertyPath("asset.json") || mode != PropertySetMode::Absolute)
        return fail<DomainOperation>(editing::Status::Rejected, "editor.stylize.mesh-vfx.set",
                                     "Mesh VFX requires a matching absolute asset.json edit");
    const auto* json = value.getIf<std::string>();
    if (!json)
        return fail<DomainOperation>(editing::Status::Rejected, "editor.stylize.mesh-vfx.value",
                                     "Mesh VFX asset.json must be a string");
    auto parsed = stylize::MeshVfxAsset::fromJson(*json);
    if (!parsed)
        return fail<DomainOperation>(editing::Status::Rejected, "editor.stylize.mesh-vfx.invalid",
                                     parsed.status().describe());
    auto canonical = parsed.value().toJson();
    if (!canonical)
        return fail<DomainOperation>(editing::Status::Rejected, "editor.stylize.mesh-vfx.serialize",
                                     canonical.status().describe());
    DomainOperation operation;
    operation.type = "stylize.mesh-vfx.replace.v1";
    operation.inverseType = operation.type;
    operation.target = TargetId(id_);
    operation.payload = EditorValue::Object{{"assetJson", canonical.value()}};
    operation.inverse = EditorValue::Object{{"assetJson", canonicalJson()}};
    operation.hasInverse = true;
    operation.affectedProperties = {"asset.json"};
    operation.mergeKey = "stylize.mesh-vfx:" + id_;
    return editing::applied<DomainOperation>(std::move(operation));
}

EditorResult<DomainOperation> MeshVfxAssetTarget::makeReset(const SelectionSnapshot& selection,
                                                            const PropertyPath& path) const {
    return makeSet(selection, path, defaultAsset, PropertySetMode::Absolute);
}

EditorResult<void> MeshVfxAssetTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_) || operation.type != "stylize.mesh-vfx.replace.v1")
        return fail<void>(editing::Status::Rejected, "editor.stylize.mesh-vfx.operation",
                          "Mesh VFX operation mismatch");
    const auto* jsonValue = field(operation.payload, "assetJson");
    const auto* json = jsonValue ? jsonValue->getIf<std::string>() : nullptr;
    if (!json)
        return fail<void>(editing::Status::Rejected, "editor.stylize.mesh-vfx.payload",
                          "Mesh VFX operation payload is invalid");
    auto candidate = stylize::MeshVfxAsset::fromJson(*json);
    if (!candidate)
        return fail<void>(editing::Status::Rejected, "editor.stylize.mesh-vfx.invalid",
                          candidate.status().describe());
    asset_ = std::make_unique<stylize::MeshVfxAsset>(std::move(candidate).takeValue());
    ++revision_;
    dirty_.include(0, 0);
    return editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> MeshVfxAssetTarget::cloneDomainState() const {
    return std::make_unique<MeshVfxAssetTarget>(*this);
}

EditorResult<void> MeshVfxAssetTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* target = dynamic_cast<MeshVfxAssetTarget*>(candidate.get());
    if (!target || target->id_ != id_)
        return fail<void>(editing::Status::Conflict, "editor.stylize.mesh-vfx.candidate",
                          "Mesh VFX candidate mismatch");
    *this = *target;
    return editing::applied<void>();
}

const stylize::MeshVfxAsset& MeshVfxAssetTarget::asset() const noexcept { return *asset_; }

EditorValue MeshVfxAssetTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", std::int64_t{1}}, {"assetJson", canonicalJson()}};
}

EditorResult<void> MeshVfxAssetTarget::loadSnapshot(const EditorValue& snapshot) {
    const auto* versionValue = field(snapshot, "schemaVersion");
    const auto* jsonValue = field(snapshot, "assetJson");
    const auto* version = versionValue ? versionValue->getIf<std::int64_t>() : nullptr;
    const auto* json = jsonValue ? jsonValue->getIf<std::string>() : nullptr;
    if (!version || *version != 1 || !json)
        return fail<void>(editing::Status::Unsupported, "editor.stylize.mesh-vfx.snapshot",
                          "Unsupported Mesh VFX editor snapshot");
    DomainOperation operation;
    operation.target = TargetId(id_);
    operation.type = "stylize.mesh-vfx.replace.v1";
    operation.payload = EditorValue::Object{{"assetJson", *json}};
    auto result = applyDomainOperation(operation);
    if (result.ok()) dirty_.clear();
    return result;
}

MeshVfxPreviewRuntime::MeshVfxPreviewRuntime() = default;
MeshVfxPreviewRuntime::~MeshVfxPreviewRuntime() = default;

EditorResult<void> MeshVfxPreviewRuntime::publish(const MeshVfxAssetTarget& document) {
    auto candidate = stylize::MeshVfxAssetInstance::create(document.asset());
    if (!candidate)
        return fail<void>(editing::Status::Rejected, "editor.stylize.mesh-vfx.preview",
                          candidate.status().describe());
    instance_ = std::move(candidate).takeValue();
    revision_ = Revision(document.revision());
    return editing::applied<void>();
}

}  // namespace eve::stylize_editing
