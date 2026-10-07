#include "gpuagents/EffectBackend.h"

#include "common/Diagnostic.h"

namespace eve::gpuagents {

Result<void> EffectBackend::configure(const EffectProfile& profile, std::string displayName) {
    name_ = std::move(displayName);
    auto cfg = simulation_.configure(profile);
    if (!cfg.ok()) return cfg;
    renderer_.sync(simulation_.states());
    return Result<void>::success();
}

Result<void> EffectBackend::step(float dt, const EnvironmentSnapshot& env) {
    auto stepped = simulation_.step(dt, env);
    if (!stepped.ok()) return stepped;
    renderer_.sync(simulation_.states());
    return Result<void>::success();
}

}  // namespace eve::gpuagents
