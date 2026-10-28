#pragma once
#include <memory>
#include "common/Module.h"

namespace eve::agent {
/** @brief Manager-owned optional tensor backend. Owner thread only; revokes before destruction. */
class AgentTensor : public Module {
public:
    Module_REG(AgentTensor);
    /** @brief Register the tensor inference provider; instantiate once on the composition thread. */
    AgentTensor();
    /** @brief Revoke the provider, then destroy its owning implementation. */
    ~AgentTensor() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::agent
