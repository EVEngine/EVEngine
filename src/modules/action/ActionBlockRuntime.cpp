#include "action/ActionBlockRuntime.h"

#include <utility>

namespace eve::action {
namespace {

Result<void> invalidContext() {
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   "Action block context does not match advance", "executionId"));
}

}  // namespace

Result<void> ActionBlockRuntime::apply(const ActionAdvance& advance, ActionNotifyContext context) {
    if (context.executionId != advance.id) return invalidContext();
    context.time = advance.totalElapsed;
    for (const auto& event : advance.timelineEvents) {
        auto dispatched = registry_.dispatch(event, context);
        if (!dispatched) return dispatched;
    }
    for (const auto& block : advance.activeBlocks) {
        auto updated = registry_.dispatchUpdate(block, context);
        if (!updated) return updated;
    }
    if (advance.activeBlocks.empty())
        active_.erase(advance.id);
    else
        active_[advance.id] = advance.activeBlocks;
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionBlockRuntime::interrupt(ActionNotifyContext context) {
    const auto found = active_.find(context.executionId);
    if (found == active_.end()) return Result<void>::success(Status::success(StatusCode::NoOp));
    auto& blocks = found->second;
    while (!blocks.empty()) {
        const auto& block = blocks.front();
        ActionTimelineEvent event{ActionTimelineEventKind::StateExit, block.trackId, block.itemId, block.type,
                                  context.time, block.payload};
        auto dispatched = registry_.dispatch(event, context);
        if (!dispatched) return dispatched;
        blocks.erase(blocks.begin());
    }
    active_.erase(found);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

Result<void> ActionBlockRuntime::sample(const std::vector<ActionActiveBlock>& blocks, ActionNotifyContext context) const {
    context.preview   = true;
    context.scrubbing = true;
    for (const auto& block : blocks) {
        auto sampled = registry_.dispatchSample(block, context);
        if (!sampled) return sampled;
    }
    return Result<void>::success(Status::success(blocks.empty() ? StatusCode::NoOp : StatusCode::Applied));
}

}  // namespace eve::action
