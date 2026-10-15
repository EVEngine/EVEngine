#include "action/ActionNotifyPlayback.h"

namespace eve::action {

Result<std::unique_ptr<ActionNotifyPlayback>> ActionNotifyPlayback::withBuiltins() {
    auto registry = ActionNotifyRegistry::withBuiltins();
    if (!registry) return Result<std::unique_ptr<ActionNotifyPlayback>>::failure(registry.status());
    return Result<std::unique_ptr<ActionNotifyPlayback>>::success(
        std::make_unique<ActionNotifyPlayback>(std::move(registry).takeValue()));
}

ActionNotifyPlayback::ActionNotifyPlayback(ActionNotifyRegistry registry) : registry_(std::move(registry)) {}

Result<void> ActionNotifyPlayback::apply(const ActionAdvance& advance, ActionNotifyContext context) {
    return blocks_.apply(advance, std::move(context));
}

Result<void> ActionNotifyPlayback::interrupt(ActionNotifyContext context) {
    return blocks_.interrupt(std::move(context));
}

Result<void> ActionNotifyPlayback::sample(const std::vector<ActionActiveBlock>& blocks,
                                          ActionNotifyContext                  context) const {
    return blocks_.sample(blocks, std::move(context));
}

}  // namespace eve::action
