#include "rts/RTSProductionAction.h"
#include "rts/RTSTypes.h"

#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace eve::rts {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

eve::Result<void> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

class OrderParticipant final : public transaction::ITransactionParticipant {
public:
    OrderParticipant(orders::CommandQueue& queue, std::string kind, int priority, double timeout, std::string product,
                     std::string owner)
        : queue_(queue),
          kind_(std::move(kind)),
          priority_(priority),
          timeout_(timeout),
          product_(std::move(product)),
          owner_(std::move(owner)) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rts-build-order"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (prepared_ || committed_)
            return failure(eve::DiagnosticCode::Conflict, "RTS order participant is already in flight", "orders");
        try {
            before_ = queue_;
            staged_ = queue_;
        } catch (const std::exception& exception) {
            return failure(eve::DiagnosticCode::Failed, std::string("failed to stage RTS order: ") + exception.what(),
                           "orders");
        }
        auto id = staged_->append(kind_, priority_, timeout_);
        if (!id) return eve::Result<void>::failure(id.status());
        orderId_   = std::move(id).takeValue();
        auto order = staged_->find(orderId_);
        if (!order) return failure(eve::DiagnosticCode::InvariantViolation, "staged RTS order disappeared", "orders");
        order->get().payload.setString("owner", owner_);
        order->get().payload.setString("product", product_);
        prepared_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure(eve::DiagnosticCode::Conflict, "RTS order participant has no prepared stage", "orders");
        try {
            queue_ = *staged_;
        } catch (const std::exception& exception) {
            return failure(eve::DiagnosticCode::Failed, std::string("failed to publish RTS order: ") + exception.what(),
                           "orders");
        }
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (committed_)
            return failure(eve::DiagnosticCode::Conflict, "committed RTS order requires compensation", "orders");
        staged_.reset();
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_)
            return failure(eve::DiagnosticCode::Conflict, "RTS order has no committed state to compensate", "orders");
        try {
            queue_ = *before_;
        } catch (const std::exception& exception) {
            return failure(eve::DiagnosticCode::Failed,
                           std::string("failed to compensate RTS order: ") + exception.what(), "orders");
        }
        staged_.reset();
        before_.reset();
        prepared_  = false;
        committed_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] const std::string& orderId() const noexcept { return orderId_; }

private:
    orders::CommandQueue&               queue_;
    std::string                         kind_;
    int                                 priority_ = 0;
    double                              timeout_  = 0.0;
    std::string                         product_;
    std::string                         owner_;
    std::optional<orders::CommandQueue> before_;
    std::optional<orders::CommandQueue> staged_;
    std::string                         orderId_;
    bool                                prepared_  = false;
    bool                                committed_ = false;
};

class ProductionParticipant final : public transaction::ITransactionParticipant {
public:
    ProductionParticipant(production::WorkQueue& queue, std::string owner, std::string kind, std::string product,
                          eve::Value context, Duration duration, int priority)
        : queue_(queue),
          owner_(std::move(owner)),
          kind_(std::move(kind)),
          product_(std::move(product)),
          context_(std::move(context)),
          duration_(duration),
          priority_(priority) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rts-production-queue"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (prepared_ || committed_)
            return failure(eve::DiagnosticCode::Conflict, "RTS production participant is already in flight",
                           "production");
        auto before = queue_.snapshot();
        if (!before) return eve::Result<void>::failure(before.status());
        beforeJson_   = std::move(before).takeValue();
        staged_       = std::make_unique<production::WorkQueue>();
        auto restored = staged_->restore(beforeJson_);
        if (!restored) return restored;
        auto task = staged_->enqueue(owner_, kind_, product_, context_, duration_.seconds(), priority_);
        if (!task) return eve::Result<void>::failure(task.status());
        taskId_    = std::move(task).takeValue();
        auto after = staged_->snapshot();
        if (!after) return eve::Result<void>::failure(after.status());
        afterJson_ = std::move(after).takeValue();
        prepared_  = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_)
            return failure(eve::DiagnosticCode::Conflict, "RTS production participant has no prepared stage",
                           "production");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != beforeJson_)
            return failure(eve::DiagnosticCode::StaleHandle,
                           "RTS production queue changed while transaction was staged", "production");
        auto restored = queue_.restore(afterJson_);
        if (!restored) return restored;
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (committed_)
            return failure(eve::DiagnosticCode::Conflict, "committed production task requires compensation",
                           "production");
        staged_.reset();
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        if (!committed_)
            return failure(eve::DiagnosticCode::Conflict, "production queue has no committed task to compensate",
                           "production");
        auto current = queue_.snapshot();
        if (!current) return eve::Result<void>::failure(current.status());
        if (current.value() != afterJson_)
            return failure(eve::DiagnosticCode::StaleHandle, "RTS production queue changed before compensation",
                           "production");
        auto restored = queue_.restore(beforeJson_);
        if (!restored) return restored;
        staged_.reset();
        beforeJson_.clear();
        afterJson_.clear();
        prepared_  = false;
        committed_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] const std::string& taskId() const noexcept { return taskId_; }

private:
    production::WorkQueue&                 queue_;
    std::string                            owner_;
    std::string                            kind_;
    std::string                            product_;
    eve::Value                             context_;
    Duration                               duration_;
    int                                    priority_ = 0;
    std::unique_ptr<production::WorkQueue> staged_;
    std::string                            beforeJson_;
    std::string                            afterJson_;
    std::string                            taskId_;
    bool                                   prepared_  = false;
    bool                                   committed_ = false;
};

class ActionParticipant final : public transaction::ITransactionParticipant {
public:
    ActionParticipant(action::ActionRuntime& runtime, action::ActionDefinition definition,
                      action::ActionRequest request, SimulationTick tick, Duration delta)
        : runtime_(runtime),
          definition_(std::move(definition)),
          request_(std::move(request)),
          tick_(tick),
          delta_(delta) {}

    [[nodiscard]] std::string_view name() const noexcept override { return "rts-build-action"; }

    [[nodiscard]] eve::Result<void> prepare(const transaction::TransactionContext&) override {
        if (execution_)
            return failure(eve::DiagnosticCode::Conflict, "RTS Action participant is already in flight", "action");
        auto submitted = runtime_.submit(std::move(definition_), std::move(request_));
        if (!submitted) return eve::Result<void>::failure(submitted.status());
        execution_ = std::move(submitted).takeValue();
        prepared_  = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> commit(const transaction::TransactionContext&) override {
        if (!prepared_ || committed_ || !execution_)
            return failure(eve::DiagnosticCode::Conflict, "RTS Action participant has no prepared execution", "action");
        auto advanced = runtime_.advance(*execution_, tick_, delta_);
        if (!advanced) return eve::Result<void>::failure(advanced.status());
        std::move(advanced).takeValue();
        committed_ = true;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const transaction::TransactionContext&) override {
        if (committed_)
            return failure(eve::DiagnosticCode::Conflict, "committed RTS Action requires compensation", "action");
        if (!execution_) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
        const auto* current = runtime_.find(*execution_);
        if (current != nullptr && current->phase() != action::ActionPhase::Failed &&
            current->phase() != action::ActionPhase::Cancelled && current->phase() != action::ActionPhase::Completed) {
            auto cancelled = runtime_.cancel(*execution_, tick_);
            if (!cancelled) return cancelled;
        }
        prepared_ = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const transaction::TransactionContext&) override {
        // ActionRuntime's terminal record is an audit record. It has no
        // mutable business state once the other participants are compensated.
        committed_ = false;
        prepared_  = false;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    }

    [[nodiscard]] action::ActionExecutionId executionId() const noexcept {
        return execution_.value_or(action::ActionExecutionId{});
    }

private:
    action::ActionRuntime&                   runtime_;
    action::ActionDefinition                 definition_;
    action::ActionRequest                    request_;
    SimulationTick                           tick_;
    Duration                                 delta_;
    std::optional<action::ActionExecutionId> execution_;
    bool                                     prepared_  = false;
    bool                                     committed_ = false;
};

}  // namespace

eve::Result<RTSBuildReceipt> RTSProductionActionAdapter::build(Building& building, action::ActionRuntime& action,
                                                               resource::IResourceAccount& account,
                                                               resource::CostSpec cost, std::string product,
                                                               Duration duration, std::string productionKind,
                                                               int priority, std::string transactionId) {
    const auto subject = building.identity()->subject;
    if (!subject.isValid())
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS building build requires a valid building subject", "building.subject");
    auto* production = building.production()->values.queueForComposition();
    auto* orders     = building.orders()->values.queueForComposition();
    if (production == nullptr || orders == nullptr)
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvariantViolation,
                                        "RTS building build components are not initialized", "building");

    RTSBuildRequest request;
    request.production           = production;
    request.orders               = orders;
    request.action               = &action;
    request.account              = &account;
    request.cost                 = std::move(cost);
    request.owner                = subject.format();
    request.productionKind       = std::move(productionKind);
    request.product              = std::move(product);
    request.duration             = std::move(duration);
    request.priority             = priority;
    request.transactionId        = std::move(transactionId);
    request.actionRequest.source = ecs::handle_of(&building);
    return build(std::move(request));
}

eve::Result<RTSBuildReceipt> RTSProductionActionAdapter::build(RTSBuildRequest request) {
    if (request.production == nullptr || request.orders == nullptr || request.action == nullptr ||
        request.account == nullptr)
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS build requires production, orders, Action and account ports", "request");
    if (request.owner.empty() || request.productionKind.empty() || request.product.empty())
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS build requires owner, kind and product", "request");
    if (!request.cost.isValid())
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS build requires a validated resource cost", "cost");
    if (request.duration.nanoseconds() <= 0 || request.actionDelta.nanoseconds() < 0)
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument, "RTS build durations are invalid",
                                        "duration");

    if (!request.actionDefinition.id.isValid()) {
        auto id = eve::LogicalId::fromParts("rts", "build." + request.product);
        if (!id)
            return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                            "RTS product cannot form an Action id", "product");
        request.actionDefinition.id = *id;
    }
    if (request.actionDefinition.cost || request.actionDefinition.activeExecutionRequired ||
        !request.actionDefinition.effectIds.empty())
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS build Action must be effect-free; effect is a transaction port",
                                        "actionDefinition");
    request.actionDefinition.timing = {};
    if (!request.actionRequest.actionId.isValid()) request.actionRequest.actionId = request.actionDefinition.id;
    if (request.actionRequest.actionId != request.actionDefinition.id)
        return failure<RTSBuildReceipt>(eve::DiagnosticCode::InvalidArgument,
                                        "RTS build Action request does not match definition", "actionRequest");
    request.actionRequest.requestedTick = request.tick;

    if (request.transactionId.empty()) request.transactionId = "rts.build." + request.product;
    transaction::TransactionContext context(std::move(request.transactionId));
    OrderParticipant      order(*request.orders, request.orderKind, request.orderPriority, request.orderTimeoutSeconds,
                                request.product, request.owner);
    ProductionParticipant production(*request.production, request.owner, request.productionKind, request.product,
                                     std::move(request.context), request.duration, request.priority);
    ActionParticipant     action(*request.action, std::move(request.actionDefinition), std::move(request.actionRequest),
                                 request.tick, request.actionDelta);
    std::vector<transaction::ITransactionParticipant*> participants;
    participants.reserve(request.effect ? 4u : 3u);
    participants.push_back(&order);
    participants.push_back(&production);
    if (request.effect != nullptr) participants.push_back(request.effect);
    participants.push_back(&action);

    auto committed = transaction::AtomicResourcePayment::execute(
        context, *request.account, request.cost, std::span<transaction::ITransactionParticipant*>(participants));
    if (!committed) return eve::Result<RTSBuildReceipt>::failure(committed.status());
    auto            receipt = std::move(committed).takeValue();
    RTSBuildReceipt result{std::move(receipt), order.orderId(), production.taskId(), action.executionId()};
    return eve::Result<RTSBuildReceipt>::success(std::move(result), eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::rts
