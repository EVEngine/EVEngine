#include "editor/EditorHost.h"

#include "common/Capability.h"
#include "editing/PropertySchemaJson.h"
#include "editor/Editor.h"
#include "editor/EditorAutomationTargetFactory.h"
#include "editor/EditorCommandService.h"

#include <algorithm>
#include <utility>

namespace eve::editor {
namespace {

bool ownerAllowed(const std::vector<std::string>& owners, std::string_view owner) {
    if (owners.empty()) return true;
    return std::find(owners.begin(), owners.end(), owner) != owners.end();
}

}  // namespace

EditorHost::EditorHost(Editor& editor, Options options)
    : editor_(&editor),
      commands_(&editor.commandService()),
      options_(std::move(options)),
      workspace_(options_.id, options_.title.empty() ? options_.id : options_.title) {
    // Default-constructed HostProfile is Developer but denies commands; promote to the
    // unrestricted developer profile unless the caller already configured one.
    if (options_.profile.kind() == HostKind::Developer && !options_.profile.allowsAllCommands())
        options_.profile = HostProfile::developer();
}

EditorHost::~EditorHost() { unmountAll(); }

Result<EditorHostMountReport> EditorHost::autoMount() { return mount({}); }

Result<EditorHostMountReport> EditorHost::mount(
    const std::vector<std::string>& owners, const std::unordered_map<std::string, EditorHostMountOverride>& overrides) {
    EditorHostMountReport report;
    if (!owners.empty() && eve::cap::listenerCount<editing::IEditorPackageProvider>() == 0)
        return eve::editing::failed<EditorHostMountReport>(EditorStatus::NotFound, RuleId("editor.host.no-providers"),
                                                           "No IEditorPackageProvider capabilities are loaded");

    eve::cap::forEach<editing::IEditorPackageProvider>([&](editing::IEditorPackageProvider* provider) {
        if (!provider) return;
        const editing::EditorPackage& package = provider->package();
        if (!ownerAllowed(owners, package.ownerModule)) return;

        EditorHostMountOverride override;
        if (const auto found = overrides.find(package.ownerModule); found != overrides.end())
            override = found->second;
        else
            override.undoPolicy = package.undoPolicy;

        auto entry = mountPackage(package, override);
        if (!entry.ok()) {
            EditorHostMountEntry skipped;
            skipped.ownerModule = package.ownerModule;
            skipped.skipped     = true;
            skipped.skipReason  = entry.status().describe();
            report.entries.push_back(std::move(skipped));
            return;
        }
        if (!entry.value().skipped) ++report.mountedPackages;
        report.entries.push_back(std::move(entry).takeValue());
    });
    return eve::editing::applied<EditorHostMountReport>(std::move(report));
}

Result<void> EditorHost::unmount(std::string_view ownerModule) {
    const auto found = mounted_.find(std::string(ownerModule));
    if (found == mounted_.end()) return eve::editing::noOp();
    for (const std::string& panelId : found->second.panelIds) (void)workspace_.removePanel(panelId);
    mounted_.erase(found);
    hasPrimaryViewport_ = false;
    for (const auto& [_, mounted] : mounted_)
        for (const editing::EditorPanelSpec& panel : mounted.package.panels)
            if (panel.primaryViewport) hasPrimaryViewport_ = true;
    return eve::editing::applied<void>();
}

void EditorHost::unmountAll() {
    const std::vector<std::string> owners = [&] {
        std::vector<std::string> values;
        values.reserve(mounted_.size());
        for (const auto& [owner, _] : mounted_) values.push_back(owner);
        return values;
    }();
    for (const std::string& owner : owners) (void)unmount(owner);
}

bool EditorHost::isMounted(std::string_view ownerModule) const { return mounted_.contains(std::string(ownerModule)); }

Result<EditorHostMountEntry> EditorHost::mountPackage(const editing::EditorPackage&  package,
                                                      const EditorHostMountOverride& override) {
    auto valid = editing::validateEditorPackage(package, override.allowIncomplete);
    if (!valid.ok()) return Result<EditorHostMountEntry>::failure(valid.status());

    if (mounted_.contains(package.ownerModule))
        return eve::editing::failed<EditorHostMountEntry>(EditorStatus::Conflict, RuleId("editor.host.already-mounted"),
                                                          "Editor package already mounted: " + package.ownerModule);

    for (const editing::EditorPanelSpec& panel : package.panels) {
        if (panel.primaryViewport && hasPrimaryViewport_)
            return eve::editing::failed<EditorHostMountEntry>(
                EditorStatus::Conflict, RuleId("editor.host.primary-viewport"),
                "Only one primaryViewport panel may be mounted on a host");
    }

    std::vector<std::string> panelIds;
    auto                     installed = installPanels(package, panelIds);
    if (!installed.ok()) {
        for (const std::string& panelId : panelIds) (void)workspace_.removePanel(panelId);
        return Result<EditorHostMountEntry>::failure(installed.status());
    }

    for (const editing::EditorPanelSpec& panel : package.panels)
        if (panel.primaryViewport) hasPrimaryViewport_ = true;

    MountedPackage mounted;
    mounted.package            = package;
    mounted.package.undoPolicy = override.undoPolicy;
    mounted.panelIds           = std::move(panelIds);
    mounted_.emplace(package.ownerModule, std::move(mounted));

    EditorHostMountEntry entry;
    entry.ownerModule  = package.ownerModule;
    entry.panelCount   = package.panels.size();
    entry.commandCount = package.commands.size();
    entry.targetCount  = package.targetTypes.size();
    return eve::editing::applied<EditorHostMountEntry>(std::move(entry));
}

Result<void> EditorHost::installPanels(const editing::EditorPackage& package, std::vector<std::string>& panelIds) {
    for (const editing::EditorPanelSpec& panel : package.panels) {
        WorkspacePanelDescriptor descriptor;
        descriptor.id         = panel.id;
        descriptor.title      = panel.title;
        descriptor.region     = panel.region;
        descriptor.capability = panel.capability;
        descriptor.context    = panel.context;
        descriptor.order      = panel.order;
        auto registered       = workspace_.registerPanel(std::move(descriptor));
        if (!registered.ok())
            return eve::editing::failed<void>(registered.code(), RuleId("editor.host.panel-conflict"),
                                              "Could not install package panel: " + panel.id);
        panelIds.push_back(panel.id);
    }
    return eve::editing::applied<void>();
}

Result<EditorValue> EditorHost::exportCommandCatalog() const {
    if (!commands_)
        return eve::editing::failed<EditorValue>(EditorStatus::Failed, RuleId("editor.host.no-commands"),
                                                 "EditorHost has no command service");
    EditorValue::Array commands;
    for (const CommandDescriptor& command : commands_->commands(options_.profile)) {
        EditorValue::Object item;
        item["id"]                = command.id.value();
        item["displayName"]       = command.displayName;
        item["category"]          = command.category;
        item["owner"]             = command.ownerModule;
        item["automationAllowed"] = command.automationAllowed;
        if (!command.panelCapability.empty()) item["panelCapability"] = command.panelCapability;
        if (!command.panelContext.empty()) item["panelContext"] = command.panelContext;
        if (!command.targetKinds.empty()) {
            EditorValue::Array kinds;
            for (const std::string& kind : command.targetKinds) kinds.emplace_back(kind);
            item["targetKinds"] = std::move(kinds);
        }
        auto schema = editing::propertySchemaToJsonSchema(command.payloadSchema, command.id.value());
        if (schema.ok()) item["inputSchema"] = std::move(schema).takeValue();
        commands.emplace_back(std::move(item));
    }
    return eve::editing::applied<EditorValue>(EditorValue::Object{{"commands", std::move(commands)}});
}

Result<EditorValue> EditorHost::exportTargetCatalog() const {
    std::unordered_map<std::string, editing::PropertySchema> createSchemas;
    for (const auto& [_, mounted] : mounted_) {
        for (const editing::EditorTargetTypeSpec& target : mounted.package.targetTypes)
            createSchemas[target.type] = target.createSchema;
    }
    eve::cap::forEach<editing::IEditorPackageProvider>([&](editing::IEditorPackageProvider* provider) {
        if (!provider) return;
        for (const editing::EditorTargetTypeSpec& target : provider->package().targetTypes)
            createSchemas.emplace(target.type, target.createSchema);
    });

    EditorValue::Array types;
    eve::cap::forEach<IEditorAutomationTargetFactory>([&](IEditorAutomationTargetFactory* factory) {
        if (!factory) return;
        for (const std::string_view type : factory->types()) {
            EditorValue::Object item;
            item["type"] = std::string(type);
            if (const auto found = createSchemas.find(std::string(type)); found != createSchemas.end()) {
                auto schema = editing::propertySchemaToJsonSchema(found->second, type);
                if (schema.ok()) item["createSchema"] = std::move(schema).takeValue();
            }
            types.emplace_back(std::move(item));
        }
    });
    std::sort(types.begin(), types.end(), [](const EditorValue& left, const EditorValue& right) {
        const auto* leftObject  = left.getIf<EditorValue::Object>();
        const auto* rightObject = right.getIf<EditorValue::Object>();
        auto        typeOf      = [](const EditorValue::Object* object) -> std::string {
            if (!object) return {};
            const auto found = object->find("type");
            if (found == object->end()) return {};
            const auto* value = found->second.getIf<std::string>();
            return value ? *value : std::string{};
        };
        return typeOf(leftObject) < typeOf(rightObject);
    });
    return eve::editing::applied<EditorValue>(EditorValue::Object{{"supportedTypes", std::move(types)}});
}

}  // namespace eve::editor
