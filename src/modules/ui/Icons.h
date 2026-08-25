#pragma once

#include "common/Export.h"

#include <string>
#include <string_view>

namespace eve::ui {

/** @brief Semantic editor icons backed by the bundled Font Awesome 4.7 font. */
enum class Icon {
    None,
    Search,
    Close,
    Check,
    Settings,
    Home,
    File,
    Folder,
    FolderOpen,
    Save,
    Undo,
    Redo,
    Refresh,
    Plus,
    Minus,
    ChevronLeft,
    ChevronRight,
    ChevronUp,
    ChevronDown,
    Menu,
    Grid,
    List,
    Eye,
    EyeSlash,
    Lock,
    Unlock,
    Play,
    Pause,
    Stop,
    Move,
    Pointer,
    Pencil,
    PaintBrush,
    Image,
    Camera,
    Cube,
    Cubes,
    Database,
    Wrench,
    Sliders,
    Info,
    Question,
    Warning,
    Code,
    Bug,
    Terminal,
    Layers,
    User,
    Users,
};

/** @brief Returns the UTF-8 glyph for an icon, or an empty string for None. */
EVENGINE_API const char *iconGlyph(Icon icon);
/** @brief Returns the stable kebab-case semantic name for an icon. */
EVENGINE_API const char *iconName(Icon icon);
/** @brief Resolves a semantic icon name; accepts kebab-case names case-insensitively. */
EVENGINE_API bool iconFromName(std::string_view name, Icon *out);
/** @brief Composes an icon with an optional visible label. */
EVENGINE_API std::string iconText(Icon icon, std::string_view label = {});

}  // namespace eve::ui
