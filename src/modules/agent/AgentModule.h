#pragma once
#include "common/Module.h"

namespace eve::agent {
/**
 * @brief Squirrel entry eve.Agent(): owner-thread synchronous training, inference and replay.
 * @remarks ModuleManager owns this service. Environment callbacks are rooted only during calls;
 * run/replay reentry is rejected. Returned tables own data and survive module destruction.
 */
class Agent : public Module {
public:
    SSQ_REG;
    /** @brief Static module name, independent of registration lifetime. */
    std::string getName() const override { return "Agent"; }
    /**
     * @brief Borrow the registered singleton.
     * @ownership Borrowed; ModuleManager owns the returned module.
     * @nullable No after successful creation.
     * @lifetime Until module shutdown or registry teardown.
     * @thread Composition thread only.
     * @reentrancy Must not reenter registration or destroy the module during a call.
     */
    [[nodiscard]] static Agent* create();
    static const char*          name;

private:
    bool active_ = false;
};
}  // namespace eve::agent
