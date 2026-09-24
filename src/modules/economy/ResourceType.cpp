#include "economy/ResourceType.h"

#include <algorithm>
#include <vector>

namespace eve::economy {

std::unordered_map<std::string, ResourceTypeDef>& ResourceTypeRegistry::types() {
    static std::unordered_map<std::string, ResourceTypeDef> s_types;
    return s_types;
}

bool ResourceTypeRegistry::registerType(const ResourceTypeDef& def) {
    if (def.id.empty()) return false;
    types()[def.id] = def;
    return true;
}

const ResourceTypeDef* ResourceTypeRegistry::find(const std::string& id) {
    auto& all = types();
    auto  it  = all.find(id);
    return it == all.end() ? nullptr : &it->second;
}

int ResourceTypeRegistry::count() { return static_cast<int>(types().size()); }

const ResourceTypeDef* ResourceTypeRegistry::typeAt(int index) {
    if (index < 0) return nullptr;
    std::vector<const std::string*> ids;
    ids.reserve(types().size());
    for (const auto& entry : types()) ids.push_back(&entry.first);
    if (index >= static_cast<int>(ids.size())) return nullptr;
    std::sort(ids.begin(), ids.end(), [](const std::string* left, const std::string* right) { return *left < *right; });
    return find(*ids[static_cast<std::size_t>(index)]);
}

void ResourceTypeRegistry::clear() { types().clear(); }

}  // namespace eve::economy
