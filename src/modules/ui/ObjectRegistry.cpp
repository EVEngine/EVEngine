#include "ui/ObjectRegistry.h"

#include "common/Module.h"
#include "common/Runtime.h"

#include <algorithm>

namespace eve::ui {

ObjectRegistry& ObjectRegistry::instance() {
    static ObjectRegistry registry;
    return registry;
}

uint64_t ObjectRegistry::create(const std::string& className) {
    if (className.empty()) return 0;
    Runtime* rt = ModuleManager::runtime();
    if (!rt) return 0;
    try {
        return registerObject(className, rt->createInstance(className));
    } catch (...) {
        return 0;
    }
}

uint64_t ObjectRegistry::registerObject(const std::string& className,
                                        const ssq::Object& object,
                                        const std::string& label) {
    if (object.getType() != ssq::Type::INSTANCE) return 0;
    std::string group = className;
    if (group.empty()) {
        Runtime* rt = ModuleManager::runtime();
        if (!rt) return 0;
        group = rt->classNameOf(object);
        if (group.empty()) return 0;
    }
    const uint64_t id = nextId_++;
    ObjectEntry entry;
    entry.id = id;
    entry.className = group;
    entry.object = object;
    entry.label = label.empty() ? group + " #" + std::to_string(count(group) + 1)
                                : label;
    byClass_[group].push_back(std::move(entry));
    return id;
}

bool ObjectRegistry::unregister(uint64_t id) {
    for (auto& pair : byClass_) {
        auto& entries = pair.second;
        const auto it = std::find_if(entries.begin(), entries.end(),
                                     [&](const ObjectEntry& e) { return e.id == id; });
        if (it == entries.end()) continue;
        entries.erase(it);
        if (entries.empty()) byClass_.erase(pair.first);
        return true;
    }
    return false;
}

void ObjectRegistry::clear(const std::string& className) {
    byClass_.erase(className);
}

void ObjectRegistry::clearAll() {
    byClass_.clear();
}

std::vector<std::string> ObjectRegistry::classNames() const {
    std::vector<std::string> names;
    names.reserve(byClass_.size());
    for (const auto& pair : byClass_) names.push_back(pair.first);
    return names;
}

std::vector<ObjectEntry> ObjectRegistry::entries(const std::string& className) const {
    const auto found = byClass_.find(className);
    return found == byClass_.end() ? std::vector<ObjectEntry>()
                                   : found->second;
}

const ObjectEntry* ObjectRegistry::entry(uint64_t id) const {
    for (const auto& pair : byClass_) {
        for (const ObjectEntry& e : pair.second) {
            if (e.id == id) return &e;
        }
    }
    return nullptr;
}

size_t ObjectRegistry::count(const std::string& className) const {
    const auto found = byClass_.find(className);
    return found == byClass_.end() ? 0 : found->second.size();
}

}  // namespace eve::ui
