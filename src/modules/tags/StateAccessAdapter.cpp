#include "tags/StateAccessAdapter.h"

#include <string>
#include <utility>

namespace eve::tags {
namespace {

eve::Result<eve::MutationReceipt> failure(eve::DiagnosticCode code, std::string message,
                                          std::string path = {}) {
    return eve::Result<eve::MutationReceipt>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

bool isTagKey(std::string_view key) {
    return key.starts_with("tag.") || key.starts_with("tag:");
}

std::string tagName(std::string_view key) {
    if (key.starts_with("tag.") || key.starts_with("tag:")) key.remove_prefix(4);
    return std::string(key);
}

}  // namespace

std::optional<eve::Value> TagStoreStateAdapter::value(std::string_view subject,
                                                       std::string_view key) const {
    if (subject.empty() || key.empty() || !isTagKey(key)) return std::nullopt;
    return eve::Value(store_.hasTag(std::string(subject), tagName(key)));
}

std::optional<bool> TagStoreStateAdapter::hasTag(std::string_view subject,
                                                  std::string_view tag) const {
    if (subject.empty() || tag.empty()) return std::nullopt;
    return store_.hasTag(std::string(subject), std::string(tag));
}

std::optional<eve::Value> TagStoreStateAdapter::state(std::string_view subject,
                                                       std::string_view key) const {
    return value(subject, key);
}

eve::Result<eve::MutationReceipt> TagStoreStateAdapter::apply(
    std::span<const eve::StateMutation> mutations, const eve::MutationContext& context) {
    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto& mutation = mutations[index];
        if (mutation.persistent)
            return failure(eve::DiagnosticCode::Unsupported,
                           "TagStore mutations are volatile; use StatePatch for persistence",
                           "mutations[" + std::to_string(index) + "]");
        if (mutation.subject.empty() || mutation.key.empty())
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "tag mutation requires subject and tag", "mutations[" + std::to_string(index) + "]");
        if ((mutation.kind == eve::MutationKind::Set) &&
            (!isTagKey(mutation.key) || !mutation.value.isBool()))
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "tag Set requires a tag.* key and boolean value",
                           "mutations[" + std::to_string(index) + "]");
        if (mutation.kind == eve::MutationKind::Set || mutation.kind == eve::MutationKind::AddTag ||
            mutation.kind == eve::MutationKind::RemoveTag)
            continue;
        return failure(eve::DiagnosticCode::Unsupported,
                       "TagStore supports Set, AddTag, and RemoveTag only",
                       "mutations[" + std::to_string(index) + "]");
    }

    TagStore candidate = store_;
    std::size_t changed = 0;
    for (const auto& mutation : mutations) {
        const std::string tag = tagName(mutation.key);
        bool didChange = false;
        switch (mutation.kind) {
        case eve::MutationKind::Set:
            didChange = mutation.value.asBool() ? candidate.addTag(mutation.subject, tag)
                                                : candidate.removeTag(mutation.subject, tag);
            break;
        case eve::MutationKind::AddTag:
            didChange = candidate.addTag(mutation.subject, tag);
            break;
        case eve::MutationKind::RemoveTag:
            didChange = candidate.removeTag(mutation.subject, tag);
            break;
        case eve::MutationKind::Remove:
        case eve::MutationKind::AddNumber:
            break;
        }
        if (didChange) ++changed;
    }
    store_ = std::move(candidate);
    return eve::Result<eve::MutationReceipt>::success(
        eve::MutationReceipt{context.transactionId, changed},
        eve::Status::success(changed == 0 ? eve::StatusCode::NoOp : eve::StatusCode::Applied));
}

}  // namespace eve::tags
