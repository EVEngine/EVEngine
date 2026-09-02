#include "level_editor/LevelEditorModule.h"

#include "editor/EditorSession.h"
#include "level_editing/Brush.h"
#include "level_editing/EditorHistory.h"
#include "level_editing/FieldTargets.h"
#include "level_editing/TileBuffer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <functional>
#include <utility>

namespace eve::level_editor {

Module_IMPL(LevelEditorModule, new LevelEditorModule());

std::unique_ptr<level_editing::TileBuffer> LevelEditorModule::newTileBuffer(int width, int height) const {
    return std::make_unique<level_editing::TileBuffer>(width, height);
}

std::unique_ptr<level_editing::Brush> LevelEditorModule::newBrush() const {
    return std::make_unique<level_editing::Brush>();
}

std::unique_ptr<level_editing::EditorHistory> LevelEditorModule::newHistory() const {
    return std::make_unique<level_editing::EditorHistory>();
}

std::unique_ptr<level_editing::TileBufferTarget> LevelEditorModule::newTarget(std::string                id,
                                                                              level_editing::TileBuffer* buffer) const {
    return std::make_unique<level_editing::TileBufferTarget>(std::move(id), buffer);
}

void LevelEditorModule::bind(editor::EditorSession* session, level_editing::TileBufferTarget* target) const {
    if (session) session->bindTarget(target);
}

void LevelEditorModule::expose(ssq::Table& table) {
    auto buffer = table.addClass<level_editing::TileBuffer>(
        "TileBuffer", std::function<level_editing::TileBuffer*()>([]() { return nullptr; }), true);
    buffer.addFunc("getWidth", &level_editing::TileBuffer::getWidth);
    buffer.addFunc("getHeight", &level_editing::TileBuffer::getHeight);
    buffer.addFunc("resize", &level_editing::TileBuffer::resize);
    buffer.addFunc("clear", &level_editing::TileBuffer::clear);
    buffer.addFunc("fill", &level_editing::TileBuffer::fill);
    buffer.addFunc("setGid", &level_editing::TileBuffer::setGid);
    buffer.addFunc("getGid", &level_editing::TileBuffer::getGid);
    buffer.addFunc("inBounds", &level_editing::TileBuffer::containsCell);

    auto brush = table.addClass<level_editing::Brush>(
        "Brush", std::function<level_editing::Brush*()>([]() { return nullptr; }), true);
    brush.addFunc("setTool", &level_editing::Brush::setTool);
    brush.addFunc("getTool", &level_editing::Brush::getTool);
    brush.addFunc("setSize", &level_editing::Brush::setSize);
    brush.addFunc("getSize", &level_editing::Brush::getSize);
    brush.addFunc("setShape", &level_editing::Brush::setShape);
    brush.addFunc("getShape", &level_editing::Brush::getShape);
    brush.addFunc("setTile", &level_editing::Brush::setTile);
    brush.addFunc("getTile", &level_editing::Brush::getTile);
    brush.addFunc("setEraseTile", &level_editing::Brush::setEraseTile);
    brush.addFunc("getEraseTile", &level_editing::Brush::getEraseTile);
    brush.addFunc("setStampSize", &level_editing::Brush::setStampSize);
    brush.addFunc("getStampWidth", &level_editing::Brush::getStampWidth);
    brush.addFunc("getStampHeight", &level_editing::Brush::getStampHeight);
    brush.addFunc("setStampTile", &level_editing::Brush::setStampTile);
    brush.addFunc("getStampTile", &level_editing::Brush::getStampTile);
    brush.addFunc("clearStamp", &level_editing::Brush::clearStamp);
    brush.addFunc("paintAt", &level_editing::Brush::paintAt);
    brush.addFunc("eraseAt", &level_editing::Brush::eraseAt);
    brush.addFunc("floodFill", &level_editing::Brush::floodFill);
    brush.addFunc("paintLine", &level_editing::Brush::paintLine);
    brush.addFunc("paintRect", &level_editing::Brush::paintRect);
    brush.addFunc("previewAt", &level_editing::Brush::previewAt);
    brush.addFunc("previewLine", &level_editing::Brush::previewLine);
    brush.addFunc("previewRect", &level_editing::Brush::previewRect);
    brush.addFunc("getPreviewCount", &level_editing::Brush::getPreviewCount);
    brush.addFunc("getPreviewX", &level_editing::Brush::getPreviewX);
    brush.addFunc("getPreviewY", &level_editing::Brush::getPreviewY);
    brush.addFunc("getPreviewGid", &level_editing::Brush::getPreviewGid);
    brush.addFunc("getChangeCount", &level_editing::Brush::getChangeCount);
    brush.addFunc("getChangeX", &level_editing::Brush::getChangeX);
    brush.addFunc("getChangeY", &level_editing::Brush::getChangeY);
    brush.addFunc("getChangeOldGid", &level_editing::Brush::getChangeOldGid);
    brush.addFunc("getChangeNewGid", &level_editing::Brush::getChangeNewGid);

    auto history = table.addClass<level_editing::EditorHistory>(
        "EditorHistory", std::function<level_editing::EditorHistory*()>([]() { return nullptr; }), true);
    history.addFunc("clear", &level_editing::EditorHistory::clear);
    history.addFunc("push", &level_editing::EditorHistory::push);
    history.addFunc("beginGroup", &level_editing::EditorHistory::beginGroup);
    history.addFunc("recordTile", &level_editing::EditorHistory::recordTile);
    history.addFunc("endGroup", &level_editing::EditorHistory::endGroup);
    history.addFunc("isGrouping", &level_editing::EditorHistory::isGrouping);
    history.addFunc("canUndo", &level_editing::EditorHistory::canUndo);
    history.addFunc("canRedo", &level_editing::EditorHistory::canRedo);
    history.addFunc("getUndoCount", &level_editing::EditorHistory::getUndoCount);
    history.addFunc("getRedoCount", &level_editing::EditorHistory::getRedoCount);
    history.addFunc("undo", &level_editing::EditorHistory::undo);
    history.addFunc("redo", &level_editing::EditorHistory::redo);
    history.addFunc("applyLastToBuffer", &level_editing::EditorHistory::applyLastToBuffer);
    history.addFunc("getLastActionName", &level_editing::EditorHistory::getLastActionName);
    history.addFunc("getLastActionKind", &level_editing::EditorHistory::getLastActionKind);
    history.addFunc("getLastPayload", &level_editing::EditorHistory::getLastPayload);
    history.addFunc("getLastTileCount", &level_editing::EditorHistory::getLastTileCount);
    history.addFunc("getLastTileX", &level_editing::EditorHistory::getLastTileX);
    history.addFunc("getLastTileY", &level_editing::EditorHistory::getLastTileY);
    history.addFunc("getLastTileOldGid", &level_editing::EditorHistory::getLastTileOldGid);
    history.addFunc("getLastTileNewGid", &level_editing::EditorHistory::getLastTileNewGid);

    auto target = table.addClass<level_editing::TileBufferTarget>(
        "TileBufferTarget", std::function<level_editing::TileBufferTarget*()>([]() { return nullptr; }), true);
    target.addFunc("getTargetId", [](level_editing::TileBufferTarget* self) {
        return self ? self->targetId().value() : std::string{};
    });
    target.addFunc("getRevision", [](level_editing::TileBufferTarget* self) {
        return self ? static_cast<std::int64_t>(self->revision()) : std::int64_t{0};
    });
    target.addFunc("getWidth", &level_editing::TileBufferTarget::width);
    target.addFunc("getHeight", &level_editing::TileBufferTarget::height);
    target.addFunc("readInt", &level_editing::TileBufferTarget::readInt);
    target.addFunc("writeInt", &level_editing::TileBufferTarget::writeInt);
    target.addFunc("clearDirtyRegion", &level_editing::TileBufferTarget::clearDirtyRegion);

    auto module = table.addClass(name, LevelEditorModule::create, false);
    expose(module);
}

void LevelEditorModule::expose(ssq::Class& cls) {
    cls.addFunc("newTileBuffer", [](LevelEditorModule* self, int width, int height) {
        return self->newTileBuffer(width, height).release();
    });
    cls.addFunc("newBrush", [](LevelEditorModule* self) { return self->newBrush().release(); });
    cls.addFunc("newHistory", [](LevelEditorModule* self) { return self->newHistory().release(); });
    cls.addFunc("createTarget", [](LevelEditorModule* self, const std::string& id, level_editing::TileBuffer* buffer) {
        return self->newTarget(id, buffer).release();
    });
    cls.addFunc("bind", &LevelEditorModule::bind);
}

}  // namespace eve::level_editor
