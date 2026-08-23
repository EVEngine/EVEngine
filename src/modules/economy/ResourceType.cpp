#include "economy/ResourceType.h"

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

void ResourceTypeRegistry::clear() { types().clear(); }

}  // namespace eve::economy
