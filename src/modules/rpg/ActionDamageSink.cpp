#include "rpg/ActionDamageSink.h"

#include "common/Capability.h"
#include "rpg/RPGActor.h"
#include "rpg/SettlementAdapter.h"
#include "rpg/VitalsSystem.h"
#include "settlement/Settlement.h"

#include <utility>

namespace eve::rpg {
namespace {
RPGActor*    resolve(ecs::EntityHandle handle) { return dynamic_cast<RPGActor*>(ecs::try_get(handle)); }
Result<void> failure(DiagnosticCode code, const char* message) {
    return Result<void>::failure(Diagnostic::error(code, message));
}
}  // namespace

struct RPGActionDamageSink::Impl {
    RPGDamageResolver                   resolver;
    settlement::SettlementPipeline      pipeline;
    std::vector<RPGActionDamageReceipt> receipts;
};

RPGActionDamageSink::RPGActionDamageSink(RPGDamageResolver resolver) : impl_(std::make_unique<Impl>()) {
    impl_->resolver = std::move(resolver);
    cap::addListener<action::IActionDamageSink>(this);
}
RPGActionDamageSink::~RPGActionDamageSink() { cap::removeListener<action::IActionDamageSink>(this); }

bool RPGActionDamageSink::supports(ecs::EntityHandle target) const {
    if (!impl_->resolver || !ecs::try_get(target)) return false;
    const auto mapped = impl_->resolver(target);
    return mapped && mapped->subject.isValid() && resolve(mapped->actor);
}

Result<void> RPGActionDamageSink::apply(const action::ActionDamageBinding& binding,
                                        const action::ActionNotifyContext& context) {
    if (context.preview || context.scrubbing) return Result<void>::success(Status::success(StatusCode::NoOp));
    if (binding.poiseAmount != 0.0 || binding.knockback.x != 0.0 || binding.knockback.y != 0.0 ||
        binding.knockback.z != 0.0)
        return failure(DiagnosticCode::Unsupported, "RPG Vitals damage does not own poise or knockback");
    if (binding.targetIndex >= context.targets.size() || !impl_->resolver)
        return failure(DiagnosticCode::NotFound, "RPG damage target or resolver is unavailable");
    const auto targetHandle = context.targets[binding.targetIndex];
    if (!ecs::try_get(targetHandle)) return failure(DiagnosticCode::StaleHandle, "Action target was destroyed");
    const auto mappedTarget = impl_->resolver(targetHandle);
    if (!mappedTarget || !mappedTarget->subject.isValid())
        return failure(DiagnosticCode::NotFound, "Action target has no RPG identity mapping");
    std::optional<RPGDamageTarget> mappedSource;
    if (context.source) {
        if (!ecs::try_get(*context.source)) return failure(DiagnosticCode::StaleHandle, "Action source was destroyed");
        mappedSource = impl_->resolver(*context.source);
        if (!mappedSource || !mappedSource->subject.isValid())
            return failure(DiagnosticCode::NotFound, "Action source has no RPG identity mapping");
    }
    // Resolve only after all host callbacks have returned; retain no borrowed ECS addresses.
    auto* target = resolve(mappedTarget->actor);
    auto* source = mappedSource ? resolve(mappedSource->actor) : nullptr;
    if (!target || (mappedSource && !source))
        return failure(DiagnosticCode::StaleHandle, "Mapped RPG actor was destroyed");

    settlement::SettlementRequest request;
    request.source    = mappedSource ? mappedSource->subject : SubjectRef::nil();
    request.target    = mappedTarget->subject;
    request.kind      = "damage";
    request.resource  = "hp";
    request.magnitude = binding.amount;
    request.tags      = {binding.damageType};
    request.tick      = context.tick;
    RPGSettlementAdapter adapter(*target, request.target, source, request.source);
    const double         before = VitalsSystem::getCurrent(target, "hp");
    impl_->receipts.reserve(impl_->receipts.size() + 1);
    auto settled = impl_->pipeline.settle(request, adapter);
    if (!settled) return Result<void>::failure(settled.status());
    const double applied = settled.value().applied;
    impl_->receipts.push_back({request.source, request.target, context.executionId, context.tick, applied,
                               before > 0.0 && VitalsSystem::getCurrent(target, "hp") == 0.0});
    return Result<void>::success(Status::success(StatusCode::Applied));
}

std::vector<RPGActionDamageReceipt> RPGActionDamageSink::takeReceipts() {
    std::vector<RPGActionDamageReceipt> result;
    result.swap(impl_->receipts);
    return result;
}
}  // namespace eve::rpg
