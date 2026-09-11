#pragma once

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Tool strip state for assembling editor chrome with `ui`. */
class EditorToolbar {
public:
    void clear();
    void addTool(const std::string &id, const std::string &label);
    /**
     * @brief Assigns a backend-neutral semantic icon name to an existing tool.
     * @param id Stable tool id.
     * @param icon Semantic icon name understood by the presenting UI.
     * @throws Exception when id is unknown.
     */
    void setIcon(const std::string &id, const std::string &icon);
    void setShortcut(const std::string &id, const std::string &key);
    bool setActive(const std::string &id);
    std::string getActive() const { return active_; }

    bool matchShortcut(const std::string &key);

    int getToolCount() const { return static_cast<int>(tools_.size()); }
    std::string getToolId(int index) const;
    std::string getToolLabel(int index) const;
    /**
     * @brief Returns the tool's semantic icon name.
     * @param index Zero-based tool index.
     * @return Owning icon-name snapshot; empty means the presenter should use the label.
     * @throws Exception when index is out of range.
     */
    std::string getToolIcon(int index) const;
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
