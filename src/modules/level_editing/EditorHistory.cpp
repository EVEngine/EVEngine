#include "level_editing/EditorHistory.h"

#include "common/Exception.h"

namespace eve::level_editing {

void EditorHistory::clear() {
    undoStack_.clear();
    redoStack_.clear();
    grouping_ = false;
    pending_ = Action{};
    hasLast_ = false;
    lastWasUndo_ = false;
    lastApplied_ = Action{};
}

void EditorHistory::trimRedo() { redoStack_.clear(); }

void EditorHistory::push(const std::string &name, const std::string &payload) {
    if (grouping_) throw Exception("EditorHistory::push: finish beginGroup/endGroup first");
    Action a;
    a.kind = "opaque";
    a.name = name;
    a.payload = payload;
    undoStack_.push_back(a);
    trimRedo();
}

void EditorHistory::beginGroup(const std::string &name) {
    if (grouping_) throw Exception("EditorHistory::beginGroup: already grouping");
    grouping_ = true;
    pending_ = Action{};
    pending_.kind = "tiles";
    pending_.name = name;
}

void EditorHistory::recordTile(int x, int y, int oldGid, int newGid) {
    if (!grouping_) throw Exception("EditorHistory::recordTile: call beginGroup first");
    TileChange t;
    t.x = x;
    t.y = y;
    t.oldGid = oldGid;
    t.newGid = newGid;
    pending_.tiles.push_back(t);
}

void EditorHistory::endGroup() {
    if (!grouping_) throw Exception("EditorHistory::endGroup: not grouping");
    grouping_ = false;
    if (!pending_.tiles.empty()) {
        undoStack_.push_back(pending_);
        trimRedo();
    }
    pending_ = Action{};
}

bool EditorHistory::canUndo() const { return !undoStack_.empty(); }
bool EditorHistory::canRedo() const { return !redoStack_.empty(); }
int EditorHistory::getUndoCount() const { return static_cast<int>(undoStack_.size()); }
int EditorHistory::getRedoCount() const { return static_cast<int>(redoStack_.size()); }

editing::Result<std::string> EditorHistory::undoAction() {
    if (undoStack_.empty())
        return eve::editing::failed<std::string>(editing::Status::NotFound, editing::RuleId("editor.history.nothing-to-undo"),
                                                "Editor history has no action to undo");
    Action a = undoStack_.back();
    undoStack_.pop_back();
    redoStack_.push_back(a);
    lastApplied_ = a;
    hasLast_ = true;
    lastWasUndo_ = true;
    return eve::editing::applied<std::string>(a.name);
}

editing::Result<std::string> EditorHistory::redoAction() {
    if (redoStack_.empty())
        return eve::editing::failed<std::string>(editing::Status::NotFound, editing::RuleId("editor.history.nothing-to-redo"),
                                                "Editor history has no action to redo");
    Action a = redoStack_.back();
    redoStack_.pop_back();
    undoStack_.push_back(a);
    lastApplied_ = a;
    hasLast_ = true;
    lastWasUndo_ = false;
    return eve::editing::applied<std::string>(a.name);
}

bool EditorHistory::undo() { return undoAction().code() == editing::Status::Applied; }

bool EditorHistory::redo() { return redoAction().code() == editing::Status::Applied; }

editing::Result<void> EditorHistory::applyLastToBufferChecked(TileBuffer& buffer) {
    if (!hasLast_)
        return eve::editing::failed<void>(editing::Status::NotFound, editing::RuleId("editor.history.no-last-action"),
                                         "Editor history has no applied action");
    if (lastApplied_.kind != "tiles")
        return eve::editing::failed<void>(editing::Status::Unsupported, editing::RuleId("editor.history.action-not-tiles"),
                                         "The latest history action does not contain tile changes");
    for (const auto &t : lastApplied_.tiles) {
        if (!buffer.containsCell(t.x, t.y)) continue;
        buffer.setGid(t.x, t.y, lastWasUndo_ ? t.oldGid : t.newGid);
    }
    return eve::editing::applied<void>();
}

bool EditorHistory::applyLastToBuffer(TileBuffer *buffer) {
    return buffer && applyLastToBufferChecked(*buffer).ok();
}

std::string EditorHistory::getLastActionName() const { return hasLast_ ? lastApplied_.name : ""; }
std::string EditorHistory::getLastActionKind() const { return hasLast_ ? lastApplied_.kind : ""; }
std::string EditorHistory::getLastPayload() const { return hasLast_ ? lastApplied_.payload : ""; }

int EditorHistory::getLastTileCount() const {
    return hasLast_ ? static_cast<int>(lastApplied_.tiles.size()) : 0;
}

bool EditorHistory::validLastTile(int index) const {
    return hasLast_ && index >= 0 && index < static_cast<int>(lastApplied_.tiles.size());
}

int EditorHistory::getLastTileX(int index) const {
    if (!validLastTile(index)) throw Exception("EditorHistory::getLastTileX: bad index");
    return lastApplied_.tiles[index].x;
}
int EditorHistory::getLastTileY(int index) const {
    if (!validLastTile(index)) throw Exception("EditorHistory::getLastTileY: bad index");
    return lastApplied_.tiles[index].y;
}
int EditorHistory::getLastTileOldGid(int index) const {
    if (!validLastTile(index)) throw Exception("EditorHistory::getLastTileOldGid: bad index");
    return lastApplied_.tiles[index].oldGid;
}
int EditorHistory::getLastTileNewGid(int index) const {
    if (!validLastTile(index)) throw Exception("EditorHistory::getLastTileNewGid: bad index");
    return lastApplied_.tiles[index].newGid;
}

}  // namespace eve::level_editing
