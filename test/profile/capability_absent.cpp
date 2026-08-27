#include "common/Capability.h"

namespace {
struct MissingCapability {
    static constexpr const char* capabilityName = "profile.optional.absent";
    virtual ~MissingCapability() = default;
};
}  // namespace

int main() {
    eve::cap::detail::clearAllRaw();
    // An absent optional provider is a supported state, not a link/runtime
    // error.  The consumer must observe nullptr and continue explicitly.
    return eve::cap::query<MissingCapability>() == nullptr ? 0 : 1;
}
