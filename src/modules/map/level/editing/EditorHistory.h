#pragma once
#include "common/Export.h"


#include "map/level/editing/TileBuffer.h"
#include "editing/EditingResult.h"

#include <string>
#include <vector>

namespace eve::level_editing {

/**
 * @brief Undo/redo stack: opaque string actions + optional tile change groups
 * that can apply directly to a TileBuffer.
 */
class EVENGINE_API_DOMAINS EditorHistory {
public:
    /** @brief Clears . */
    void clear();

    /** @brief Pushes . */
    void push(const std::string &name, const std::string &payload);

    /** @brief Begins group. */
    void beginGroup(const std::string &name);
    /** @brief Record tile. */
    void recordTile(int x, int y, int oldGid, int newGid);
    /** @brief Ends group. */
    void endGroup();
    /** @brief True when grouping. */
    bool isGrouping() const { return grouping_; }

    /** @brief Can undo. */
    bool canUndo() const;
    /** @brief Can redo. */
    bool canRedo() const;
    /** @brief Returns the undo count. */
    int getUndoCount() const;
    /** @brief Returns the redo count. */
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

    /** @brief Returns the last action name. */
    std::string getLastActionName() const;
    /** @brief Returns the last action kind. */
    std::string getLastActionKind() const;  // "opaque" | "tiles"
    /** @brief Returns the last payload. */
    std::string getLastPayload() const;
    /** @brief Returns the last tile count. */
    int getLastTileCount() const;
    /** @brief Returns the last tile x. */
    int getLastTileX(int index) const;
    /** @brief Returns the last tile y. */
    int getLastTileY(int index) const;
    /** @brief Returns the last tile old gid. */
    int getLastTileOldGid(int index) const;
    /** @brief Returns the last tile new gid. */
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
