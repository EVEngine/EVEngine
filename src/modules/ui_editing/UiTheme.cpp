#include "ui_editing/UiTheme.h"
#include "ui_editing/UiThemeTokens.h"

#include <algorithm>
#include <set>
#include <utility>

namespace eve::ui_editing {
namespace {

template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<T>(status, RuleId(rule), std::move(message));
}

EditorDiagnostic diagnostic(const char* rule, std::string message) {
    return eve::editing::ruleDiagnostic(eve::DiagnosticCode::InvalidArgument, RuleId(rule),
                                        DiagnosticSeverity::Error, std::move(message));
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

bool hasError(const std::vector<EditorDiagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const EditorDiagnostic& item) { return item.severity() == DiagnosticSeverity::Error; });
}

EditorValue assetValue(const UiThemeAsset& asset) {
    return EditorValue::Object{{"id", asset.id.value()},
                               {"name", asset.name},
                               {"basePreset", presetName(asset.basePreset)},
                               {"tokens", themeTokensValue(asset.tokens)}};
}

EditorResult<UiThemeAsset> parseAsset(const EditorValue& value) {
    const EditorValue* idField     = field(value, "id");
    const EditorValue* nameField   = field(value, "name");
    const EditorValue* presetField = field(value, "basePreset");
    const EditorValue* tokensField = field(value, "tokens");
    const auto*        id          = idField ? idField->getIf<std::string>() : nullptr;
    const auto*        name        = nameField ? nameField->getIf<std::string>() : nullptr;
    const auto*        preset      = presetField ? presetField->getIf<std::string>() : nullptr;
    if (!id || id->empty() || !name || name->empty() || !preset || !tokensField)
        return fail<UiThemeAsset>(EditorStatus::Rejected, "editor.ui-theme.asset",
                                  "Theme asset id, name, basePreset and tokens are required");
    auto base = parsePreset(*preset);
    if (!base.ok()) return EditorResult<UiThemeAsset>::failure(base.status());
    auto tokens = parseThemeTokens(*tokensField);
    if (!tokens.ok()) return EditorResult<UiThemeAsset>::failure(tokens.status());
    UiThemeAsset asset;
    asset.id         = ObjectId(*id);
    asset.name       = *name;
    asset.basePreset = base.value();
    asset.tokens     = std::move(tokens).value();
    return eve::editing::applied<UiThemeAsset>(std::move(asset));
}

EditorResult<void> parseCatalog(const EditorValue& content, std::vector<UiThemeAsset>& themes, ObjectId& activeId) {
    const EditorValue* activeField = field(content, "activeId");
    const EditorValue* themesField = field(content, "themes");
    const auto*        active      = activeField ? activeField->getIf<std::string>() : nullptr;
    const auto*        list        = themesField ? themesField->getIf<EditorValue::Array>() : nullptr;
    if (!active || active->empty() || !list)
        return fail<void>(EditorStatus::Rejected, "editor.ui-theme.catalog",
                          "Theme catalog requires activeId and a themes array");
    std::vector<UiThemeAsset> parsed;
    parsed.reserve(list->size());
    for (const EditorValue& entry : *list) {
        auto asset = parseAsset(entry);
        if (!asset.ok()) return EditorResult<void>::failure(asset.status());
        parsed.push_back(std::move(asset).value());
    }
    themes   = std::move(parsed);
    activeId = ObjectId(*active);
    return eve::editing::applied<void>();
}

}  // namespace

UiThemeCatalogTarget::UiThemeCatalogTarget(std::string id) : id_(std::move(id)) {
    UiThemeAsset dark;
    dark.id         = ObjectId("dark");
    dark.name       = "Dark";
    dark.basePreset = UiThemeBasePreset::Dark;
    dark.tokens     = ui::Theme::dark();
    UiThemeAsset light;
    light.id         = ObjectId("light");
    light.name       = "Light";
    light.basePreset = UiThemeBasePreset::Light;
    light.tokens     = ui::Theme::light();
    themes_.push_back(std::move(dark));
    themes_.push_back(std::move(light));
    activeId_ = ObjectId("dark");
}

TargetDescriptor UiThemeCatalogTarget::describe() const {
    return {TargetId(id_),
            "ui-theme-catalog",
            revision_,
            false,
            {propertyCapabilityId(), IEditingSnapshotProvider::editingCapabilityId()}};
}

void* UiThemeCatalogTarget::queryCapability(const CapabilityId& capability) {
    if (capability == propertyCapabilityId()) return static_cast<IPropertyProvider*>(this);
    if (capability == IEditingSnapshotProvider::editingCapabilityId())
        return static_cast<IEditingSnapshotProvider*>(this);
    return nullptr;
}

EditorValue UiThemeCatalogTarget::contentValue() const {
    EditorValue::Array themes;
    themes.reserve(themes_.size());
    for (const UiThemeAsset& asset : themes_) themes.push_back(assetValue(asset));
    return EditorValue::Object{{"activeId", activeId_.value()}, {"themes", std::move(themes)}};
}

EditorResult<DomainOperation> UiThemeCatalogTarget::replacement(EditorValue content, std::string property) const {
    DomainOperation operation;
    operation.type        = "ui.theme.catalog.replace.v1";
    operation.inverseType = operation.type;
    operation.target      = TargetId(id_);
    operation.payload     = std::move(content);
    operation.inverse     = contentValue();
    operation.hasInverse  = true;
    if (!property.empty()) operation.affectedProperties.push_back(property);
    operation.mergeKey = "ui-theme:" + id_ + ":" + (property.empty() ? "structure" : property);
    return eve::editing::applied<DomainOperation>(std::move(operation));
}

const UiThemeAsset* UiThemeCatalogTarget::findTheme(const ObjectId& id) const {
    for (const UiThemeAsset& asset : themes_)
        if (asset.id == id) return &asset;
    return nullptr;
}

UiThemeAsset* UiThemeCatalogTarget::mutableTheme(const ObjectId& id) {
    for (UiThemeAsset& asset : themes_)
        if (asset.id == id) return &asset;
    return nullptr;
}

EditorResult<UiThemeAsset> UiThemeCatalogTarget::theme(const ObjectId& id) const {
    const UiThemeAsset* asset = findTheme(id);
    if (!asset)
        return fail<UiThemeAsset>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                  "Theme asset does not exist: " + id.value());
    return eve::editing::applied<UiThemeAsset>(*asset);
}

std::string UiThemeCatalogTarget::runtimeName(const ObjectId& id) const {
    if (id.value() == "dark" || id.value() == "light") return id.value();
    return "custom";
}

std::vector<EditorDiagnostic> UiThemeCatalogTarget::validate() const {
    std::vector<EditorDiagnostic> diagnostics;
    if (themes_.empty())
        diagnostics.push_back(diagnostic("editor.ui-theme.empty", "Theme catalog must contain at least one asset"));
    if (themes_.size() > 32)
        diagnostics.push_back(diagnostic("editor.ui-theme.budget", "Theme catalog exceeds 32 named assets"));
    std::set<std::string> ids;
    std::set<std::string> names;
    bool                  activeFound = false;
    for (const UiThemeAsset& asset : themes_) {
        if (asset.id.empty() || !ids.insert(asset.id.value()).second)
            diagnostics.push_back(diagnostic("editor.ui-theme.id", "Theme asset id is empty or duplicated"));
        if (asset.name.empty() || !names.insert(asset.name).second)
            diagnostics.push_back(diagnostic("editor.ui-theme.name", "Theme asset name is empty or duplicated"));
        if (asset.id == activeId_) activeFound = true;
        auto tokenDiagnostics = validateThemeTokens(asset.tokens);
        diagnostics.insert(diagnostics.end(), tokenDiagnostics.begin(), tokenDiagnostics.end());
    }
    if (!activeFound)
        diagnostics.push_back(diagnostic("editor.ui-theme.active", "Active theme id is missing from the catalog"));
    return diagnostics;
}

EditorResult<void> UiThemeCatalogTarget::applyDomainOperation(const DomainOperation& operation) {
    if (operation.target != TargetId(id_))
        return fail<void>(EditorStatus::Rejected, "editor.ui-theme.target", "Theme operation targets another catalog");
    if (operation.type != "ui.theme.catalog.replace.v1")
        return fail<void>(EditorStatus::Unsupported, "editor.ui-theme.operation",
                          "Unsupported theme operation: " + operation.type);
    auto candidate = *this;
    auto parsed    = parseCatalog(operation.payload, candidate.themes_, candidate.activeId_);
    if (!parsed.ok()) return parsed;
    if (hasError(candidate.validate()))
        return fail<void>(EditorStatus::Rejected, "editor.ui-theme.invalid",
                          "Theme catalog replacement is invalid");
    themes_   = std::move(candidate.themes_);
    activeId_ = candidate.activeId_;
    ++revision_;
    dirty_.include(0, 0);
    return eve::editing::applied<void>();
}

std::unique_ptr<IDomainOperationTarget> UiThemeCatalogTarget::cloneDomainState() const {
    return std::make_unique<UiThemeCatalogTarget>(*this);
}

EditorResult<void> UiThemeCatalogTarget::commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) {
    auto* catalog = dynamic_cast<UiThemeCatalogTarget*>(candidate.get());
    if (!catalog || catalog->id_ != id_)
        return fail<void>(EditorStatus::Conflict, "editor.ui-theme.candidate",
                          "Theme compensation candidate belongs to another catalog");
    *this = *catalog;
    return eve::editing::applied<void>();
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeCreateFromPreset(const ObjectId& id, std::string name,
                                                                         UiThemeBasePreset preset) const {
    if (id.empty() || name.empty())
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.create",
                                     "Theme id and name are required");
    if (preset == UiThemeBasePreset::Custom)
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.preset-source",
                                     "New themes must be created from dark or light");
    if (findTheme(id) ||
        std::any_of(themes_.begin(), themes_.end(), [&](const UiThemeAsset& asset) { return asset.name == name; }))
        return fail<DomainOperation>(EditorStatus::Conflict, "editor.ui-theme.exists",
                                     "Theme id or name already exists");
    auto candidate = *this;
    UiThemeAsset asset;
    asset.id         = id;
    asset.name       = std::move(name);
    asset.basePreset = preset;
    asset.tokens     = themeFromPreset(preset);
    candidate.themes_.push_back(std::move(asset));
    if (hasError(candidate.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.invalid",
                                     "Creating the theme would invalidate the catalog");
    return replacement(candidate.contentValue());
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeDuplicate(const ObjectId& source, const ObjectId& id,
                                                                  std::string name) const {
    const UiThemeAsset* original = findTheme(source);
    if (!original)
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                     "Theme asset does not exist");
    if (id.empty() || name.empty() || findTheme(id) ||
        std::any_of(themes_.begin(), themes_.end(), [&](const UiThemeAsset& asset) { return asset.name == name; }))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.duplicate",
                                     "Duplicated theme needs a unique id and name");
    auto         candidate = *this;
    UiThemeAsset asset     = *original;
    asset.id               = id;
    asset.name             = std::move(name);
    asset.basePreset       = UiThemeBasePreset::Custom;
    candidate.themes_.push_back(std::move(asset));
    if (hasError(candidate.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.invalid",
                                     "Duplicating the theme would invalidate the catalog");
    return replacement(candidate.contentValue());
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeRename(const ObjectId& id, std::string name) const {
    const UiThemeAsset* current = findTheme(id);
    if (!current)
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                     "Theme asset does not exist");
    if (name.empty())
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.empty-name",
                                     "Theme name must not be empty");
    if (std::any_of(themes_.begin(), themes_.end(),
                    [&](const UiThemeAsset& asset) { return asset.id != id && asset.name == name; }))
        return fail<DomainOperation>(EditorStatus::Conflict, "editor.ui-theme.name", "Theme name already exists");
    auto candidate = *this;
    candidate.mutableTheme(id)->name = std::move(name);
    return replacement(candidate.contentValue(), "theme.name");
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeDelete(const ObjectId& id) const {
    if (!findTheme(id))
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                     "Theme asset does not exist");
    if (themes_.size() <= 1)
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.last",
                                     "The last theme asset cannot be deleted");
    auto candidate = *this;
    std::erase_if(candidate.themes_, [&](const UiThemeAsset& asset) { return asset.id == id; });
    if (candidate.activeId_ == id) candidate.activeId_ = candidate.themes_.front().id;
    if (hasError(candidate.validate()))
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.invalid",
                                     "Deleting the theme would invalidate the catalog");
    return replacement(candidate.contentValue());
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeSetActive(const ObjectId& id) const {
    if (!findTheme(id))
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                     "Theme asset does not exist");
    auto candidate      = *this;
    candidate.activeId_ = id;
    return replacement(candidate.contentValue(), "catalog.active");
}

EditorResult<DomainOperation> UiThemeCatalogTarget::makeResetToBase(const ObjectId& id) const {
    const UiThemeAsset* current = findTheme(id);
    if (!current)
        return fail<DomainOperation>(EditorStatus::NotFound, "editor.ui-theme.missing",
                                     "Theme asset does not exist");
    if (current->basePreset == UiThemeBasePreset::Custom)
        return fail<DomainOperation>(EditorStatus::Rejected, "editor.ui-theme.reset",
                                     "Custom themes have no built-in base preset to restore");
    auto candidate                 = *this;
    candidate.mutableTheme(id)->tokens = themeFromPreset(current->basePreset);
    return replacement(candidate.contentValue(), "theme.tokens");
}

EditorValue UiThemeCatalogTarget::snapshotValue() const {
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"content", contentValue()}};
}

EditorResult<void> UiThemeCatalogTarget::loadSnapshot(const EditorValue& snapshot) {
    const EditorValue* versionValue = field(snapshot, "schemaVersion");
    const EditorValue* content      = field(snapshot, "content");
    const auto*        version      = versionValue ? versionValue->getIf<int64_t>() : nullptr;
    if (!version || *version != 1 || !content)
        return fail<void>(EditorStatus::Unsupported, "editor.ui-theme.snapshot",
                          "Theme snapshot requires schemaVersion 1 and content");
    auto candidate = *this;
    auto parsed    = parseCatalog(*content, candidate.themes_, candidate.activeId_);
    if (!parsed.ok()) return parsed;
    if (hasError(candidate.validate()))
        return fail<void>(EditorStatus::Rejected, "editor.ui-theme.snapshot-invalid",
                          "Theme snapshot failed validation");
    themes_   = std::move(candidate.themes_);
    activeId_ = candidate.activeId_;
    ++revision_;
    dirty_.clear();
    return eve::editing::applied<void>();
}

}  // namespace eve::ui_editing
