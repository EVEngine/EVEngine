#pragma once

#include <vector>

namespace eve::spatial {

/**
 * Last-query hit buffer shared by spatial indexes.
 * Script pattern: call query* → getResultCount / getResultId(i).
 */
class QueryIds {
public:
    void clear() { ids_.clear(); }

    void addUnique(int id) {
        for (int existing : ids_) {
            if (existing == id) return;
        }
        ids_.push_back(id);
    }

    void addUnchecked(int id) { ids_.push_back(id); }

    int getCount() const { return static_cast<int>(ids_.size()); }

    int getId(int index) const {
        if (index < 0 || index >= getCount()) return -1;
        return ids_[static_cast<size_t>(index)];
    }

    const std::vector<int> &ids() const { return ids_; }
    std::vector<int>       &ids() { return ids_; }

private:
    std::vector<int> ids_;
};

}  // namespace eve::spatial
