#pragma once

#include <vector>

namespace eve::spatial {

/**
 * @brief Last-query hit buffer shared by spatial indexes.
 * Script pattern: call query* → getResultCount / getResultId(i).
 */
class QueryIds {
public:
    /** @brief Removes all stored entries. */
    void clear() { ids_.clear(); }

    /** @brief Appends id if not already present. */
    void addUnique(int id) {
        for (int existing : ids_) {
            if (existing == id) return;
        }
        ids_.push_back(id);
    }

    /** @brief Appends id without uniqueness checks. */
    void addUnchecked(int id) { ids_.push_back(id); }

    /** @brief Number of stored ids. */
    int getCount() const { return static_cast<int>(ids_.size()); }

    /** @brief Id at dense index, or -1 if out of range. */
    int getId(int index) const {
        /** @brief Number of stored ids. */
        if (index < 0 || index >= getCount()) return -1;
        return ids_[static_cast<size_t>(index)];
    }

    const std::vector<int> &ids() const { return ids_; }
    std::vector<int>       &ids() { return ids_; }

private:
    std::vector<int> ids_;
};

}  // namespace eve::spatial
