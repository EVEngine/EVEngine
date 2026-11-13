// eve-hot-path
// Immutable content context only: no entity Views, structural writes, events, service discovery, clocks or RNG.
// Explicit one-owner-per-operation injection; nested scopes restore a borrowed outer context in stack order.
#include "rpg/SkillContent.h"
namespace eve::rpg {
namespace {
thread_local OptionalRef<const SkillContent> scopedContent;
}
SkillContentScope::SkillContentScope(std::shared_ptr<const SkillContent> content)
    : owner_(std::move(content)), previous_(scopedContent) {
    scopedContent = owner_ ? OptionalRef<const SkillContent>(std::cref(*owner_)) : std::nullopt;
}
SkillContentScope::~SkillContentScope() { scopedContent = previous_; }
OptionalRef<const SkillContent> SkillContentScope::current() noexcept { return scopedContent; }
}  // namespace eve::rpg
