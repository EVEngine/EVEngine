#include "common/BootWarmup.h"
#include "common/Capability.h"

#include <algorithm>
#include <mutex>
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

/** One registry-owned provider generation. `generation` 0 means "never published". */
struct OwnedSlot {
    std::shared_ptr<void> provider;
    std::uint64_t         generation = 0;
};

/**
 * Registry-owned providers live in their own table so the caller-owned path above
 * keeps its exact semantics (raw pointers, no lock, lifetime is the caller's).
 * This table is mutated at publish/unload time only, so a mutex is affordable
 * here and absent from the hot caller-owned lookups.
 */
std::unordered_map<std::string, OwnedSlot>& ownedSlots() {
    static std::unordered_map<std::string, OwnedSlot> table;
    return table;
}

std::mutex& ownedMutex() {
    static std::mutex mutex;
    return mutex;
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

void clearAllRaw() {
    slots().clear();
    std::lock_guard<std::mutex> guard(ownedMutex());
    ownedSlots().clear();
}

std::uint64_t publishOwnedRaw(const char* name, std::shared_ptr<void> provider) {
    if (!name || !provider) return 0;
    std::lock_guard<std::mutex> guard(ownedMutex());
    OwnedSlot& slot = ownedSlots()[name];
    slot.provider   = std::move(provider);
    // Generation 0 is reserved for "never published", so the first publish is 1.
    return ++slot.generation;
}

std::shared_ptr<void> acquireOwnedRaw(const char* name, std::uint64_t generation) {
    if (!name || generation == 0) return {};
    std::lock_guard<std::mutex> guard(ownedMutex());
    auto it = ownedSlots().find(name);
    if (it == ownedSlots().end() || it->second.generation != generation) return {};
    // Returns empty after a retirement, so the caller reports StaleHandle rather
    // than handing out a destroyed implementation.
    return it->second.provider;
}

bool unloadOwnedRaw(const char* name, std::uint64_t generation) {
    if (!name || generation == 0) return false;
    std::lock_guard<std::mutex> guard(ownedMutex());
    auto it = ownedSlots().find(name);
    if (it == ownedSlots().end() || it->second.generation != generation) return false;
    it->second.provider.reset();
    // Advance past the retired generation so the retired handle never resolves
    // again, even if the same capability is republished later.
    ++it->second.generation;
    return true;
}

std::uint64_t ownedGenerationRaw(const char* name) {
    if (!name) return 0;
    std::lock_guard<std::mutex> guard(ownedMutex());
    auto it = ownedSlots().find(name);
    if (it == ownedSlots().end() || !it->second.provider) return 0;
    return it->second.generation;
}

}  // namespace eve::cap::detail

namespace eve::boot {
namespace {

void (*gVulkanWarmup)() = nullptr;

}  // namespace

void registerVulkanInstanceWarmup(void (*start)()) { gVulkanWarmup = start; }

void startVulkanInstanceWarmup() {
    if (gVulkanWarmup) gVulkanWarmup();
}

}  // namespace eve::boot
