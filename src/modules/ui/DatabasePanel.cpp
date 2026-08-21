#include "ui/DatabasePanel.h"

#include "common/Module.h"
#include "ui/UIHost.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace eve::ui {
namespace {

constexpr const char* kDatabaseHostName = "eve_database";
constexpr float kCellWidth = 160.f;
constexpr float kRowHeight = 30.f;

bool sameValue(const ReflectedValue& a, const ReflectedValue& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case ReflectedValueKind::Bool: return a.boolean == b.boolean;
        case ReflectedValueKind::Integer: return a.integer == b.integer;
        case ReflectedValueKind::Float: return a.floating == b.floating;
        case ReflectedValueKind::String: return a.text == b.text;
        default: return true;
    }
}

std::string valueText(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::Bool:
            return value.boolean ? "true" : "false";
        case ReflectedValueKind::Integer:
            return std::to_string(value.integer);
        case ReflectedValueKind::Float: {
            char buf[32];
            const std::to_chars_result res =
                std::to_chars(buf, buf + sizeof(buf), value.floating);
            if (res.ec == std::errc()) return std::string(buf, res.ptr);
            return std::to_string(value.floating);
        }
        case ReflectedValueKind::String:
            return value.text;
        default:
            return {};
    }
}

/** Cell label keeps the ImGui ID unique per entry while hiding the suffix. */
std::string cellLabel(const std::string& memberName, uint64_t entryId) {
    return memberName + "##db_" + std::to_string(entryId) + "_" + memberName;
}

}  // namespace

DatabasePanel::~DatabasePanel() {
    // The ECS host outlives this panel; drop its tree so the stored widget
    // callbacks (which capture `this`) are released while we are still alive.
    if (host_) host_->setTree(window("", {}));
}

void DatabasePanel::open() {
    if (!host_) host_ = UIHost::createHost(kDatabaseHostName);
    host_->setVisible(true);
    host_->setLayer(90);
    refresh();
}

void DatabasePanel::close() {
    if (host_) host_->setVisible(false);
}

bool DatabasePanel::isOpen() const {
    return host_ && host_->meta()->visible;
}

void DatabasePanel::refresh() {
    classNames_.clear();
    if (Runtime* rt = ModuleManager::runtime()) {
        rt->scanClasses();
        for (const ReflectedClass& cls : rt->reflectedClasses())
            classNames_.push_back(cls.name);
    }
    if (!selectedClass_.empty() &&
        std::find(classNames_.begin(), classNames_.end(), selectedClass_) ==
            classNames_.end()) {
        selectedClass_.clear();
    }
    if (selectedClass_.empty() && !classNames_.empty())
        selectedClass_ = classNames_.front();
    rebuildCache();
    rebuildHost();
}

bool DatabasePanel::selectClass(const std::string& name) {
    if (std::find(classNames_.begin(), classNames_.end(), name) ==
        classNames_.end())
        return false;
    selectedClass_ = name;
    rebuildCache();
    rebuildHost();
    return true;
}

uint64_t DatabasePanel::createInstance() {
    if (selectedClass_.empty()) return 0;
    const uint64_t id = ObjectRegistry::instance().create(selectedClass_);
    if (id != 0) {
        rebuildCache();
        rebuildHost();
    }
    return id;
}

uint64_t DatabasePanel::registerObject(const ssq::Object& object,
                                       const std::string& label) {
    const uint64_t id =
        ObjectRegistry::instance().registerObject(selectedClass_, object, label);
    if (id != 0) {
        rebuildCache();
        rebuildHost();
    }
    return id;
}

bool DatabasePanel::unregister(uint64_t id) {
    const bool removed = ObjectRegistry::instance().unregister(id);
    if (removed) {
        rebuildCache();
        rebuildHost();
    }
    return removed;
}

void DatabasePanel::rebuildCache() {
    entries_ = ObjectRegistry::instance().entries(selectedClass_);
    members_.clear();
    if (const ReflectedClass* cls =
            ModuleManager::runtime() ? ModuleManager::runtime()->reflectedClass(
                                           selectedClass_)
                                     : nullptr) {
        for (const ReflectedMember& member : cls->members) {
            if (!member.method) members_.push_back(member);
        }
    }
    cached_.assign(entries_.size(), std::vector<ReflectedValue>(members_.size()));
    Runtime* rt = ModuleManager::runtime();
    if (!rt) {
        entries_.clear();
        return;
    }
    for (size_t r = 0; r < entries_.size(); ++r) {
        for (size_t c = 0; c < members_.size(); ++c)
            cached_[r][c] = rt->readProperty(entries_[r].object, members_[c].name);
    }
}

WidgetDesc DatabasePanel::cellWidget(const ObjectEntry& entry,
                                     const ReflectedMember& member,
                                     const ReflectedValue& value) {
    const std::string id =
        "cell_" + std::to_string(entry.id) + "_" + member.name;
    const std::string label = cellLabel(member.name, entry.id);
    const std::string editor = member.attrString("editor");
    const std::vector<std::string> options = member.attrOptions("options");
    WidgetDesc cell;
    if (value.kind == ReflectedValueKind::Bool) {
        cell = checkbox(label, value.asBool(), id,
                        [this, entryId = entry.id, name = member.name](bool v) {
                            ReflectedValue out;
                            out.kind = ReflectedValueKind::Bool;
                            out.boolean = v;
                            if (const ObjectEntry* e =
                                    ObjectRegistry::instance().entry(entryId))
                                ModuleManager::runtime()->writeProperty(e->object, name,
                                                                        out);
                        });
    } else if (editor == "combo" && !options.empty()) {
        int index = 0;
        const std::string current =
            value.kind == ReflectedValueKind::String ? value.text : valueText(value);
        const auto it = std::find(options.begin(), options.end(), current);
        if (it != options.end()) index = int(it - options.begin());
        cell = combo(label, options, index, id,
                     [this, entryId = entry.id, name = member.name,
                      kind = value.kind, options](int i) {
                         if (i < 0 || i >= int(options.size())) return;
                         const ObjectEntry* e =
                             ObjectRegistry::instance().entry(entryId);
                         if (!e) return;
                         ReflectedValue out;
                         if (kind == ReflectedValueKind::Integer) {
                             out.kind = ReflectedValueKind::Integer;
                             out.integer =
                                 std::strtoll(options[size_t(i)].c_str(), nullptr, 10);
                         } else if (kind == ReflectedValueKind::Float) {
                             out.kind = ReflectedValueKind::Float;
                             out.floating =
                                 std::strtod(options[size_t(i)].c_str(), nullptr);
                         } else {
                             out.kind = ReflectedValueKind::String;
                             out.text = options[size_t(i)];
                         }
                         ModuleManager::runtime()->writeProperty(e->object, name, out);
                     });
    } else if (value.kind == ReflectedValueKind::Array ||
               value.kind == ReflectedValueKind::Table ||
               value.kind == ReflectedValueKind::Instance ||
               value.kind == ReflectedValueKind::None ||
               value.kind == ReflectedValueKind::Other) {
        std::string shown;
        switch (value.kind) {
            case ReflectedValueKind::Array: shown = "array"; break;
            case ReflectedValueKind::Table: shown = "table"; break;
            case ReflectedValueKind::Instance: shown = "instance"; break;
            default: shown = "null"; break;
        }
        cell = text(member.name + " = " + shown, id);
    } else {
        cell = inputText(label, valueText(value), id,
                         [this, entryId = entry.id, name = member.name,
                          kind = value.kind](const std::string& text) {
                             const ObjectEntry* e =
                                 ObjectRegistry::instance().entry(entryId);
                             if (!e) return;
                             ReflectedValue out;
                             out.kind = ReflectedValueKind::String;
                             out.text = text;
                             ModuleManager::runtime()->writeProperty(e->object, name,
                                                                     out);
                         });
    }
    cell.withSize(kCellWidth, 0.f);
    return cell;
}

WidgetDesc DatabasePanel::build() {
    std::vector<WidgetDesc> children;
    if (classNames_.empty()) {
        children.push_back(text(
            "No reflected script classes. Run a script that defines classes, "
            "then call ui.dbRefresh().",
            "db_hint"));
    } else {
        const auto it =
            std::find(classNames_.begin(), classNames_.end(), selectedClass_);
        const int classIndex = it == classNames_.end() ? 0 : int(it - classNames_.begin());
        children.push_back(row(
            {
                text("Class", "db_lbl_class"),
                spacer("db_class_spacer"),
                combo("##db_class", classNames_, classIndex, "db_class",
                      [this](int index) {
                          if (index >= 0 && index < int(classNames_.size()))
                              selectClass(classNames_[size_t(index)]);
                      }),
                button("+", "db_add", [this]() { createInstance(); }),
            },
            "db_toolbar"));
        children.push_back(separator("db_sep"));

        // Header row: one label per reflected member.
        std::vector<WidgetDesc> header;
        for (const ReflectedMember& member : members_)
            header.push_back(text(member.name, "db_hdr_" + member.name).withSize(kCellWidth, 0.f));
        children.push_back(row(std::move(header), "db_header"));

        // Body: one row per instance, cells bound to the live objects.
        std::vector<WidgetDesc> rows;
        for (size_t r = 0; r < entries_.size(); ++r) {
            const ObjectEntry& entry = entries_[r];
            std::vector<WidgetDesc> cells;
            for (size_t c = 0; c < members_.size(); ++c) {
                const ReflectedValue value = ModuleManager::runtime()->readProperty(
                    entry.object, members_[c].name);
                cells.push_back(cellWidget(entry, members_[c], value));
            }
            cells.push_back(
                button("x##db_" + std::to_string(entry.id),
                       "db_del_" + std::to_string(entry.id),
                       [this, entryId = entry.id]() { unregister(entryId); }));
            rows.push_back(row(std::move(cells), "db_row_" + std::to_string(entry.id)));
        }
        if (rows.empty()) {
            children.push_back(text("No instances of " + selectedClass_ +
                                        ". Press + to create one.",
                                    "db_empty"));
        } else {
            children.push_back(scrollList("db_rows", std::move(rows), 0.f, kRowHeight));
        }
    }
    return window("Database", std::move(children), "root");
}

void DatabasePanel::rebuildHost() {
    if (!host_ || !host_->meta()->visible) return;
    host_->setTreeReconcile(build());
}

void DatabasePanel::sync() {
    if (!host_ || !host_->meta()->visible) return;
    Runtime* rt = ModuleManager::runtime();
    if (!rt || entries_.size() != cached_.size()) return;
    for (size_t r = 0; r < entries_.size(); ++r) {
        for (size_t c = 0; c < members_.size(); ++c) {
            const ReflectedValue live =
                rt->readProperty(entries_[r].object, members_[c].name);
            if (sameValue(live, cached_[r][c])) continue;
            cached_[r][c] = live;
            const std::string id =
                "cell_" + std::to_string(entries_[r].id) + "_" + members_[c].name;
            const ReflectedMember& member = members_[c];
            const std::string editor = member.attrString("editor");
            const std::vector<std::string> options = member.attrOptions("options");
            if (live.kind == ReflectedValueKind::Bool) {
                host_->setCheckedById(id, live.asBool());
            } else if (editor == "combo" && !options.empty()) {
                const std::string current =
                    live.kind == ReflectedValueKind::String ? live.text
                                                            : valueText(live);
                const auto it = std::find(options.begin(), options.end(), current);
                host_->setValueById(id, it == options.end() ? 0.f
                                                            : float(it - options.begin()));
            } else if (live.kind == ReflectedValueKind::Array ||
                       live.kind == ReflectedValueKind::Table ||
                       live.kind == ReflectedValueKind::Instance ||
                       live.kind == ReflectedValueKind::None ||
                       live.kind == ReflectedValueKind::Other) {
                // read-only cell; nothing to patch
            } else {
                host_->setValueTextById(id, valueText(live));
            }
        }
    }
}

}  // namespace eve::ui
