#include "emergence/WatchIndex.h"

#include <algorithm>

namespace eve::emergence {

void WatchIndex::clear() { index_.clear(); }

void WatchIndex::addRule(std::uint32_t ruleIndex, const std::vector<std::string>& keys) {
    for (const auto& key : keys) {
        if (key.empty()) continue;
        auto&      list = index_[key];
        const auto it   = std::lower_bound(list.begin(), list.end(), ruleIndex);
        if (it == list.end() || *it != ruleIndex) list.insert(it, ruleIndex);
    }
}

std::size_t WatchIndex::collect(std::string_view key, std::vector<std::uint32_t>& out) const {
    auto it = index_.find(std::string(key));
    if (it == index_.end()) return 0;
    out.insert(out.end(), it->second.begin(), it->second.end());
    return it->second.size();
}

}  // namespace eve::emergence
