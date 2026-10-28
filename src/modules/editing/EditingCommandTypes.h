#pragma once

namespace eve::editing {

/** @brief Source that initiated an authoring command. */
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

}  // namespace eve::editing
