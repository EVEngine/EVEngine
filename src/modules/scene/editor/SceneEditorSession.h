#pragma once

#include "editing/EditingProtocol.h"

#include <memory>
#include <string>
#include <vector>

namespace eve::scene_editing {
class SceneTargetBase;
}

namespace eve::scene_editor {
/**
 * @brief Composable scene command/history host with no UI or renderer dependency.
 * Owns its target, command registry and history. Host supplies selection, input,
 * rendering and file I/O; snapshot JSON contains hierarchy/TRS only.
 * @thread Owner thread only. Live targets may notify host observers after publication,
 * without locks. Observers must not destroy or reenter the active session.
 */
class SceneEditorSession {
public:
    /** @brief Own a document target with a nonempty host-local identity. */
    explicit SceneEditorSession(std::string targetId);
    /** @brief Transfer a non-null scene target into this session; target ownership is exclusive. */
    explicit SceneEditorSession(std::unique_ptr<scene_editing::SceneTargetBase> target);
    ~SceneEditorSession();
    SceneEditorSession(const SceneEditorSession&)            = delete;
    SceneEditorSession& operator=(const SceneEditorSession&) = delete;
    /** @brief Execute a registered scene command through revision checks and one reversible transaction. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> execute(std::string command, editing::Value payload);
    /** @brief Restrict future commands to a host allow-list; empty denies all. Existing history remains undoable. */
    [[nodiscard]] editing::Result<void> restrictCommands(const std::vector<std::string>& commands);
    /** @brief Undo the last scene transaction; failures preserve history. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> undo();
    /** @brief Reapply the last undone scene transaction. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> redo();
    /** @brief Copy the versioned hierarchy/TRS snapshot; caller owns the returned tree. */
    [[nodiscard]] editing::Value snapshot() const;
    /** @brief Encode snapshot deterministically; file I/O and paths belong to the host. */
    [[nodiscard]] std::string saveJson() const;
    /** @brief Parse and restore schema-1 JSON as an undoable transaction; invalid input changes nothing. */
    [[nodiscard]] editing::Result<editing::TransactionReceipt> restoreJson(const std::string& json);
    /** @brief Return the current monotonic target revision. */
    [[nodiscard]] editing::Revision revision() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::scene_editor
