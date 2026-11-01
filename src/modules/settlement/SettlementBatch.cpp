#include "settlement/Settlement.h"

#include <algorithm>
#include <exception>
#include <memory>
#include <utility>

namespace eve::settlement {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

template <class T>
eve::Result<T> failure(eve::Status status) {
    return eve::Result<T>::failure(std::move(status));
}

}  // namespace

eve::Result<std::vector<SettlementResult>> SettlementPipeline::settleAtomic(std::span<const SettlementRequest> requests,
                                                                            std::span<ISettlementPolicy*>      policies,
                                                                            game_event::GameEventLog* events) const {
    if (requests.empty())
        return failure<std::vector<SettlementResult>>(eve::DiagnosticCode::InvalidArgument,
                                                      "atomic settlement requires at least one request", "requests");
    if (requests.size() != policies.size())
        return failure<std::vector<SettlementResult>>(
            eve::DiagnosticCode::InvalidArgument, "atomic settlement request and policy counts must match", "policies");

    for (std::size_t index = 0; index < policies.size(); ++index) {
        if (policies[index] == nullptr)
            return failure<std::vector<SettlementResult>>(eve::DiagnosticCode::InvalidArgument,
                                                          "atomic settlement policy must not be null",
                                                          "policies[" + std::to_string(index) + "]");
        for (std::size_t previous = 0; previous < index; ++previous) {
            if (requests[previous].target == requests[index].target &&
                (requests[previous].resource.empty() || requests[index].resource.empty() ||
                 requests[previous].resource == requests[index].resource) &&
                policies[previous] != policies[index])
                return failure<std::vector<SettlementResult>>(
                    eve::DiagnosticCode::Conflict,
                    "channels for the same target and resource must share one policy instance",
                    "policies[" + std::to_string(index) + "]");
        }
    }

    std::vector<SettlementResult>                   results(requests.size());
    std::vector<std::unique_ptr<SettlementContext>> contexts;
    contexts.reserve(requests.size());
    const auto rollbackAll = [&]() noexcept {
        for (auto it = contexts.rbegin(); it != contexts.rend(); ++it)
            if (auto* pending = (*it)->pendingApply()) pending->rollback();
    };

    for (std::size_t index = 0; index < requests.size(); ++index) {
        results[index].requested = requests[index].magnitude;
        results[index].tick      = requests[index].tick;
        contexts.push_back(std::unique_ptr<SettlementContext>(
            new SettlementContext(requests[index], results[index], *policies[index])));
        auto prepared = prepare(*contexts.back(), results[index]);
        if (!prepared) {
            const auto status = prepared.status();
            rollbackAll();
            return failure<std::vector<SettlementResult>>(status);
        }
        auto committed = contexts.back()->pendingApply()->commit();
        if (!committed) {
            const auto status = committed.status();
            rollbackAll();
            return failure<std::vector<SettlementResult>>(status);
        }
    }

    const std::string eventSnapshot = events == nullptr ? std::string{} : events->snapshotJson();
    for (std::size_t index = 0; index < contexts.size(); ++index) {
        const auto* preparedEvent = contexts[index]->pendingEvent();
        if (preparedEvent == nullptr) continue;
        game_event::GameEvent envelope = *preparedEvent;
        results[index].event           = envelope;
        if (events == nullptr) continue;
        try {
            auto appended = events->append(envelope);
            if (!appended) {
                const auto status   = appended.status();
                auto       restored = events->restore(eventSnapshot);
                if (!restored) {
                    const auto restoreStatus = restored.status();
                    rollbackAll();
                    return failure<std::vector<SettlementResult>>(restoreStatus);
                }
                rollbackAll();
                return failure<std::vector<SettlementResult>>(status);
            }
            const auto sequence = std::move(appended).takeValue();
            envelope.sequence   = sequence;
            if (const auto* stored = events->find(sequence)) {
                results[index].event = *stored;
            } else {
                results[index].event = envelope;
            }
        } catch (const std::exception& exception) {
            auto restored = events->restore(eventSnapshot);
            if (!restored) {
                const auto status = restored.status();
                rollbackAll();
                return failure<std::vector<SettlementResult>>(status);
            }
            rollbackAll();
            return failure<std::vector<SettlementResult>>(
                eve::DiagnosticCode::Failed, std::string("atomic settlement event append threw: ") + exception.what(),
                "events");
        } catch (...) {
            auto restored = events->restore(eventSnapshot);
            if (!restored) {
                const auto status = restored.status();
                rollbackAll();
                return failure<std::vector<SettlementResult>>(status);
            }
            rollbackAll();
            return failure<std::vector<SettlementResult>>(
                eve::DiagnosticCode::Failed, "atomic settlement event append threw an unknown exception", "events");
        }
    }

    const bool anyApplied =
        std::any_of(results.begin(), results.end(), [](const auto& result) { return result.applied != 0.0; });
    return eve::Result<std::vector<SettlementResult>>::success(
        std::move(results), eve::Status::success(anyApplied ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<std::vector<SettlementBatchItemResult>> SettlementPipeline::settleIndependent(
    std::span<const SettlementRequest> requests, std::span<ISettlementPolicy*> policies,
    game_event::GameEventLog* events) const {
    if (requests.empty())
        return failure<std::vector<SettlementBatchItemResult>>(
            eve::DiagnosticCode::InvalidArgument, "independent settlement requires at least one request", "requests");
    if (requests.size() != policies.size())
        return failure<std::vector<SettlementBatchItemResult>>(
            eve::DiagnosticCode::InvalidArgument, "independent settlement request and policy counts must match",
            "policies");
    for (std::size_t index = 0; index < policies.size(); ++index)
        if (policies[index] == nullptr)
            return failure<std::vector<SettlementBatchItemResult>>(eve::DiagnosticCode::InvalidArgument,
                                                                   "independent settlement policy must not be null",
                                                                   "policies[" + std::to_string(index) + "]");

    bool                                   anyApplied = false;
    std::vector<SettlementBatchItemResult> outcomes;
    outcomes.reserve(requests.size());
    for (std::size_t index = 0; index < requests.size(); ++index) {
        auto                      settled = settle(requests[index], *policies[index], events);
        SettlementBatchItemResult outcome;
        outcome.index  = index;
        outcome.request = requests[index];
        outcome.status = settled.status();
        if (settled) {
            outcome.result = std::move(settled).takeValue();
            anyApplied     = anyApplied || outcome.result->applied != 0.0;
        }
        outcomes.push_back(std::move(outcome));
    }
    return eve::Result<std::vector<SettlementBatchItemResult>>::success(
        std::move(outcomes), eve::Status::success(anyApplied ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

eve::Result<std::vector<SettlementBatchItemResult>> SettlementPipeline::settleChain(
    const SettlementRequest& root, const RequestExecutor& execute, std::uint32_t maxDepth,
    std::uint32_t maxSettlements) const {
    if (!execute)
        return failure<std::vector<SettlementBatchItemResult>>(
            eve::DiagnosticCode::InvalidArgument, "settlement chain request executor must not be empty", "execute");
    if (maxSettlements == 0)
        return failure<std::vector<SettlementBatchItemResult>>(
            eve::DiagnosticCode::InvalidArgument, "settlement chain limit must include at least the root request",
            "maxSettlements");
    if (root.chain.depth != 0 || root.chain.emittedCount != 0 || !root.chain.triggerPath.empty() ||
        !root.trigger.empty())
        return failure<std::vector<SettlementBatchItemResult>>(
            eve::DiagnosticCode::InvalidArgument, "settlement chain root contains derived-chain metadata", "root.chain");

    std::vector<SettlementRequest>         pending{root};
    std::vector<SettlementBatchItemResult> outcomes;
    std::uint32_t emittedCount = 0;
    bool          anyApplied   = false;

    const auto appendFailure = [&](eve::Status status, SettlementRequest request = {}) {
        SettlementBatchItemResult outcome;
        outcome.index  = outcomes.size();
        outcome.request = std::move(request);
        outcome.status = std::move(status);
        outcomes.push_back(std::move(outcome));
    };

    for (std::size_t cursor = 0; cursor < pending.size(); ++cursor) {
        auto settled = execute(pending[cursor]);
        SettlementBatchItemResult outcome;
        outcome.index  = outcomes.size();
        outcome.request = pending[cursor];
        outcome.status = settled.status();
        if (!settled) {
            outcomes.push_back(std::move(outcome));
            break;
        }

        outcome.result = std::move(settled).takeValue();
        anyApplied     = anyApplied || outcome.result->applied != 0.0;
        auto derived   = outcome.result->derived;
        outcomes.push_back(std::move(outcome));

        bool stop = false;
        for (auto& child : derived) {
            if (pending.size() >= maxSettlements) {
                appendFailure(eve::Status::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "settlement chain exceeded its total settlement limit",
                    "maxSettlements")), child);
                stop = true;
                break;
            }
            if (child.trigger.empty()) {
                appendFailure(eve::Status::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "derived settlement request requires a trigger key",
                    "derived.trigger")), child);
                stop = true;
                break;
            }
            if (pending[cursor].chain.depth >= maxDepth) {
                appendFailure(eve::Status::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "settlement chain exceeded its depth limit", "maxDepth")), child);
                stop = true;
                break;
            }
            if (std::find(pending[cursor].chain.triggerPath.begin(), pending[cursor].chain.triggerPath.end(),
                          child.trigger) != pending[cursor].chain.triggerPath.end()) {
                appendFailure(eve::Status::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Conflict, "settlement trigger re-entered its ancestry path",
                    "derived.trigger")), child);
                stop = true;
                break;
            }
            if (child.chain.depth != 0 || child.chain.emittedCount != 0 || !child.chain.triggerPath.empty()) {
                appendFailure(eve::Status::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "policy-produced request must not pre-populate chain metadata",
                    "derived.chain")), child);
                stop = true;
                break;
            }

            ++emittedCount;
            child.chain              = pending[cursor].chain;
            child.chain.depth        = pending[cursor].chain.depth + 1;
            child.chain.emittedCount = emittedCount;
            child.chain.triggerPath.push_back(child.trigger);
            if (child.correlation.kind() == game_event::CorrelationId::Kind::None)
                child.correlation = pending[cursor].correlation;
            if (child.causation.kind() == game_event::CausationRef::Kind::None && outcomes.back().result->event &&
                !outcomes.back().result->event->eventId.isNil())
                child.causation = game_event::CausationRef::fromEventId(outcomes.back().result->event->eventId);
            pending.push_back(std::move(child));
        }
        if (stop) break;
    }

    return eve::Result<std::vector<SettlementBatchItemResult>>::success(
        std::move(outcomes), eve::Status::success(anyApplied ? eve::StatusCode::Applied : eve::StatusCode::NoOp));
}

}  // namespace eve::settlement
