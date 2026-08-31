#include "rpg/ShopTransaction.h"

#include "inventory/InventorySystem.h"
#include "rpg/GameState.h"
#include "rpg/ShopCatalogue.h"

#include <cmath>
#include <utility>

namespace eve::rpg {
namespace {

eve::Result<int> tradeFailure(eve::DiagnosticCode code, std::string message,
                              std::string path, const std::string &itemId) {
    return eve::Result<int>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {{"itemId", itemId}}, "rpg.shop-transaction"));
}

eve::Result<double> validateTrade(const GameState &gameState, const std::string &currencyId,
                                  const std::string &itemId, int quantity, double unitPrice,
                                  bool purchase) {
    if (currencyId.empty() || itemId.empty() || quantity <= 0 ||
        !std::isfinite(unitPrice) || unitPrice < 0.0)
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "shop trade requires currency/item ids, positive quantity, and finite non-negative unit price",
            "trade", {{"itemId", itemId}}, "rpg.shop-transaction"));
    const double total = unitPrice * static_cast<double>(quantity);
    const double balance = gameState.getVariable(currencyId);
    const double result = purchase ? balance - total : balance + total;
    if (!std::isfinite(total) || !std::isfinite(balance) || !std::isfinite(result))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "shop trade would produce a non-finite currency value",
            "unitPrice", {{"itemId", itemId}}, "rpg.shop-transaction"));
    if (purchase && balance < total)
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "shop purchase has insufficient currency",
            "currency", {{"itemId", itemId}}, "rpg.shop-transaction"));
    return eve::Result<double>::success(total);
}

eve::Result<int> rollbackFailure(GameState &gameState, const std::string &snapshot,
                                 const eve::Status &commitStatus, const std::string &itemId) {
    auto restored = gameState.restoreSnapshotJson(snapshot);
    if (!restored.ok())
        return tradeFailure(eve::DiagnosticCode::InvariantViolation,
                            "shop trade rollback could not restore GameState", "rollback", itemId);
    return eve::Result<int>::failure(commitStatus);
}

}  // namespace

eve::Result<int> ShopTransaction::buyOffer(GameState &gameState, inventory::Bag &bag,
                                           const std::string &currencyId,
                                           const std::string &offerId, int quantity) {
    const ShopOffer *offer = ShopCatalogue::find(offerId);
    if (!offer)
        return tradeFailure(eve::DiagnosticCode::NotFound,
                            "shop purchase references an unknown offer", "offerId", offerId);
    return buy(gameState, bag, currencyId, offer->itemId, quantity,
               static_cast<double>(offer->buyPrice));
}

eve::Result<int> ShopTransaction::sellOffer(GameState &gameState, inventory::Bag &bag,
                                            const std::string &currencyId,
                                            const std::string &offerId, int quantity) {
    const ShopOffer *offer = ShopCatalogue::find(offerId);
    if (!offer)
        return tradeFailure(eve::DiagnosticCode::NotFound,
                            "shop sale references an unknown offer", "offerId", offerId);
    return sell(gameState, bag, currencyId, offer->itemId, quantity,
                static_cast<double>(offer->sellPrice));
}

eve::Result<int> ShopTransaction::buy(GameState &gameState, inventory::Bag &bag,
                                      const std::string &currencyId, const std::string &itemId,
                                      int quantity, double unitPrice) {
    auto validated = validateTrade(gameState, currencyId, itemId, quantity, unitPrice, true);
    if (!validated.ok()) return eve::Result<int>::failure(validated.status());
    const double total = std::move(validated).takeValue();
    auto prepared = inventory::InventorySystem::prepareAddBatch(&bag, {{itemId, quantity}});
    if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
    auto before = gameState.snapshotJson();
    if (!before.ok()) return eve::Result<int>::failure(before.status());
    std::string snapshot = std::move(before).takeValue();

    gameState.addVariable(currencyId, -total);
    auto committed = inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    if (!committed.ok()) return rollbackFailure(gameState, snapshot, committed.status(), itemId);
    return committed;
}

eve::Result<int> ShopTransaction::sell(GameState &gameState, inventory::Bag &bag,
                                       const std::string &currencyId, const std::string &itemId,
                                       int quantity, double unitPrice) {
    auto validated = validateTrade(gameState, currencyId, itemId, quantity, unitPrice, false);
    if (!validated.ok()) return eve::Result<int>::failure(validated.status());
    const double total = std::move(validated).takeValue();
    auto prepared = inventory::InventorySystem::prepareRemove(&bag, itemId, quantity);
    if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
    auto before = gameState.snapshotJson();
    if (!before.ok()) return eve::Result<int>::failure(before.status());
    std::string snapshot = std::move(before).takeValue();

    gameState.addVariable(currencyId, total);
    auto committed = inventory::InventorySystem::commitRemove(std::move(prepared).takeValue());
    if (!committed.ok()) return rollbackFailure(gameState, snapshot, committed.status(), itemId);
    return committed;
}

}  // namespace eve::rpg
