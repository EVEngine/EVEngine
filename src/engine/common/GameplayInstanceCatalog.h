#pragma once

/**
 * @file GameplayInstanceCatalog.h
 * @brief Optional companion surface for gameplay providers that can enumerate instances.
 */

#include "common/SubjectRef.h"

#include <string_view>
#include <vector>

namespace eve {

/**
 * @brief Optional read-only discovery surface implemented beside a gameplay provider.
 *
 * `IGameplayControlProvider` addresses one instance per call, and the shared router
 * allows exactly one provider per domain, so nothing in that interface tells an
 * unattended agent *which* instances exist. A provider that can enumerate its
 * published instances implements this interface as well and registers it with
 * `eve::cap::addListener`; the `instances` gameplay-control operation reports every
 * catalogue and, separately, the domains that cannot enumerate.
 *
 * Registration and every method are owner-simulation-thread affine, like the
 * provider itself. Implementations return owning vectors and retain no argument.
 *
 * @ownership The implementation is owned by whichever module publishes the domain;
 *            callers only read it during the call.
 * @thread Owner-simulation thread only.
 */
class IGameplayInstanceCatalog {
public:
    static constexpr const char* capabilityName = "IGameplayInstanceCatalog";
    virtual ~IGameplayInstanceCatalog()         = default;

    /** @brief Provider domain these instances belong to; matches the provider's domain. */
    [[nodiscard]] virtual std::string_view gameplayDomain() const noexcept = 0;
    /** @brief Instances currently served, in publication order. */
    [[nodiscard]] virtual std::vector<SubjectRef> gameplayInstances() const = 0;
};

}  // namespace eve
