#pragma once

#include "editor/EditorCommandService.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::editor {

/** @brief Host audiences to which a game-provided editor extension may be published. */
enum class ExtensionAudience : std::uint32_t {
    None       = 0,
    Developer  = 1u << 0,
    Player     = 1u << 1,
    Admin      = 1u << 2,
    Automation = 1u << 3
};

constexpr ExtensionAudience operator|(ExtensionAudience left, ExtensionAudience right) {
    return static_cast<ExtensionAudience>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}
constexpr ExtensionAudience operator&(ExtensionAudience left, ExtensionAudience right) {
    return static_cast<ExtensionAudience>(static_cast<std::uint32_t>(left) & static_cast<std::uint32_t>(right));
}
constexpr bool hasAudience(ExtensionAudience value, ExtensionAudience audience) {
    return (value & audience) == audience;
}

/** @brief Host-neutral metadata for a game-provided editor tool. */
struct ExtensionToolDescriptor {
    ToolId                    id;
    std::string               ownerModule;
    std::string               labelKey;
    std::string               category;
    std::vector<CapabilityId> targetRequirements;
    ExtensionAudience         audiences = ExtensionAudience::Developer;
};

/** @brief Semantic palette that can place the same tool differently per audience. */
struct ExtensionPaletteDescriptor {
    StableId               id;
    std::string            ownerModule;
    std::string            titleKey;
    std::vector<ToolId>    tools;
    std::vector<CommandId> commands;
    ExtensionAudience      audiences = ExtensionAudience::Developer;
};

/** @brief Discoverable gameplay validation rule registered by a game extension. */
struct ExtensionRuleDescriptor {
    RuleId            id;
    std::string       ownerModule;
    ExtensionAudience audiences = ExtensionAudience::Developer;
};

/** @brief Scoped registration surface exposed to game modules. */
class IEditorExtensionRegistry {
public:
    virtual ~IEditorExtensionRegistry() = default;
    /** @brief Register a command; the registry overwrites ownerModule with the active extension owner. */
    virtual EditorResult<EditorValue> registerCommand(CommandDescriptor descriptor, EditorCommandHandler handler,
                                                      ExtensionAudience audiences) = 0;
    /** @brief Register semantic tool metadata. */
    virtual EditorResult<void> registerTool(ExtensionToolDescriptor descriptor) = 0;
    /** @brief Register a host-specific palette for existing tools/commands. */
    virtual EditorResult<void> registerPalette(ExtensionPaletteDescriptor descriptor) = 0;
    /** @brief Register gameplay validation metadata. */
    virtual EditorResult<void> registerRule(ExtensionRuleDescriptor descriptor) = 0;
};

/** @brief Entry point implemented by a gameplay module to inject editor behavior. */
class IGameEditorExtension {
public:
    virtual ~IGameEditorExtension() = default;
    /** @brief Stable module owner used for unload and duplicate protection. */
    virtual std::string ownerModule() const = 0;
    /** @brief Register all game-provided editor components. */
    virtual void registerEditor(IEditorExtensionRegistry& registry) = 0;
};

/**
 * @brief Loads game extensions and filters their descriptors for each host.
 *
 * The command service is non-owning and must outlive this registry. Extension
 * unload removes every command and descriptor owned by the module.
 */
class EditorExtensionRegistry final : public IEditorExtensionRegistry {
public:
    explicit EditorExtensionRegistry(EditorCommandService* commands) : commands_(commands) {}

    /** @brief Load one extension under its stable owner module. */
    EditorResult<void> load(IGameEditorExtension& extension);
    /** @brief Remove every component registered by an owner module. */
    std::size_t unload(const std::string& ownerModule);
    /** @brief Add player/admin/automation extension commands to a deny-by-default profile. */
    void configureProfile(HostProfile& profile) const;

    /** @brief Return tool descriptors visible to a host. */
    std::vector<ExtensionToolDescriptor> tools(const HostProfile& profile) const;
    /** @brief Return palette descriptors visible to a host. */
    std::vector<ExtensionPaletteDescriptor> palettes(const HostProfile& profile) const;
    /** @brief Return validation descriptors visible to a host. */
    std::vector<ExtensionRuleDescriptor> rules(const HostProfile& profile) const;

    EditorResult<EditorValue> registerCommand(CommandDescriptor descriptor, EditorCommandHandler handler,
                                              ExtensionAudience audiences) override;
    EditorResult<void>        registerTool(ExtensionToolDescriptor descriptor) override;
    EditorResult<void>        registerPalette(ExtensionPaletteDescriptor descriptor) override;
    EditorResult<void>        registerRule(ExtensionRuleDescriptor descriptor) override;

private:
    struct CommandVisibility {
        CommandId         id;
        std::string       ownerModule;
        ExtensionAudience audiences;
    };

    static ExtensionAudience  audienceFor(const HostProfile& profile);
    static EditorResult<void> invalid(const char* rule, std::string message);

    EditorCommandService*                   commands_ = nullptr;
    std::string                             activeOwner_;
    std::vector<CommandVisibility>          commandVisibility_;
    std::vector<ExtensionToolDescriptor>    tools_;
    std::vector<ExtensionPaletteDescriptor> palettes_;
    std::vector<ExtensionRuleDescriptor>    rules_;
};

}  // namespace eve::editor
