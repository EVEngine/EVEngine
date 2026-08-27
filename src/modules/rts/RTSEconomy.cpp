#include "rts/RTSEconomy.h"

#include <utility>

namespace eve::rts {

RTSEconomyAdapter::RTSEconomyAdapter(eve::economy::EconomyLedger& ledger)
    : account_(ledger) {}

eve::resource::IResourceAccount& RTSEconomyAdapter::account() noexcept {
    return account_;
}

eve::Result<eve::transaction::TransactionReceipt> RTSEconomyAdapter::pay(
    const eve::resource::CostSpec& cost,
    eve::transaction::ITransactionParticipant& effect, std::string transactionId) {
    if (transactionId.empty()) transactionId = "rts.economy.payment";
    return eve::transaction::AtomicResourcePayment::execute(
        eve::transaction::TransactionContext(std::move(transactionId)), account_, cost, effect);
}

eve::Result<eve::resource::Receipt> RTSEconomyAdapter::credit(
    const eve::resource::CostSpec& cost) {
    return account_.credit(cost);
}

}  // namespace eve::rts
