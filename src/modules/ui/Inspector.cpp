#include "ui/Inspector.h"

#include "common/Module.h"
#include "ui/PropertyView.h"
#include "ui/UIHost.h"

#include <algorithm>
#include <cstdlib>

namespace eve::ui {
namespace {

constexpr const char* kInspectorHostName = "eve_inspector";

std::string valueText(const ReflectedValue& value) {
    switch (value.kind) {
        case ReflectedValueKind::Bool:
            return value.boolean ? "true" : "false";
        case ReflectedValueKind::Integer:
            return std::to_string(value.integer);
        case ReflectedValueKind::Float: {
            return reflectedFloatString(value.floating);
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
    if (auto host = UIHost::resolve(host_)) host->get().setTree(window("", {}));
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
    rebuildPropertyModel();
    rebuildHost();
}

void Inspector::open() {
    auto host = UIHost::resolve(host_);
    if (!host) {
        host_ = UIHost::createHost(kInspectorHostName);
        host  = UIHost::resolve(host_);
    }
    if (!host) return;
    host->get().setVisible(true);
    host->get().setLayer(100);
    refresh();
}

void Inspector::close() {
    if (auto host = UIHost::resolve(host_)) host->get().setVisible(false);
}

bool Inspector::isOpen() const {
    auto host = UIHost::resolve(host_);
    return host && host->get().meta()->visible;
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
    rebuildPropertyModel();
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
    rebuildPropertyModel();
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
        rebuildPropertyModel();
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
    rebuildPropertyModel();
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
    rebuildPropertyModel();
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
    rebuildPropertyModel();
    rebuildHost();
}

void Inspector::rebuildPropertyModel() {
    propertyModel_.reset();
    Runtime* rt = runtime();
    const ssq::Object* instance = currentInstance();
    if (rt && instance)
        propertyModel_ =
            std::make_unique<property_access::ReflectedPropertyModel>(*rt, *instance);
}

PropertyViewOptions Inspector::propertyViewOptions() {
    PropertyViewOptions options;
    options.idPrefix = "prop_";
    options.showAdvanced = true;
    options.showEditorOnly = true;
    options.groupCategories = false;
    options.onStructureChange = [this]() { rebuildHost(); };
    return options;
}

WidgetDesc Inspector::propertyWidget(const std::string& ownerClass,
                                     const ReflectedMember& member,
                                     const ReflectedValue& value,
                                     const ssq::Object& /*instance*/) {
    if (value.kind == ReflectedValueKind::Instance) {
        const std::string openId = "open_" + ownerClass + "_" + member.name;
        return button("open " + member.name + "##" + openId, openId,
                      [this, memberName = member.name]() {
                          Runtime* rt = runtime();
                          if (!rt) return;
                          const ssq::Object* inst = currentInstance();
                          if (!inst) return;
                          const ssq::Object nested = rt->readObjectProperty(*inst, memberName);
                          if (nested.getType() != ssq::Type::INSTANCE) return;
                          const std::string nestedClass = rt->classNameOf(nested);
                          if (nestedClass.empty()) return;
                          openNested(nestedClass, nested);
                      });
    }
    if (propertyModel_) return buildPropertyField(*propertyModel_, member.name, propertyViewOptions());
    return text(member.name + " = " + valueText(value), "prop_" + member.name);
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
                  [this](float index) {
                      const int idx = static_cast<int>(index);
                      if (idx >= 0 && idx < int(classNames_.size()))
                          selectClass(classNames_[size_t(idx)]);
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
                      [this](float index) { selectInstance(static_cast<int>(index)); }),
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
    auto host = UIHost::resolve(host_);
    if (!host || !host->get().meta()->visible) return;
    host->get().setTreeReconcile(build());
}

void Inspector::sync() {
    auto host = UIHost::resolve(host_);
    if (!host || !host->get().meta()->visible) return;
    Runtime* rt = runtime();
    if (!rt || selectedInstance_ < 0 ||
        size_t(selectedInstance_) >= instances_.size())
        return;
    if (!propertyModel_) rebuildPropertyModel();
    if (!propertyModel_) return;
    propertyModel_->refresh();
    syncPropertyView(host->get(), *propertyModel_, propertyViewOptions());
}

}  // namespace eve::ui
