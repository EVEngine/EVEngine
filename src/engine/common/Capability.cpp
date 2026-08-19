#include "common/Capability.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::cap::detail {

namespace {

struct Listener {
    void* impl = nullptr;
    int priority = 0;
};

struct Slot {
    void* service = nullptr;
    std::vector<Listener> listeners;  // kept sorted by priority
};

/**
 * Keyed by capability name rather than by type so a provider compiled into a
 * plugin resolves to the same slot as the host. Function-local so the table is
 * constructed before any static module registration touches it.
 */
std::unordered_map<std::string, Slot>& slots() {
    static std::unordered_map<std::string, Slot> table;
    return table;
}

}  // namespace

void provideRaw(const char* name, void* impl) {
    if (!name) return;
    slots()[name].service = impl;
}

void* queryRaw(const char* name) {
    if (!name) return nullptr;
    auto& table = slots();
    auto it = table.find(name);
    return it == table.end() ? nullptr : it->second.service;
}

void revokeRaw(const char* name, void* impl) {
    if (!name) return;
    auto& table = slots();
    auto it = table.find(name);
    // Only clear when `impl` is still the current provider, so a stale revoke
    // from a torn-down module cannot unregister its replacement.
    if (it != table.end() && it->second.service == impl) it->second.service = nullptr;
}

void addListenerRaw(const char* name, void* impl, int priority) {
    if (!name || !impl) return;
    auto& list = slots()[name].listeners;
    for (const auto& l : list)
        if (l.impl == impl) return;  // idempotent
    // stable_sort keeps registration order within one priority.
    list.push_back({impl, priority});
    std::stable_sort(list.begin(), list.end(),
                     [](const Listener& a, const Listener& b) { return a.priority < b.priority; });
}

void removeListenerRaw(const char* name, void* impl) {
    if (!name) return;
    auto& table = slots();
    auto it = table.find(name);
    if (it == table.end()) return;
    auto& list = it->second.listeners;
    list.erase(std::remove_if(list.begin(), list.end(),
                              [impl](const Listener& l) { return l.impl == impl; }),
               list.end());
}

size_t listenerCountRaw(const char* name) {
    if (!name) return 0;
    auto& table = slots();
    auto it = table.find(name);
    return it == table.end() ? 0 : it->second.listeners.size();
}

void* listenerAtRaw(const char* name, size_t index) {
    if (!name) return nullptr;
    auto& table = slots();
    auto it = table.find(name);
    if (it == table.end() || index >= it->second.listeners.size()) return nullptr;
    return it->second.listeners[index].impl;
}

void clearAllRaw() { slots().clear(); }

}  // namespace eve::cap::detail
