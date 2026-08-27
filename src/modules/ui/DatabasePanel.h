#pragma once

#include "common/Export.h"
#include "common/Runtime.h"
#include "ui/ObjectRegistry.h"
#include "ui/Widget.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::ui {

class UIHost;

/**
 * @brief Reflection-driven database panel (DevTools, MVVM).
 *
 * Left/top: class menu generated from reflected script classes. Below: one
 * editable row per registered instance (ObjectRegistry), with one cell per
 * reflected member. Cell edits write back to the live script instance; sync()
 * pulls model changes back into the grid.
 *
 * Grid cells reuse the declarative widget set (InputText / Checkbox / Combo /
 * read-only text), so no separate table node type is needed.
 */
class EVENGINE_API DatabasePanel {
public:
    DatabasePanel() = default;
    ~DatabasePanel();
    DatabasePanel(const DatabasePanel&) = delete;
    DatabasePanel& operator=(const DatabasePanel&) = delete;

    /** @brief Mounts (or updates) the database host on the UI ECS world. */
    void open();
    /** @brief Hides the database host. */
    void close();
    /** @brief True while the database host is mounted and visible. */
    bool isOpen() const;
    /** @brief Returns the mounted database host handle, or an empty handle until open(). */
    [[nodiscard]] UIHostHandle host() const noexcept { return host_; }

    /** @brief Re-scans reflected classes and refreshes the grid. */
    void refresh();
    /** @brief Selects a class for the grid. */
    bool selectClass(const std::string& name);
    /** @brief Name of the currently selected class ("" when none). */
    const std::string& selectedClass() const { return selectedClass_; }
    /**
     * @brief Creates and registers an instance of the selected class.
     * @return A generation-qualified UI object handle, or a structured failure.
     */
    [[nodiscard]] eve::Result<ObjectHandle> createInstance();
    /**
     * @brief Registers a live script object (auto-derives class when empty).
     * @return A generation-qualified UI object handle, or a structured failure.
     */
    [[nodiscard]] eve::Result<ObjectHandle> registerObject(const ssq::Object& object, const std::string& label = {});
    /**
     * @brief Removes an entry from the registry and refreshes the grid.
     * @param handle The generation-qualified handle to remove.
     * @return Applied on success, or a structured stale/invalid failure.
     */
    [[nodiscard]] eve::Result<void> unregister(ObjectHandle handle);

    /** @brief Pulls model → view for visible cells (per-frame). */
    void sync();
    /** @brief Declarative tree of the current selection. */
    WidgetDesc build();

private:
    void rebuildHost();
    void rebuildCache();
    WidgetDesc cellWidget(const ObjectEntry& entry, const ReflectedMember& member,
                          const ReflectedValue& value);

    UIHostHandle                 host_{};
    std::vector<std::string> classNames_;
    std::string selectedClass_;
    std::vector<ObjectEntry> entries_;
    std::vector<ReflectedMember> members_;
    // Row-major cell cache: [row][col] -> last value pushed to the view.
    std::vector<std::vector<ReflectedValue>> cached_;
};

}  // namespace eve::ui
