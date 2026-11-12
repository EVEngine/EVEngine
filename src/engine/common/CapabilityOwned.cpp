#include "common/CapabilityOwned.h"

namespace eve::cap::detail {
namespace {
struct OwnedTables {
    std::mutex                                                               mutex;
    std::mutex                                                               publicationMutex;
    std::unordered_map<std::string, std::unique_ptr<ProviderRegistry<void>>> registries;
};
OwnedTables& tables() {
    static OwnedTables state;
    return state;
}
}  // namespace

std::mutex& ownedPublicationMutex() { return tables().publicationMutex; }

ProviderRegistry<void>& ownedRegistry(const char* capabilityName) {
    std::scoped_lock lock(tables().mutex);
    auto&            registry = tables().registries[capabilityName ? capabilityName : ""];
    if (!registry) registry = std::make_unique<ProviderRegistry<void>>();
    return *registry;
}

void clearOwnedRaw() {
    decltype(OwnedTables::registries) retired;
    {
        std::scoped_lock lock(tables().mutex);
        retired.swap(tables().registries);
    }
    // Provider destructors may reenter the capability API; no registry lock is held.
}
}  // namespace eve::cap::detail
