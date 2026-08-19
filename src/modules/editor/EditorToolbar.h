#pragma once

#include <string>
#include <vector>

namespace eve::editor {

/** Tool strip state for assembling editor chrome with `ui`. */
class EditorToolbar {
public:
    void clear();
    void addTool(const std::string &id, const std::string &label);
    void setShortcut(const std::string &id, const std::string &key);
    bool setActive(const std::string &id);
    std::string getActive() const { return active_; }

    bool matchShortcut(const std::string &key);

    int getToolCount() const { return static_cast<int>(tools_.size()); }
    std::string getToolId(int index) const;
    std::string getToolLabel(int index) const;
    std::string getToolShortcut(int index) const;

private:
    struct Tool {
        std::string id;
        std::string label;
        std::string shortcut;
    };

    int findIndex(const std::string &id) const;

    std::vector<Tool> tools_;
    std::string active_;
};

}  // namespace eve::editor
