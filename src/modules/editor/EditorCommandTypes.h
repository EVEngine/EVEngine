#pragma once

namespace eve::editor {

/** @brief Source that initiated an editor command. */
enum class CommandSource {
    Menu,
    Shortcut,
    Toolbar,
    ContextMenu,
    DragDrop,
    Pointer,
    InlineEdit,
    Palette,
    Script,
    Automation,
    Api
};

}  // namespace eve::editor
