#pragma once

#include "level_editing/TileBuffer.h"
#include "editing/EditingResult.h"

#include <string>
#include <vector>

namespace eve::level_editing {

/**
 * @brief Undo/redo stack: opaque string actions + optional tile change groups
 * that can apply directly to a TileBuffer.
 */
class EditorHistory {
public:
    void clear();

    void push(const std::string &name, const std::string &payload);

    void beginGroup(const std::string &name);
    void recordTile(int x, int y, int oldGid, int newGid);
    void endGroup();
    bool isGrouping() const { return grouping_; }

    bool canUndo() const;
    bool canRedo() const;
    int getUndoCount() const;
    int getRedoCount() const;

    /** @brief Move the latest action to redo history and return its stable name. */
    [[nodiscard]] editing::Result<std::string> undoAction();
    /** @brief Move the latest redo action back to undo history and return its stable name. */
    [[nodiscard]] editing::Result<std::string> redoAction();
    /** @brief Compatibility-only boolean projection of undoAction(). */
    bool undo();
    /** @brief Compatibility-only boolean projection of redoAction(). */
    bool redo();

    /** @brief Apply last undone/redone tile group to buffer (no-op for opaque actions). */
    /** @brief Apply the latest tile action with a structured failure result. */
    [[nodiscard]] editing::Result<void> applyLastToBufferChecked(TileBuffer& buffer);
    /** @brief Compatibility-only nullable pointer projection of applyLastToBufferChecked(). */
    bool applyLastToBuffer(TileBuffer *buffer);

    std::string getLastActionName() const;
    std::string getLastActionKind() const;  // "opaque" | "tiles"
    std::string getLastPayload() const;
    int getLastTileCount() const;
    int getLastTileX(int index) const;
    int getLastTileY(int index) const;
    int getLastTileOldGid(int index) const;
    int getLastTileNewGid(int index) const;

private:
    struct TileChange {
        int x = 0, y = 0, oldGid = 0, newGid = 0;
    };
    struct Action {
        std::string kind;  // opaque | tiles
        std::string name;
        std::string payload;
        std::vector<TileChange> tiles;
    };

    void trimRedo();
    bool validLastTile(int index) const;

    std::vector<Action> undoStack_;
    std::vector<Action> redoStack_;
    Action pending_;
    bool grouping_ = false;
    Action lastApplied_;
    bool hasLast_ = false;
    bool lastWasUndo_ = false;
};

}  // namespace eve::level_editing
