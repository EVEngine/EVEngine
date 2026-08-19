#pragma once

#include "editor/TileBuffer.h"

#include <string>
#include <vector>

namespace eve::editor {

/**
 * Undo/redo stack: opaque string actions + optional tile change groups
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

    bool undo();
    bool redo();

    /** Apply last undone/redone tile group to buffer (no-op for opaque actions). */
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

}  // namespace eve::editor
