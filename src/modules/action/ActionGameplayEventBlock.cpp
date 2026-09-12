#include "action/ActionGameplayEventBlock.h"

#include "tags/GameplayTag.h"

#include <limits>
#include <utility>

namespace eve::action {
namespace {

template <typename T>
Result<T> invalid(std::string message, std::string path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), std::move(path)));
}

}  // namespace

Result<ActionGameplayEventBinding> ActionGameplayEventBinding::fromPayload(const Value::Object& payload) {
    ActionGameplayEventBinding candidate;
    const auto foundTag = payload.find("tag");
    const auto* tag = foundTag == payload.end() ? nullptr : foundTag->second.getIf<std::string>();
    if (!tag || !tags::isValidGameplayTagName(*tag))
        return invalid<ActionGameplayEventBinding>("gameplay event tag must be canonical", "tag");
    candidate.tag = *tag;

    if (const auto foundTarget = payload.find("targetIndex"); foundTarget != payload.end()) {
        const auto* target = foundTarget->second.getIf<std::int64_t>();
        if (!target || *target < 0 || static_cast<std::uint64_t>(*target) > std::numeric_limits<std::uint32_t>::max())
            return invalid<ActionGameplayEventBinding>(
                "gameplay event target index must be an unsigned 32-bit integer", "targetIndex");
        candidate.targetIndex = static_cast<std::size_t>(*target);
    }
    if (const auto foundData = payload.find("data"); foundData != payload.end()) candidate.data = foundData->second;
    return Result<ActionGameplayEventBinding>::success(std::move(candidate));
}

}  // namespace eve::action
