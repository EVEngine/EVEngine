#pragma once
#include "common/Export.h"


#include <string>
#include <vector>

namespace eve::editor {

/** @brief Tool strip state for assembling editor chrome with `ui`. */
class EVENGINE_API_ORCHESTRATION EditorToolbar {
public:
    /** @brief Clears . */
    void clear();
    /** @brief Adds tool. */
    void addTool(const std::string &id, const std::string &label);
    /**
     * @brief Assigns a backend-neutral semantic icon name to an existing tool.
     * @param id Stable tool id.
     * @param icon Semantic icon name understood by the presenting UI.
     * @throws Exception when id is unknown.
     */
    void setIcon(const std::string &id, const std::string &icon);
    /** @brief Sets the shortcut. */
    void setShortcut(const std::string &id, const std::string &key);
    /** @brief Sets the active. */
    bool setActive(const std::string &id);
    /** @brief Returns the active. */
    std::string getActive() const { return active_; }

    /** @brief Match shortcut. */
    bool matchShortcut(const std::string &key);

    /** @brief Returns the tool count. */
    int getToolCount() const { return static_cast<int>(tools_.size()); }
    /** @brief Returns the tool id. */
    std::string getToolId(int index) const;
    /** @brief Returns the tool label. */
    std::string getToolLabel(int index) const;
    /**
     * @brief Returns the tool's semantic icon name.
     * @param index Zero-based tool index.
     * @return Owning icon-name snapshot; empty means the presenter should use the label.
     * @throws Exception when index is out of range.
     */
    std::string getToolIcon(int index) const;
    /** @brief Returns the tool shortcut. */
    std::string getToolShortcut(int index) const;

private:
    struct Tool {
        std::string id;
        std::string label;
        std::string icon;
        std::string shortcut;
    };

    int findIndex(const std::string &id) const;

    std::vector<Tool> tools_;
    std::string active_;
};

}  // namespace eve::editor
