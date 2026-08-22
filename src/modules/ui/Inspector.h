#pragma once

#include "common/Export.h"
#include "common/Runtime.h"
#include "ui/Widget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <string>
#include <vector>

namespace eve::ui {

class UIHost;

/**
 * @brief Reflection-driven property inspector (DevTools phase C, MVVM).
 *
 * ViewModel: script class instances registered in the active eve Runtime.
 * View: a declarative UIHost ("eve_inspector") rebuilt from reflected members.
 * Binding: widget change callbacks write back to the script instance and
 * `sync()` pulls model values into the tree (two-way).
 *
 * Script classes are auto-scanned from Runtime::reflectedClasses(); members
 * become editable widgets using Squirrel attribute metadata:
 *
 *     </ editor = "slider", min = 0, max = 100 />   → Slider
 *     </ editor = "checkbox" />                     → Checkbox (bool)
 *     </ editor = "combo", options = "a,b,c" />     → Combo
 *     (default)                                     → InputText
 *
 * Inherited members are grouped under their owning (base) class header, so
 * parent properties are editable side-by-side (see docs/dev/界面设计.md).
 */
class EVENGINE_API Inspector {
public:
    Inspector() = default;
    ~Inspector();
    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    /** @brief Re-scans the active Runtime's reflected classes. */
    void refresh();
    /** @brief Mounts (or updates) the inspector host on the UI ECS world. */
    void open();
    /** @brief Hides the inspector host. */
    void close();
    /** @brief True while the inspector host is mounted and visible. */
    bool isOpen() const;
    /** @brief The mounted inspector host (nullptr until open()); for embedding. */
    UIHost* host() const { return host_; }

    /** @brief Selects a class, auto-creating its first instance. */
    bool selectClass(const std::string& name);
    /**
     * @brief Inspects a caller-provided live script instance (the game model).
     *
     * The panel binds to this exact object: edits write back to it and sync()
     * pulls its values into the view. The class must already be reflected.
     */
    bool inspectObject(const ssq::Object& object);
    /** @brief Name of the currently selected class ("" when none). */
    const std::string& selectedClass() const { return selectedClass_; }
    /** @brief Creates another instance of the selected class and selects it. */
    bool addInstance();
    /**
     * @brief Registers the scene-pick source used by the Pick button.
     * @param pickScene Returns the live script instance under the pick cursor
     *                  (empty object when nothing was picked).
     */
    void setPickScene(std::function<ssq::Object()> pickScene);
    /** @brief Selects an instance by index; false when out of range. */
    bool selectInstance(int index);
    /** @brief Navigates into a nested script instance (reference editing). */
    void openNested(const std::string& className, const ssq::Object& object);
    /** @brief Returns to the previously inspected instance. */
    void back();
    /** @brief Number of live instances of the selected class. */
    int instanceCount() const { return int(instances_.size()); }
    /** @brief Index of the selected instance; -1 when none. */
    int selectedIndex() const { return selectedInstance_; }
    /** @brief Live script object of the selected instance (empty when none). */
    ssq::Object selectedInstance() const {
        return (selectedInstance_ >= 0 &&
                size_t(selectedInstance_) < instances_.size())
                   ? instances_[size_t(selectedInstance_)].object
                   : ssq::Object();
    }

    /** @brief Pulls model → view for the selected instance (per-frame). */
    void sync();
    /** @brief Declarative tree of the current selection. */
    WidgetDesc build();

private:
    struct InstanceEntry {
        std::string label;
        ssq::Object object;
    };
    struct NestedEntry {
        std::string className;
        ssq::Object object;
    };

    Runtime* runtime() const;
    const ssq::Object* currentInstance() const;
    int currentClassIndex() const;
    void writeProperty(const std::string& name, ReflectedValue value);
    void rebuildHost();
    WidgetDesc propertyWidget(const std::string& ownerClass,
                              const ReflectedMember& member,
                              const ReflectedValue& value,
                              const ssq::Object& instance);
    WidgetDesc arrayWidget(const std::string& ownerClass, const ReflectedMember& member,
                           const ssq::Object& instance);
    WidgetDesc tableWidget(const std::string& ownerClass, const ReflectedMember& member,
                           const ssq::Object& instance);

    std::vector<std::string> classNames_;
    std::string selectedClass_;
    std::vector<InstanceEntry> instances_;
    std::vector<ReflectedMember> cachedMembers_;
    std::vector<NestedEntry> navStack_;
    std::function<ssq::Object()> pickScene_;
    int selectedInstance_ = -1;
    UIHost* host_ = nullptr;
};

}  // namespace eve::ui
