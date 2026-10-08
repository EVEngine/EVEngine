#pragma once
#include "common/Export.h"


#include "editing/EditingProtocol.h"
#include "editor/EditorIds.h"

#include <cstdint>
#include <initializer_list>
#include <unordered_set>

namespace eve::editor {

/** @brief Kind of presentation host running an editor session. */
using HostKind = eve::editing::HostKind;

/** @brief Coarse host features used to discover and gate editor extensions. */
enum class HostFeature : uint64_t {
    None              = 0,
    ProjectDocuments  = 1ull << 0,
    SourceAssets      = 1ull << 1,
    RuntimeWorld      = 1ull << 2,
    ArbitraryScript   = 1ull << 3,
    BuildCook         = 1ull << 4,
    SourceControl     = 1ull << 5,
    MultiplayerSubmit = 1ull << 6,
    UserCreations     = 1ull << 7,
    DebugOverride     = 1ull << 8
};

/** @brief Operator |. */
constexpr HostFeature operator|(HostFeature left, HostFeature right) {
    return static_cast<HostFeature>(static_cast<uint64_t>(left) | static_cast<uint64_t>(right));
}
/** @brief Operator &. */
constexpr HostFeature operator&(HostFeature left, HostFeature right) {
    return static_cast<HostFeature>(static_cast<uint64_t>(left) & static_cast<uint64_t>(right));
}
/** @brief Operator |=. */
constexpr HostFeature& operator|=(HostFeature& left, HostFeature right) {
    left = left | right;
    return left;
}

/**
 * @brief Upper capability boundary for one editor host.
 *
 * This profile gates discovery, execution and build exposure. It does not
 * replace target constraints, gameplay policy or server-side authority.
 */
class EVENGINE_API_ORCHESTRATION HostProfile {
public:
    /** @brief Host profile. */
    HostProfile() = default;
    /** @brief Host profile. */
    explicit HostProfile(HostKind kind) : kind_(kind) {}

    /** @brief Unrestricted developer profile used by legacy sessions. */
    static HostProfile developer();
    /** @brief Safe runtime profile that denies commands unless allow-listed. */
    static HostProfile runtimeBuilder();
    /** @brief Headless profile that denies commands unless allow-listed. */
    static HostProfile automation();

    /** @brief Kind. */
    HostKind    kind() const { return kind_; }
    /** @brief Features. */
    HostFeature features() const { return features_; }
    /** @brief Sets the features. */
    void        setFeatures(HostFeature features) { features_ = features; }
    /** @brief Adds features. */
    void        addFeatures(HostFeature features) { features_ |= features; }
    /** @brief True when features. */
    bool        hasFeatures(HostFeature required) const;

    /** @brief Sets the allow all commands. */
    void setAllowAllCommands(bool allow) { allowAllCommands_ = allow; }
    /** @brief Allows all commands. */
    bool allowsAllCommands() const { return allowAllCommands_; }
    /** @brief Allow command. */
    void allowCommand(CommandId id);
    /** @brief Allow commands. */
    void allowCommands(std::initializer_list<CommandId> ids);
    /** @brief Deny command. */
    void denyCommand(const CommandId& id);
    /** @brief Allows command. */
    bool allowsCommand(const CommandId& id) const;

    /** @brief Max payload bytes. */
    size_t maxPayloadBytes() const { return maxPayloadBytes_; }
    /** @brief Sets the max payload bytes. */
    void   setMaxPayloadBytes(size_t bytes) { maxPayloadBytes_ = bytes; }

private:
    HostKind                                                     kind_             = HostKind::Developer;
    HostFeature                                                  features_         = HostFeature::None;
    bool                                                         allowAllCommands_ = false;
    size_t                                                       maxPayloadBytes_  = 256 * 1024;
    std::unordered_set<CommandId, StrongEditorIdHash<CommandId>> allowedCommands_;
    std::unordered_set<CommandId, StrongEditorIdHash<CommandId>> deniedCommands_;
};

}  // namespace eve::editor
