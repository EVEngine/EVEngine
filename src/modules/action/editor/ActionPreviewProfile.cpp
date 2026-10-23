#include "action/editor/ActionPreviewProfile.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

Result<void> invalid(std::string message, std::string path) {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

const Value* field(const Value& value, std::string_view name) { return value.find(std::string(name)); }

Result<std::string> stringField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isString()) return invalid<std::string>("expected string field", std::move(path));
    return Result<std::string>::success(member->asString());
}

Result<bool> boolField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isBool()) return invalid<bool>("expected boolean field", std::move(path));
    return Result<bool>::success(member->asBool());
}

Result<std::int64_t> intField(const Value& value, std::string_view name, std::string path) {
    const Value* member = field(value, name);
    if (!member || !member->isInt64()) return invalid<std::int64_t>("expected integer field", std::move(path));
    return Result<std::int64_t>::success(member->asInt());
}

bool validUri(std::string_view uri) {
    if (uri.empty()) return true;
    const auto scheme = uri.find("://");
    return scheme != std::string_view::npos && scheme != 0 && scheme + 3 < uri.size();
}

std::string normalizedBone(std::string_view name) {
    std::string normalized;
    normalized.reserve(name.size());
    for (const unsigned char character : name)
        if (std::isalnum(character)) normalized.push_back(static_cast<char>(std::tolower(character)));
    return normalized;
}

Result<void> validateSkeleton(const ActionPreviewSkeleton& skeleton, std::string path) {
    if (skeleton.bones.empty()) return invalid("skeleton has no bones", std::move(path));
    for (std::size_t index = 0; index < skeleton.bones.size(); ++index) {
        const auto& bone = skeleton.bones[index];
        if (bone.name.empty()) return invalid("skeleton bone name is empty", path + ".bones[" + std::to_string(index) + "]");
        if (bone.parent < -1 || bone.parent >= static_cast<int>(index))
            return invalid("skeleton parent must precede its child", path + ".bones[" + std::to_string(index) + "].parent");
    }
    return Result<void>::success();
}

ActionPreviewCompatibility compareSkeletons(const ActionPreviewSkeleton& target,
                                            const ActionPreviewSkeleton& candidate) {
    if (target.bones.size() != candidate.bones.size()) return ActionPreviewCompatibility::Incompatible;
    bool exact = true;
    std::map<std::string, std::string> targetParents;
    for (std::size_t index = 0; index < target.bones.size(); ++index) {
        const auto& bone = target.bones[index];
        const std::string parent = bone.parent < 0 ? std::string{} : normalizedBone(target.bones[bone.parent].name);
        targetParents.emplace(normalizedBone(bone.name), parent);
    }
    for (std::size_t index = 0; index < candidate.bones.size(); ++index) {
        const auto& bone = candidate.bones[index];
        const std::string normalized = normalizedBone(bone.name);
        const std::string parent = bone.parent < 0 ? std::string{} : normalizedBone(candidate.bones[bone.parent].name);
        const auto found = targetParents.find(normalized);
        if (found == targetParents.end() || found->second != parent) return ActionPreviewCompatibility::Incompatible;
        exact = exact && target.bones[index].name == bone.name && target.bones[index].parent == bone.parent;
    }
    return exact ? ActionPreviewCompatibility::Compatible : ActionPreviewCompatibility::Retargetable;
}

}  // namespace

std::string_view actionPreviewShadingName(ActionPreviewShading shading) noexcept {
    switch (shading) {
        case ActionPreviewShading::Lit: return "lit";
        case ActionPreviewShading::Unlit: return "unlit";
    }
    return "lit";
}

Result<void> ActionPreviewProfile::validate() const {
    if (actionAsset.format().empty()) return invalid("preview profile requires an action asset", "actionAssetGuid");
    const std::pair<std::string_view, std::string_view> uris[] = {
        {previewModelUri, "previewModelUri"}, {baseMotionUri, "baseMotionUri"},
        {animationProfileUri, "animationProfileUri"}, {boneMaskUri, "boneMaskUri"}};
    for (const auto& [uri, path] : uris)
        if (!validUri(uri)) return invalid("preview resource must use a canonical URI", std::string(path));
    return Result<void>::success();
}

Result<Value> ActionPreviewProfile::toValue() const {
    auto valid = validate();
    if (!valid) return Result<Value>::failure(valid.status());
    Value::Object object;
    object["schema"]              = std::string(kActionPreviewProfileSchemaId);
    object["schemaVersion"]       = static_cast<std::int64_t>(kActionPreviewProfileSchemaVersion);
    object["actionAssetGuid"]     = actionAsset.format();
    object["previewModelUri"]     = previewModelUri;
    object["baseMotionUri"]       = baseMotionUri;
    object["animationProfileUri"] = animationProfileUri;
    object["boneMaskUri"]         = boneMaskUri;
    object["showRootMotion"]      = showRootMotion;
    object["footIk"]              = footIk;
    object["showGrid"]            = showGrid;
    object["showSkeleton"]        = showSkeleton;
    object["showJointNames"]      = showJointNames;
    object["shading"]             = std::string(actionPreviewShadingName(shading));
    return Result<Value>::success(Value(std::move(object)));
}

Result<ActionPreviewProfile> ActionPreviewProfile::fromValue(const Value& value) {
    if (!value.isObject()) return invalid<ActionPreviewProfile>("expected preview profile object", "$");
    auto schema = stringField(value, "schema", "schema");
    auto version = intField(value, "schemaVersion", "schemaVersion");
    if (!schema) return Result<ActionPreviewProfile>::failure(schema.status());
    if (!version) return Result<ActionPreviewProfile>::failure(version.status());
    if (schema.value() != kActionPreviewProfileSchemaId)
        return invalid<ActionPreviewProfile>("unexpected preview profile schema", "schema");
    if (version.value() != static_cast<std::int64_t>(kActionPreviewProfileSchemaVersion))
        return invalid<ActionPreviewProfile>("unsupported preview profile schema version", "schemaVersion");

    auto asset = stringField(value, "actionAssetGuid", "actionAssetGuid");
    auto model = stringField(value, "previewModelUri", "previewModelUri");
    auto motion = stringField(value, "baseMotionUri", "baseMotionUri");
    auto animationProfile = stringField(value, "animationProfileUri", "animationProfileUri");
    auto mask = stringField(value, "boneMaskUri", "boneMaskUri");
    auto rootMotion = boolField(value, "showRootMotion", "showRootMotion");
    auto footIkValue = boolField(value, "footIk", "footIk");
    auto grid = boolField(value, "showGrid", "showGrid");
    auto skeleton = boolField(value, "showSkeleton", "showSkeleton");
    auto names = boolField(value, "showJointNames", "showJointNames");
    auto shadingValue = stringField(value, "shading", "shading");
    if (!asset) return Result<ActionPreviewProfile>::failure(asset.status());
    if (!model) return Result<ActionPreviewProfile>::failure(model.status());
    if (!motion) return Result<ActionPreviewProfile>::failure(motion.status());
    if (!animationProfile) return Result<ActionPreviewProfile>::failure(animationProfile.status());
    if (!mask) return Result<ActionPreviewProfile>::failure(mask.status());
    if (!rootMotion) return Result<ActionPreviewProfile>::failure(rootMotion.status());
    if (!footIkValue) return Result<ActionPreviewProfile>::failure(footIkValue.status());
    if (!grid) return Result<ActionPreviewProfile>::failure(grid.status());
    if (!skeleton) return Result<ActionPreviewProfile>::failure(skeleton.status());
    if (!names) return Result<ActionPreviewProfile>::failure(names.status());
    if (!shadingValue) return Result<ActionPreviewProfile>::failure(shadingValue.status());

    ActionPreviewProfile profile;
    profile.actionAsset          = AssetGuid(std::move(asset).takeValue());
    profile.previewModelUri      = std::move(model).takeValue();
    profile.baseMotionUri        = std::move(motion).takeValue();
    profile.animationProfileUri  = std::move(animationProfile).takeValue();
    profile.boneMaskUri          = std::move(mask).takeValue();
    profile.showRootMotion       = rootMotion.value();
    profile.footIk               = footIkValue.value();
    profile.showGrid             = grid.value();
    profile.showSkeleton         = skeleton.value();
    profile.showJointNames       = names.value();
    if (shadingValue.value() == "lit") profile.shading = ActionPreviewShading::Lit;
    else if (shadingValue.value() == "unlit") profile.shading = ActionPreviewShading::Unlit;
    else return invalid<ActionPreviewProfile>("unknown preview shading mode", "shading");
    auto valid = profile.validate();
    if (!valid) return Result<ActionPreviewProfile>::failure(valid.status());
    return Result<ActionPreviewProfile>::success(std::move(profile));
}

bool ActionPreviewCompatibilityReport::canPreview() const noexcept {
    if (!profileDiagnostics.empty()) return false;
    return std::none_of(sections.begin(), sections.end(), [](const auto& section) {
        return section.compatibility == ActionPreviewCompatibility::Incompatible ||
               section.compatibility == ActionPreviewCompatibility::Unsupported;
    });
}

Result<ActionPreviewCompatibilityReport> ActionPreviewCompatibilityAnalyzer::analyze(
    const action::ActionTimeline& timeline, const ActionPreviewProfile& profile,
    IActionPreviewAssetProvider* provider) const {
    auto timelineValid = timeline.validate();
    if (!timelineValid) return Result<ActionPreviewCompatibilityReport>::failure(timelineValid.status());
    auto profileValid = profile.validate();
    if (!profileValid) return Result<ActionPreviewCompatibilityReport>::failure(profileValid.status());

    ActionPreviewCompatibilityReport report;
    if (!provider) {
        report.profileDiagnostics.push_back(Diagnostic::error(
            DiagnosticCode::Unsupported, "animation/model preview asset provider is not available", "provider"));
        for (const auto& section : timeline.animationSections)
            report.sections.push_back({section.id, ActionPreviewCompatibility::Unsupported,
                                       {Diagnostic::error(DiagnosticCode::Unsupported,
                                                          "skeleton compatibility was not evaluated", "provider")}});
        return Result<ActionPreviewCompatibilityReport>::success(std::move(report));
    }

    if (profile.previewModelUri.empty() && timeline.animationSections.empty()) {
        report.profileDiagnostics.push_back(Diagnostic::error(
            DiagnosticCode::NotFound, "preview requires a model or at least one animation section", "previewModelUri"));
        return Result<ActionPreviewCompatibilityReport>::success(std::move(report));
    }
    Result<ActionPreviewSkeleton> target = profile.previewModelUri.empty()
                                               ? provider->animationSkeleton(timeline.animationSections.front().animationUri)
                                               : provider->modelSkeleton(profile.previewModelUri);
    if (!target) return Result<ActionPreviewCompatibilityReport>::failure(target.status());
    auto targetValid = validateSkeleton(target.value(), "targetSkeleton");
    if (!targetValid) return Result<ActionPreviewCompatibilityReport>::failure(targetValid.status());
    if (!profile.boneMaskUri.empty()) {
        auto mask = provider->validateBoneMask(profile.boneMaskUri, target.value());
        if (!mask) {
            if (!mask.status().diagnostics().empty())
                report.profileDiagnostics.push_back(mask.status().diagnostics().front());
            else
                report.profileDiagnostics.push_back(Diagnostic::error(
                    DiagnosticCode::Conflict, "preview bone mask is incompatible with the target skeleton",
                    "boneMaskUri"));
        }
    }

    for (const auto& section : timeline.animationSections) {
        ActionPreviewSectionDiagnostic projected;
        projected.sectionId = section.id;
        auto skeleton = provider->animationSkeleton(section.animationUri);
        if (!skeleton) {
            projected.compatibility = ActionPreviewCompatibility::Incompatible;
            projected.diagnostics   = skeleton.status().diagnostics();
            report.sections.push_back(std::move(projected));
            continue;
        }
        auto valid = validateSkeleton(skeleton.value(), "animationSkeleton");
        if (!valid) {
            projected.compatibility = ActionPreviewCompatibility::Incompatible;
            projected.diagnostics   = valid.status().diagnostics();
            report.sections.push_back(std::move(projected));
            continue;
        }
        projected.compatibility = compareSkeletons(target.value(), skeleton.value());
        if (projected.compatibility == ActionPreviewCompatibility::Retargetable)
            projected.diagnostics.push_back(Diagnostic::warning(
                DiagnosticCode::Conflict, "animation skeleton requires normalized-name retargeting", "animationUri"));
        else if (projected.compatibility == ActionPreviewCompatibility::Incompatible)
            projected.diagnostics.push_back(Diagnostic::error(
                DiagnosticCode::Conflict, "animation skeleton is incompatible with the preview target", "animationUri"));
        report.sections.push_back(std::move(projected));
    }
    return Result<ActionPreviewCompatibilityReport>::success(std::move(report));
}

ActionPreviewProfileStore::ActionPreviewProfileStore(std::filesystem::path projectRoot)
    : projectRoot_(std::move(projectRoot)) {}

std::filesystem::path ActionPreviewProfileStore::pathFor(const AssetGuid& actionAsset) const {
    std::string filename = actionAsset.format();
    for (char& character : filename)
        if (!std::isalnum(static_cast<unsigned char>(character)) && character != '-' && character != '_')
            character = '_';
    return projectRoot_ / ".eve" / "action-preview" / (filename + ".json");
}

Result<void> ActionPreviewProfileStore::save(const ActionPreviewProfile& profile) const {
    auto encoded = profile.toValue();
    if (!encoded) return Result<void>::failure(encoded.status());
    auto json = encoded.value().toJson();
    if (!json) return Result<void>::failure(json.status());

    const auto destination = pathFor(profile.actionAsset);
    const auto temporary   = std::filesystem::path(destination.string() + ".tmp");
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "could not create action preview sidecar directory", destination.string()));
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "could not open action preview sidecar temporary file", temporary.string()));
        output << json.value();
        output.flush();
        if (!output)
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, "could not write action preview sidecar", temporary.string()));
    }
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code removeError;
        std::filesystem::remove(destination, removeError);
        error.clear();
        std::filesystem::rename(temporary, destination, error);
    }
    if (error)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Failed, "could not atomically replace action preview sidecar", destination.string()));
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<ActionPreviewProfile> ActionPreviewProfileStore::load(const AssetGuid& actionAsset) const {
    const auto source = pathFor(actionAsset);
    std::ifstream input(source, std::ios::binary);
    if (!input)
        return Result<ActionPreviewProfile>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "action preview sidecar was not found", source.string()));
    std::string json((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof())
        return Result<ActionPreviewProfile>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "could not read action preview sidecar", source.string()));
    auto value = Value::fromJson(json);
    if (!value) return Result<ActionPreviewProfile>::failure(value.status());
    auto profile = ActionPreviewProfile::fromValue(value.value());
    if (!profile) return profile;
    if (profile.value().actionAsset != actionAsset)
        return invalid<ActionPreviewProfile>("preview sidecar asset identity does not match its filename",
                                             "actionAssetGuid");
    return profile;
}

}  // namespace eve::editor
