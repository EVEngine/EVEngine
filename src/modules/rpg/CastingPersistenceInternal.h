#pragma once
#include "common/Result.h"
#include "common/Value.h"
#include "rpg/RPGActor.h"
namespace eve::rpg {
struct PreparedCastingState {
    CastingState              state;
    std::optional<SubjectRef> target;
};
[[nodiscard]] Result<Value> encodeCastingState(const RPGActor& actor, std::optional<CastTargetBinding> binding);
[[nodiscard]] Result<PreparedCastingState> prepareCastingState(const Value& value, const RPGActor::Skills& skills);
[[nodiscard]] Result<void> bindCastingState(CastingState& state, std::optional<SubjectRef> subject, RPGActor& actor,
                                            std::optional<CastTargetBinding> binding);
}  // namespace eve::rpg
