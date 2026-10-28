#include "ui/Icons.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace eve::ui {
namespace {

struct IconEntry {
    Icon icon;
    const char *name;
    const char *glyph;
};

// UTF-8 codepoints match Font Awesome 4.7's canonical CSS mapping.
constexpr std::array kIcons{
    IconEntry{Icon::None, "none", ""},
    IconEntry{Icon::Search, "search", "\xEF\x80\x82"},
    IconEntry{Icon::Close, "close", "\xEF\x80\x8D"},
    IconEntry{Icon::Check, "check", "\xEF\x80\x8C"},
    IconEntry{Icon::Settings, "settings", "\xEF\x80\x93"},
    IconEntry{Icon::Home, "home", "\xEF\x80\x95"},
    IconEntry{Icon::File, "file", "\xEF\x85\x9B"},
    IconEntry{Icon::Folder, "folder", "\xEF\x81\xBB"},
    IconEntry{Icon::FolderOpen, "folder-open", "\xEF\x81\xBC"},
    IconEntry{Icon::Save, "save", "\xEF\x83\x87"},
    IconEntry{Icon::Undo, "undo", "\xEF\x83\xA2"},
    IconEntry{Icon::Redo, "redo", "\xEF\x80\x9E"},
    IconEntry{Icon::Refresh, "refresh", "\xEF\x80\xA1"},
    IconEntry{Icon::Plus, "plus", "\xEF\x81\xA7"},
    IconEntry{Icon::Minus, "minus", "\xEF\x81\xA8"},
    IconEntry{Icon::ChevronLeft, "chevron-left", "\xEF\x81\x93"},
    IconEntry{Icon::ChevronRight, "chevron-right", "\xEF\x81\x94"},
    IconEntry{Icon::ChevronUp, "chevron-up", "\xEF\x81\xB7"},
    IconEntry{Icon::ChevronDown, "chevron-down", "\xEF\x81\xB8"},
    IconEntry{Icon::Menu, "menu", "\xEF\x83\x89"},
    IconEntry{Icon::Grid, "grid", "\xEF\x80\x8A"},
    IconEntry{Icon::List, "list", "\xEF\x80\xBA"},
    IconEntry{Icon::Eye, "eye", "\xEF\x81\xAE"},
    IconEntry{Icon::EyeSlash, "eye-slash", "\xEF\x81\xB0"},
    IconEntry{Icon::Lock, "lock", "\xEF\x80\xA3"},
    IconEntry{Icon::Unlock, "unlock", "\xEF\x82\x9C"},
    IconEntry{Icon::Play, "play", "\xEF\x81\x8B"},
    IconEntry{Icon::Pause, "pause", "\xEF\x81\x8C"},
    IconEntry{Icon::Stop, "stop", "\xEF\x81\x8D"},
    IconEntry{Icon::Move, "move", "\xEF\x81\x87"},
    IconEntry{Icon::Pointer, "pointer", "\xEF\x89\x85"},
    IconEntry{Icon::Pencil, "pencil", "\xEF\x81\x80"},
    IconEntry{Icon::PaintBrush, "paint-brush", "\xEF\x87\xBC"},
    IconEntry{Icon::Image, "image", "\xEF\x80\xBE"},
    IconEntry{Icon::Camera, "camera", "\xEF\x80\xB0"},
    IconEntry{Icon::Cube, "cube", "\xEF\x86\xB2"},
    IconEntry{Icon::Cubes, "cubes", "\xEF\x86\xB3"},
    IconEntry{Icon::Database, "database", "\xEF\x87\x80"},
    IconEntry{Icon::Wrench, "wrench", "\xEF\x82\xAD"},
    IconEntry{Icon::Sliders, "sliders", "\xEF\x87\x9E"},
    IconEntry{Icon::Info, "info", "\xEF\x81\x9A"},
    IconEntry{Icon::Question, "question", "\xEF\x81\x99"},
    IconEntry{Icon::Warning, "warning", "\xEF\x81\xB1"},
    IconEntry{Icon::Code, "code", "\xEF\x84\xA1"},
    IconEntry{Icon::Bug, "bug", "\xEF\x86\x88"},
    IconEntry{Icon::Terminal, "terminal", "\xEF\x84\xA0"},
    IconEntry{Icon::Layers, "layers", "\xEF\x89\x8D"},
    IconEntry{Icon::User, "user", "\xEF\x80\x87"},
    IconEntry{Icon::Users, "users", "\xEF\x83\x80"},
};

std::string normalize(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
        if (c == '_' || c == ' ') return '-';
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

const IconEntry *findEntry(Icon icon) {
    const auto it = std::find_if(kIcons.begin(), kIcons.end(),
                                 [icon](const IconEntry &entry) { return entry.icon == icon; });
    return it == kIcons.end() ? &kIcons.front() : &*it;
}

}  // namespace

const char *iconGlyph(Icon icon) { return findEntry(icon)->glyph; }

const char *iconName(Icon icon) { return findEntry(icon)->name; }

bool iconFromName(std::string_view name, Icon *out) {
    if (!out) return false;
    const std::string key = normalize(name);
    const auto it = std::find_if(kIcons.begin(), kIcons.end(), [&](const IconEntry &entry) {
        return key == entry.name;
    });
    if (it == kIcons.end()) return false;
    *out = it->icon;
    return true;
}

std::string iconText(Icon icon, std::string_view label) {
    std::string result = iconGlyph(icon);
    if (!label.empty()) {
        if (!result.empty()) result += ' ';
        result.append(label.data(), label.size());
    }
    return result;
}

}  // namespace eve::ui
