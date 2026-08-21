#include "ui/Inspector.h"

#include "common/Module.h"
#include "ui/UIHost.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>

namespace eve::ui {
namespace {

constexpr const char* kInspectorHostName = "eve_inspector";

/** True when two reflected values hold the same data. */
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

std::string memberLabel(const std::string& ownerClass, const std::string& memberName) {
    // "##" keeps the ImGui ID unique per class while hiding the suffix.
    return memberName + "##" + ownerClass + "/" + memberName;
}

/** Editor kind a reflected member maps to. */
enum class EditorKind : uint8_t {
    Checkbox,
    Slider,
    Combo,
    Input,
    ReadOnly,
};

EditorKind editorKind(const ReflectedMember& member, const ReflectedValue& value) {
    const std::string editor = member.attrString("editor");
    if (value.kind == ReflectedValueKind::Bool)
        return editor == "checkbox" ? EditorKind::Checkbox : EditorKind::Checkbox;
    if (editor == "combo" && !member.attrOptions("options").empty())
        return EditorKind::Combo;
    if (editor == "slider" &&
        (value.kind == ReflectedValueKind::Integer ||
         value.kind == ReflectedValueKind::Float))
        return EditorKind::Slider;
    if (value.kind == ReflectedValueKind::Array ||
        value.kind == ReflectedValueKind::Table ||
        value.kind == ReflectedValueKind::Instance ||
        value.kind == ReflectedValueKind::None ||
        value.kind == ReflectedValueKind::Other)
        return EditorKind::ReadOnly;
    return EditorKind::Input;
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

}  // namespace

Inspector::~Inspector() {
    // The ECS host outlives this panel; drop its tree so the stored widget
    // callbacks (which capture `this`) are released while we are still alive.
    if (host_) host_->setTree(window("", {}));
}

Runtime* Inspector::runtime() const {
    return ModuleManager::runtime();
}

const ssq::Object* Inspector::currentInstance() const {
    if (selectedInstance_ < 0 ||
        size_t(selectedInstance_) >= instances_.size())
        return nullptr;
    return &instances_[size_t(selectedInstance_)].object;
}

int Inspector::currentClassIndex() const {
    const auto it = std::find(classNames_.begin(), classNames_.end(), selectedClass_);
    return it == classNames_.end() ? 0 : int(it - classNames_.begin());
}

void Inspector::refresh() {
    classNames_.clear();
    if (Runtime* rt = runtime()) {
        rt->scanClasses();  // picks up dofile()/compilestring() defined classes
        for (const ReflectedClass& cls : rt->reflectedClasses())
            classNames_.push_back(cls.name);
    }
    // Drop a selection whose class vanished (e.g. script unloaded).
    if (!selectedClass_.empty() &&
        std::find(classNames_.begin(), classNames_.end(), selectedClass_) ==
            classNames_.end()) {
        selectedClass_.clear();
        instances_.clear();
        selectedInstance_ = -1;
    }
    if (selectedClass_.empty() && !classNames_.empty())
        selectClass(classNames_.front());
    cachedMembers_.clear();
    rebuildHost();
}

void Inspector::open() {
    if (!host_) host_ = UIHost::createHost(kInspectorHostName);
    host_->setVisible(true);
    host_->setLayer(100);
    refresh();
}

void Inspector::close() {
    if (host_) host_->setVisible(false);
}

bool Inspector::isOpen() const {
    return host_ && host_->meta()->visible;
}

bool Inspector::selectClass(const std::string& name) {
    Runtime* rt = runtime();
    if (!rt ||
        std::find(classNames_.begin(), classNames_.end(), name) == classNames_.end())
        return false;
    navStack_.clear();
    selectedClass_ = name;
    instances_.clear();
    selectedInstance_ = -1;
    cachedMembers_.clear();
    try {
        ssq::Object instance = rt->createInstance(name);
        InstanceEntry entry;
        entry.label = name + " #1";
        entry.object = instance;
        instances_.push_back(std::move(entry));
        selectedInstance_ = 0;
    } catch (...) {
        // Keep the class selected; the panel shows "no instances".
    }
    rebuildHost();
    return selectedInstance_ >= 0;
}

bool Inspector::inspectObject(const ssq::Object& object) {
    Runtime* rt = runtime();
    if (!rt || object.getType() != ssq::Type::INSTANCE) return false;
    const std::string className = rt->classNameOf(object);
    if (className.empty() ||
        std::find(classNames_.begin(), classNames_.end(), className) ==
            classNames_.end())
        return false;
    navStack_.clear();
    selectedClass_ = className;
    instances_.clear();
    InstanceEntry entry;
    entry.label = className + " (live)";
    entry.object = object;
    instances_.push_back(std::move(entry));
    selectedInstance_ = 0;
    cachedMembers_.clear();
    rebuildHost();
    return true;
}

bool Inspector::addInstance() {
    Runtime* rt = runtime();
    if (!rt || selectedClass_.empty()) return false;
    try {
        ssq::Object instance = rt->createInstance(selectedClass_);
        InstanceEntry entry;
        entry.label = selectedClass_ + " #" + std::to_string(instances_.size() + 1);
        entry.object = instance;
        instances_.push_back(std::move(entry));
        selectedInstance_ = int(instances_.size()) - 1;
        cachedMembers_.clear();
        rebuildHost();
        return true;
    } catch (...) {
        return false;
    }
}

void Inspector::setPickScene(std::function<ssq::Object()> pickScene) {
    pickScene_ = std::move(pickScene);
}

bool Inspector::selectInstance(int index) {
    if (index < 0 || index >= int(instances_.size())) return false;
    selectedInstance_ = index;
    cachedMembers_.clear();
    rebuildHost();
    return true;
}

void Inspector::openNested(const std::string& className, const ssq::Object& object) {
    if (object.getType() != ssq::Type::INSTANCE) return;
    if (selectedInstance_ >= 0 &&
        size_t(selectedInstance_) < instances_.size()) {
        NestedEntry entry;
        entry.className = selectedClass_;
        entry.object = instances_[size_t(selectedInstance_)].object;
        navStack_.push_back(std::move(entry));
    }
    selectedClass_ = className;
    instances_.clear();
    InstanceEntry current;
    current.label = className + " (nested)";
    current.object = object;
    instances_.push_back(std::move(current));
    selectedInstance_ = 0;
    cachedMembers_.clear();
    rebuildHost();
}

void Inspector::back() {
    if (navStack_.empty()) return;
    NestedEntry previous = std::move(navStack_.back());
    navStack_.pop_back();
    selectedClass_ = previous.className;
    instances_.clear();
    InstanceEntry current;
    current.label = previous.className + " #1";
    current.object = previous.object;
    instances_.push_back(std::move(current));
    selectedInstance_ = 0;
    cachedMembers_.clear();
    rebuildHost();
}

void Inspector::writeProperty(const std::string& name, ReflectedValue value) {
    Runtime* rt = runtime();
    if (!rt || selectedInstance_ < 0 ||
        size_t(selectedInstance_) >= instances_.size())
        return;
    if (!rt->writeProperty(instances_[size_t(selectedInstance_)].object, name, value))
        return;
    // Cache the written value so sync() does not immediately push it back
    // while the user is still editing the widget.
    for (ReflectedMember& member : cachedMembers_) {
        if (member.name == name) {
            member.value = std::move(value);
            break;
        }
    }
}

WidgetDesc Inspector::propertyWidget(const std::string& ownerClass,
                                     const ReflectedMember& member,
                                     const ReflectedValue& value,
                                     const ssq::Object& instance) {
    const std::string id = "prop_" + member.name;
    const std::string label = memberLabel(ownerClass, member.name);
    switch (editorKind(member, value)) {
        case EditorKind::Checkbox:
            return checkbox(label, value.asBool(), id,
                            [this, name = member.name](bool v) {
                                ReflectedValue out;
                                out.kind = ReflectedValueKind::Bool;
                                out.boolean = v;
                                writeProperty(name, std::move(out));
                            });
        case EditorKind::Slider: {
            const float minV = member.attrFloat("min", 0.f);
            const float maxV = member.attrFloat("max", 1.f);
            const float cur = value.kind == ReflectedValueKind::Integer
                                  ? float(value.integer)
                                  : float(value.floating);
            return slider(label, cur, minV, maxV, id,
                          [this, name = member.name](float v) {
                              ReflectedValue out;
                              out.kind = ReflectedValueKind::Float;
                              out.floating = v;
                              writeProperty(name, std::move(out));
                          });
        }
        case EditorKind::Combo: {
            const std::vector<std::string> options = member.attrOptions("options");
            int index = 0;
            const std::string current =
                value.kind == ReflectedValueKind::String ? value.text
                                                         : valueText(value);
            const auto it = std::find(options.begin(), options.end(), current);
            if (it != options.end()) index = int(it - options.begin());
            return combo(label, options, index, id,
                         [this, name = member.name, kind = value.kind, options](int i) {
                             ReflectedValue out;
                             if (i < 0 || i >= int(options.size())) return;
                             const std::string& option = options[size_t(i)];
                             if (kind == ReflectedValueKind::Integer) {
                                 out.kind = ReflectedValueKind::Integer;
                                 out.integer = std::strtoll(option.c_str(), nullptr, 10);
                             } else if (kind == ReflectedValueKind::Float) {
                                 out.kind = ReflectedValueKind::Float;
                                 out.floating = std::strtod(option.c_str(), nullptr);
                             } else {
                                 out.kind = ReflectedValueKind::String;
                                 out.text = option;
                             }
                             writeProperty(name, std::move(out));
                         });
        }
        case EditorKind::Input:
            return inputText(label, valueText(value), id,
                             [this, name = member.name](const std::string& text) {
                                 ReflectedValue out;
                                 out.kind = ReflectedValueKind::String;
                                 out.text = text;
                                 writeProperty(name, std::move(out));
                             });
        case EditorKind::ReadOnly: {
            if (value.kind == ReflectedValueKind::Array)
                return arrayWidget(ownerClass, member, instance);
            if (value.kind == ReflectedValueKind::Table)
                return tableWidget(ownerClass, member, instance);
            if (value.kind == ReflectedValueKind::Instance) {
                const std::string openId = "open_" + ownerClass + "_" + member.name;
                return button("open " + member.name + "##" + openId, openId,
                              [this, memberName = member.name]() {
                                  Runtime* rt = runtime();
                                  if (!rt) return;
                                  const ssq::Object* inst = currentInstance();
                                  if (!inst) return;
                                  const ssq::Object nested =
                                      rt->readObjectProperty(*inst, memberName);
                                  if (nested.getType() != ssq::Type::INSTANCE) return;
                                  const std::string nestedClass =
                                      rt->classNameOf(nested);
                                  if (nestedClass.empty()) return;
                                  openNested(nestedClass, nested);
                              });
            }
            std::string shown;
            switch (value.kind) {
                default:
                    shown = "null";
                    break;
            }
            return text(member.name + " = " + shown, id);
        }
    }
    return text(member.name, id);
}

WidgetDesc Inspector::arrayWidget(const std::string& ownerClass,
                                  const ReflectedMember& member,
                                  const ssq::Object& instance) {
    Runtime* rt = runtime();
    if (!rt) return text(member.name + " = array", "arr_" + ownerClass + "_" + member.name);
    const size_t size = rt->arraySize(instance, member.name);
    const std::string base = "arr_" + ownerClass + "_" + member.name;
    std::vector<WidgetDesc> rows;
    for (size_t i = 0; i < size; ++i) {
        const ReflectedValue value = rt->arrayGet(instance, member.name, i);
        const std::string elementId = base + "_" + std::to_string(i);
        const std::string elementLabel =
            member.name + "[" + std::to_string(i) + "]##" + elementId;
        WidgetDesc cell;
        if (value.kind == ReflectedValueKind::Bool) {
            cell = checkbox(elementLabel, value.asBool(), elementId,
                            [this, name = member.name, i](bool v) {
                                ReflectedValue out;
                                out.kind = ReflectedValueKind::Bool;
                                out.boolean = v;
                                if (Runtime* rt = runtime()) {
                                    if (const ssq::Object* inst = currentInstance())
                                        rt->arraySet(*inst, name, i, out);
                                }
                            });
        } else if (value.kind == ReflectedValueKind::Array ||
                   value.kind == ReflectedValueKind::Table ||
                   value.kind == ReflectedValueKind::Instance ||
                   value.kind == ReflectedValueKind::None ||
                   value.kind == ReflectedValueKind::Other) {
            cell = text(member.name + "[" + std::to_string(i) + "] = element",
                        elementId);
        } else {
            cell = inputText(elementLabel, valueText(value), elementId,
                             [this, name = member.name, i](
                                 const std::string& text) {
                                 ReflectedValue out;
                                 out.kind = ReflectedValueKind::String;
                                 out.text = text;
                                 if (Runtime* rt = runtime()) {
                                     if (const ssq::Object* inst = currentInstance())
                                         rt->arraySet(*inst, name, i, out);
                                 }
                             });
        }
        rows.push_back(row(
            {std::move(cell),
             button("x##" + elementId + "_del", elementId + "_del",
                    [this, name = member.name, i]() {
                        if (Runtime* rt = runtime()) {
                            if (const ssq::Object* inst = currentInstance())
                                rt->arrayRemove(*inst, name, i);
                        }
                        rebuildHost();
                    })},
            elementId + "_row"));
    }
    rows.push_back(
        row({button("+##" + base + "_add", base + "_add",
                    [this, name = member.name]() {
                        if (Runtime* rt = runtime()) {
                            if (const ssq::Object* inst = currentInstance()) {
                                ReflectedValue out;
                                out.kind = ReflectedValueKind::String;
                                rt->arrayAppend(*inst, name, out);
                            }
                        }
                        rebuildHost();
                    })},
            base + "_addrow"));
    return collapsingHeader(
        member.name + " (array[" + std::to_string(size) + "])##" + base,
        std::move(rows), base, false);
}

WidgetDesc Inspector::tableWidget(const std::string& ownerClass,
                                  const ReflectedMember& member,
                                  const ssq::Object& instance) {
    Runtime* rt = runtime();
    if (!rt) return text(member.name + " = table", "tbl_" + ownerClass + "_" + member.name);
    const std::string base = "tbl_" + ownerClass + "_" + member.name;
    const std::vector<std::string> keys = rt->tableKeys(instance, member.name);
    std::vector<WidgetDesc> rows;
    for (const std::string& key : keys) {
        const ReflectedValue value = rt->tableGet(instance, member.name, key);
        const std::string elementId = base + "_" + key;
        const std::string elementLabel = key + "##" + elementId;
        WidgetDesc cell;
        if (value.kind == ReflectedValueKind::Bool) {
            cell = checkbox(elementLabel, value.asBool(), elementId,
                            [this, name = member.name, key](bool v) {
                                ReflectedValue out;
                                out.kind = ReflectedValueKind::Bool;
                                out.boolean = v;
                                if (Runtime* rt = runtime()) {
                                    if (const ssq::Object* inst = currentInstance())
                                        rt->tableSet(*inst, name, key, out);
                                }
                            });
        } else if (value.kind == ReflectedValueKind::Array ||
                   value.kind == ReflectedValueKind::Table ||
                   value.kind == ReflectedValueKind::Instance ||
                   value.kind == ReflectedValueKind::None ||
                   value.kind == ReflectedValueKind::Other) {
            cell = text(key + " = element", elementId);
        } else {
            cell = inputText(elementLabel, valueText(value), elementId,
                             [this, name = member.name, key](
                                 const std::string& text) {
                                 ReflectedValue out;
                                 out.kind = ReflectedValueKind::String;
                                 out.text = text;
                                 if (Runtime* rt = runtime()) {
                                     if (const ssq::Object* inst = currentInstance())
                                         rt->tableSet(*inst, name, key, out);
                                 }
                             });
        }
        rows.push_back(row(
            {std::move(cell),
             button("x##" + elementId + "_del", elementId + "_del",
                    [this, name = member.name, key]() {
                        if (Runtime* rt = runtime()) {
                            if (const ssq::Object* inst = currentInstance())
                                rt->tableRemove(*inst, name, key);
                        }
                        rebuildHost();
                    })},
            elementId + "_row"));
    }
    rows.push_back(row(
        {button("+##" + base + "_add", base + "_add",
                [this, name = member.name, keys]() {
                    if (Runtime* rt = runtime()) {
                        if (const ssq::Object* inst = currentInstance()) {
                            std::string key = "key" + std::to_string(keys.size());
                            size_t suffix = 0;
                            while (std::find(keys.begin(), keys.end(), key) !=
                                   keys.end())
                                key = "key" + std::to_string(keys.size() + (++suffix));
                            ReflectedValue out;
                            out.kind = ReflectedValueKind::String;
                            rt->tableSet(*inst, name, key, out);
                        }
                    }
                    rebuildHost();
                })},
        base + "_addrow"));
    return collapsingHeader(member.name + " (table)##" + base, std::move(rows), base,
                            false);
}

WidgetDesc Inspector::build() {
    std::vector<WidgetDesc> children;
    if (classNames_.empty()) {
        children.push_back(text(
            "No reflected script classes. Run a script that defines classes, "
            "then call ui.inspectRefresh().",
            "hint"));
    } else {
        std::vector<WidgetDesc> toolbar = {
            text("Class", "lbl_class"),
            spacer("class_spacer"),
            combo("##class", classNames_, currentClassIndex(), "class",
                  [this](int index) {
                      if (index >= 0 && index < int(classNames_.size()))
                          selectClass(classNames_[size_t(index)]);
                  }),
        };
        if (pickScene_) {
            toolbar.push_back(
                button("Pick##inspector_pick", "inspector_pick",
                       [this]() {
                           if (!pickScene_) return;
                           const ssq::Object picked = pickScene_();
                           if (picked.getType() == ssq::Type::INSTANCE)
                               inspectObject(picked);
                       }));
        }
        if (!navStack_.empty()) {
            toolbar.insert(toolbar.begin(),
                           button("<- Back##inspector_back", "inspector_back",
                                  [this]() { back(); }));
        }
        children.push_back(row(
            std::move(toolbar),
            "classrow"));

        std::vector<std::string> labels;
        labels.reserve(instances_.size());
        for (const InstanceEntry& entry : instances_) labels.push_back(entry.label);
        children.push_back(row(
            {
                text("Instance", "lbl_instance"),
                spacer("instance_spacer"),
                combo("##instance", labels, selectedInstance_, "instance",
                      [this](int index) { selectInstance(index); }),
                button("+", "add_instance", [this]() { addInstance(); }),
            },
            "instancerow"));
        children.push_back(separator("inspector_sep"));

        Runtime* rt = runtime();
        if (rt && selectedInstance_ >= 0 &&
            size_t(selectedInstance_) < instances_.size()) {
            const ssq::Object& instance =
                instances_[size_t(selectedInstance_)].object;
            // Inheritance chain: own class first, then bases (parent props are
            // grouped under their owning class header).
            std::vector<std::string> chain;
            std::string current = selectedClass_;
            while (!current.empty()) {
                chain.push_back(current);
                const ReflectedClass* cls = rt->reflectedClass(current);
                if (!cls || cls->base.empty()) break;
                current = cls->base;
            }
            for (const std::string& className : chain) {
                const ReflectedClass* cls = rt->reflectedClass(className);
                if (!cls) continue;
                std::vector<WidgetDesc> props;
                for (const ReflectedMember& member : cls->members) {
                    if (member.method) continue;
                    props.push_back(propertyWidget(
                        className, member, rt->readProperty(instance, member.name),
                        instance));
                }
                const bool own = className == selectedClass_;
                const std::string headerLabel =
                    className + (own ? "" : " (base)") + "##cls_" + className;
                if (props.empty()) {
                    children.push_back(
                        text(className + " (no editable properties)", "cls_" + className));
                } else {
                    children.push_back(collapsingHeader(
                        headerLabel, std::move(props), "cls_" + className, own));
                }
            }
        } else if (rt) {
            children.push_back(text("Instance creation failed for " + selectedClass_,
                                    "instance_error"));
        }
    }
    return window("Inspector", std::move(children), "root");
}

void Inspector::rebuildHost() {
    if (!host_ || !host_->meta()->visible) return;
    host_->setTreeReconcile(build());
}

void Inspector::sync() {
    if (!host_ || !host_->meta()->visible) return;
    Runtime* rt = runtime();
    if (!rt || selectedInstance_ < 0 ||
        size_t(selectedInstance_) >= instances_.size())
        return;
    const ssq::Object& instance = instances_[size_t(selectedInstance_)].object;
    // Members may have changed shape after a script reload; refresh the cache.
    if (cachedMembers_.empty())
        cachedMembers_ = rt->reflectInstance(instance);
    for (ReflectedMember& cached : cachedMembers_) {
        const ReflectedValue live = rt->readProperty(instance, cached.name);
        if (sameValue(live, cached.value)) continue;
        cached.value = live;
        const std::string id = "prop_" + cached.name;
        switch (editorKind(cached, cached.value)) {
            case EditorKind::Checkbox:
                host_->setCheckedById(id, live.asBool());
                break;
            case EditorKind::Slider:
                host_->setValueById(
                    id, live.kind == ReflectedValueKind::Integer
                            ? float(live.integer)
                            : float(live.floating));
                break;
            case EditorKind::Combo: {
                const std::vector<std::string> options =
                    cached.attrOptions("options");
                const std::string current =
                    live.kind == ReflectedValueKind::String ? live.text
                                                            : valueText(live);
                const auto it = std::find(options.begin(), options.end(), current);
                host_->setValueById(id,
                                    it == options.end() ? 0.f
                                                        : float(it - options.begin()));
                break;
            }
            case EditorKind::Input:
                host_->setValueTextById(id, valueText(live));
                break;
            case EditorKind::ReadOnly:
                break;
        }
    }
}

}  // namespace eve::ui
