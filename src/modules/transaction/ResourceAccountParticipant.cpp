#include "transaction/ResourceAccountParticipant.h"

#include <utility>

namespace eve::transaction {
namespace {

eve::Status lifecycleConflict(std::string message) {
    return eve::Status::failure(
        eve::StatusCode::Conflict,
        eve::Diagnostic::error(eve::DiagnosticCode::Conflict, std::move(message),
                               "resource.transaction"));
}

eve::Result<void> forwardFailure(eve::Result<void>& result) {
    return eve::Result<void>::failure(result.status());
}

template <class T>
eve::Result<void> forwardFailure(eve::Result<T>& result) {
    return eve::Result<void>::failure(result.status());
}

}  // namespace

ResourceDebitParticipant::ResourceDebitParticipant(eve::resource::IResourceAccount& account,
                                                   eve::resource::CostSpec cost,
                                                   std::string name)
    : account_(account), cost_(std::move(cost)), name_(std::move(name)) {
    if (name_.empty()) name_ = "resource-debit";
}

eve::Result<void> ResourceDebitParticipant::prepare(const TransactionContext& context) {
    (void)context;
    if (state_ == ResourceDebitState::Prepared || state_ == ResourceDebitState::Committed ||
        state_ == ResourceDebitState::Compensated)
        return eve::Result<void>::failure(
            lifecycleConflict("resource debit participant is already in flight or terminal"));

    auto reservation = account_.reserve(cost_);
    if (!reservation) return forwardFailure(reservation);
    reservation_ = std::move(reservation).takeValue();
    state_       = ResourceDebitState::Prepared;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ResourceDebitParticipant::commit(const TransactionContext& context) {
    (void)context;
    if (state_ != ResourceDebitState::Prepared)
        return eve::Result<void>::failure(
            lifecycleConflict("resource debit participant has no active reservation to commit"));

    auto receipt = account_.commit(reservation_);
    if (!receipt) return forwardFailure(receipt);
    state_ = ResourceDebitState::Committed;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ResourceDebitParticipant::rollback(const TransactionContext& context) {
    (void)context;
    if (state_ != ResourceDebitState::Prepared)
        return eve::Result<void>::failure(
            lifecycleConflict("resource debit participant has no active reservation to roll back"));

    auto released = account_.rollback(reservation_);
    if (!released) return forwardFailure(released);
    state_ = ResourceDebitState::RolledBack;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> ResourceDebitParticipant::compensate(const TransactionContext& context) {
    (void)context;
    if (state_ != ResourceDebitState::Committed)
        return eve::Result<void>::failure(
            lifecycleConflict("resource debit participant has no committed debit to compensate"));

    auto restored = account_.credit(cost_);
    if (!restored) return forwardFailure(restored);
    state_ = ResourceDebitState::Compensated;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::transaction
