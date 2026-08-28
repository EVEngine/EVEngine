#include "transaction/AtomicResourcePayment.h"

#include <array>
#include <span>
#include <utility>
#include <vector>

namespace eve::transaction {
namespace {

template <class T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

}  // namespace

eve::Result<TransactionReceipt> AtomicResourcePayment::execute(const TransactionContext&           context,
                                                               std::span<ITransactionParticipant*> participants) {
    Coordinator coordinator;
    return coordinator.execute(context, participants);
}

eve::Result<TransactionReceipt> AtomicResourcePayment::execute(const TransactionContext& context,
                                                               ITransactionParticipant&  effect) {
    std::array<ITransactionParticipant*, 1> participants{&effect};
    return execute(context, std::span<ITransactionParticipant*>(participants));
}

eve::Result<TransactionReceipt> AtomicResourcePayment::execute(const TransactionContext&        context,
                                                               eve::resource::IResourceAccount& account,
                                                               const eve::resource::CostSpec&   cost,
                                                               ITransactionParticipant&         effect) {
    std::array<ITransactionParticipant*, 1> participants{&effect};
    return execute(context, account, cost, std::span<ITransactionParticipant*>(participants));
}

eve::Result<TransactionReceipt> AtomicResourcePayment::execute(const TransactionContext&           context,
                                                               eve::resource::IResourceAccount&    account,
                                                               const eve::resource::CostSpec&      cost,
                                                               std::span<ITransactionParticipant*> participants) {
    if (!cost.isValid())
        return failure<TransactionReceipt>(eve::DiagnosticCode::InvalidArgument,
                                           "resource payment requires a validated non-empty CostSpec", "cost");

    ResourceDebitParticipant              debit(account, cost, "resource-payment");
    std::vector<ITransactionParticipant*> all;
    all.reserve(participants.size() + 1u);
    for (auto* participant : participants) all.push_back(participant);
    // The debit is deliberately last. Domain participants are therefore
    // prepared first, and a failed debit compensates any committed domain
    // participant without exposing a partially paid operation.
    all.push_back(&debit);
    Coordinator coordinator;
    return coordinator.execute(context, std::span<ITransactionParticipant*>(all));
}

eve::Result<TransactionReceipt> AtomicResourcePayment::execute(std::string                      transactionId,
                                                               eve::resource::IResourceAccount& account,
                                                               const eve::resource::CostSpec&   cost,
                                                               ITransactionParticipant&         effect) {
    if (transactionId.empty())
        return failure<TransactionReceipt>(eve::DiagnosticCode::InvalidArgument,
                                           "resource payment transaction id must not be empty", "transactionId");
    return execute(TransactionContext(std::move(transactionId)), account, cost, effect);
}

}  // namespace eve::transaction
