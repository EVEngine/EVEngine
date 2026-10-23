#include "common/CapabilityOwned.h"

namespace eve::cap::detail {
namespace {

struct OwnedTables {
    std::mutex                                                      mutex;
    std::unordered_map<std::string, std::unordered_map<std::string, OwnedSlot>> byCapability;
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> generations;
};

OwnedTables& tables() {
    static OwnedTables state;
    return state;
}

}  // namespace

std::mutex& ownedMutex() { return tables().mutex; }

std::unordered_map<std::string, OwnedSlot>& ownedSlots(const char* capabilityName) {
    return tables().byCapability[capabilityName ? capabilityName : ""];
}

std::unordered_map<std::string, std::uint64_t>& ownedGenerations(const char* capabilityName) {
    return tables().generations[capabilityName ? capabilityName : ""];
}

void clearOwnedRaw() {
    std::scoped_lock lock(tables().mutex);
    tables().byCapability.clear();
    tables().generations.clear();
}

}  // namespace eve::cap::detail
