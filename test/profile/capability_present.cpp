#include "common/Capability.h"

namespace {
struct ProbeCapability {
    static constexpr const char* capabilityName = "profile.optional.present";
    virtual ~ProbeCapability() = default;
    virtual int value() const = 0;
};

struct Provider final : ProbeCapability {
    int value() const override { return 42; }
};
}  // namespace

int main() {
    eve::cap::detail::clearAllRaw();
    if (eve::cap::query<ProbeCapability>() != nullptr) return 1;

    Provider provider;
    eve::cap::provide<ProbeCapability>(&provider);
    auto* found = eve::cap::query<ProbeCapability>();
    if (found == nullptr || found->value() != 42) return 2;

    eve::cap::revoke<ProbeCapability>(&provider);
    return eve::cap::query<ProbeCapability>() == nullptr ? 0 : 3;
}
