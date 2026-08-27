#include "statepatch/StateAccessAdapter.h"

#include "transaction/Transaction.h"

#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace eve::statepatch {
namespace {

eve::Result<eve::MutationReceipt> failure(eve::DiagnosticCode code, std::string message,
                                          std::string path = {}) {
    return eve::Result<eve::MutationReceipt>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<eve::Value> readJsonValue(const Store& store, std::string_view subject,
                                      std::string_view key) {
    if (!store.has(std::string(subject), std::string(key)))
        return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "state value is absent", "state"));
    auto parsed = eve::Value::fromJson(store.get(std::string(subject), std::string(key)));
    if (!parsed) return eve::Result<eve::Value>::failure(parsed.status());
    return std::move(parsed);
}

bool numeric(const eve::Value& value, double& output) {
    if (value.isInt64()) {
        output = static_cast<double>(value.asInt());
        return true;
    }
    if (value.isDouble()) {
        output = value.asDouble();
        return std::isfinite(output);
    }
    return false;
}

std::string tagKey(std::string_view tag) { return "tag." + std::string(tag); }

}  // namespace

std::optional<eve::Value> StatePatchStateAdapter::value(std::string_view subject,
                                                         std::string_view key) const {
    auto result = readJsonValue(store_, subject, key);
    if (!result) return std::nullopt;
    return std::move(result).takeValue();
}

std::optional<bool> StatePatchStateAdapter::hasTag(std::string_view subject,
                                                    std::string_view tag) const {
    if (subject.empty() || tag.empty()) return std::nullopt;
    const std::string key = tagKey(tag);
    if (!store_.has(std::string(subject), key)) return false;
    auto value = readJsonValue(store_, subject, key);
    if (!value) return std::nullopt;
    const eve::Value& parsed = value.value();
    if (!parsed.isBool()) return std::nullopt;
    return parsed.asBool();
}

std::optional<eve::Value> StatePatchStateAdapter::state(std::string_view subject,
                                                         std::string_view key) const {
    return value(subject, key);
}

eve::Result<eve::MutationReceipt> StatePatchStateAdapter::apply(
    std::span<const eve::StateMutation> mutations, const eve::MutationContext& context) {
    if (context.transactionId.empty())
        return failure(eve::DiagnosticCode::InvalidArgument,
                       "persistent dialogue mutations require a transaction id", "transactionId");

    using Key = std::pair<std::string, std::string>;
    std::map<Key, std::optional<eve::Value>> pending;
    auto batchReference = store_.newBatch();
    if (!batchReference)
        return eve::Result<eve::MutationReceipt>::failure(batchReference.status());
    const PatchBatchHandleRef batchHandle = std::move(batchReference).takeValue();
    auto batch = store_.resolveBatch(batchHandle);
    if (!batch.isBound())
        return failure(eve::DiagnosticCode::StaleHandle,
                       "StatePatch patch batch became stale during allocation", "batch");
    struct BatchLease {
        Store& store;
        PatchBatchHandleRef handle;
        ~BatchLease() { store.releaseBatch(handle).ignore("release StatePatch adapter batch"); }
    } batchLease{store_, batchHandle};

    for (std::size_t index = 0; index < mutations.size(); ++index) {
        const auto& mutation = mutations[index];
        const std::string path = "mutations[" + std::to_string(index) + "]";
        if (!mutation.persistent)
            return failure(eve::DiagnosticCode::Unsupported,
                           "StatePatch adapter accepts persistent mutations only", path);
        if (mutation.subject.empty() || mutation.key.empty())
            return failure(eve::DiagnosticCode::InvalidArgument,
                           "persistent mutation requires subject and key", path);

        const std::string sourceKey = mutation.key;
        const std::string key = mutation.kind == eve::MutationKind::AddTag ||
                                        mutation.kind == eve::MutationKind::RemoveTag
                                    ? tagKey(sourceKey)
                                    : sourceKey;
        const Key pendingKey{mutation.subject, key};
        auto current = [&]() -> std::optional<eve::Value> {
            const auto found = pending.find(pendingKey);
            if (found != pending.end()) return found->second;
            if (!store_.has(mutation.subject, key)) return std::nullopt;
            auto existing = readJsonValue(store_, mutation.subject, key);
            if (!existing) return std::nullopt;
            return std::move(existing).takeValue();
        };

        bool appended = false;
        switch (mutation.kind) {
        case eve::MutationKind::Set: {
            auto json = mutation.value.toJson();
            if (!json) return failure(json.error() ? json.error()->code() : eve::DiagnosticCode::SerializationError,
                                      json.error() ? json.error()->message() : "state value cannot be serialized",
                                      path + ".value");
            appended = batch->set(mutation.subject, key, json.value());
            if (appended) pending[pendingKey] = mutation.value;
            break;
        }
        case eve::MutationKind::Remove:
            appended = batch->remove(mutation.subject, key);
            if (appended) pending[pendingKey] = std::nullopt;
            break;
        case eve::MutationKind::AddTag:
            appended = batch->set(mutation.subject, key, "true");
            if (appended) pending[pendingKey] = eve::Value(true);
            break;
        case eve::MutationKind::RemoveTag:
            appended = batch->remove(mutation.subject, key);
            if (appended) pending[pendingKey] = std::nullopt;
            break;
        case eve::MutationKind::AddNumber: {
            double delta = 0.0;
            if (!numeric(mutation.value, delta))
                return failure(eve::DiagnosticCode::InvalidArgument,
                               "AddNumber requires a finite numeric value", path + ".value");
            const auto existing = current();
            double base = 0.0;
            if (existing && !numeric(*existing, base))
                return failure(eve::DiagnosticCode::InvalidArgument,
                               "AddNumber cannot add to a non-numeric state value", path);
            const double total = base + delta;
            if (!std::isfinite(total))
                return failure(eve::DiagnosticCode::InvalidArgument,
                               "AddNumber result must be finite", path);
            const eve::Value result(total);
            auto json = result.toJson();
            if (!json) return failure(eve::DiagnosticCode::SerializationError,
                                      "AddNumber result cannot be serialized", path);
            appended = batch->set(mutation.subject, key, json.value());
            if (appended) pending[pendingKey] = result;
            break;
        }
        }
        if (!appended)
            return failure(eve::DiagnosticCode::Failed, "StatePatch rejected a mutation", path);
    }

    eve::transaction::TransactionContext transaction(context.transactionId, context.correlationId,
                                                     context.causationId);
    StoreTransactionParticipant participant(store_, *batch);
    std::array<eve::transaction::ITransactionParticipant*, 1> participants{&participant};
    eve::transaction::Coordinator coordinator;
    auto committed = coordinator.execute(transaction, participants);
    if (!committed) return eve::Result<eve::MutationReceipt>::failure(committed.status());

    const PatchResult& result = batch->result();
    return eve::Result<eve::MutationReceipt>::success(
        eve::MutationReceipt{context.transactionId, static_cast<std::size_t>(result.changedCount)},
        eve::Status::success(result.changedCount == 0 ? eve::StatusCode::NoOp
                                                       : eve::StatusCode::Applied));
}

}  // namespace eve::statepatch
