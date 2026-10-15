#include "rpg/ShopCatalogue.h"
#include <climits>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include "common/Json.h"
#include "inventory/InventorySystem.h"
#include "inventory/Item.h"
#include "rpg/GameState.h"
#include "rpg/ShopTransaction.h"

// ShopCatalogue.cpp
namespace eve::rpg {
namespace {

std::vector<ShopOffer> &offers() {
    static std::vector<ShopOffer> value;
    return value;
}

std::unordered_map<std::string, std::size_t> &offerIndex() {
    static std::unordered_map<std::string, std::size_t> value;
    return value;
}

bool validId(const std::string &value) {
    if (value.empty() || value.size() > 256) return false;
    for (unsigned char ch : value)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

eve::Result<int> failure(std::string message, std::string path) {
    return eve::Result<int>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, message, path, {}, "rpg.shop-catalogue"));
}

bool onlyKnownFields(const eve::json::Value &object) {
    static const std::unordered_set<std::string> known = {"id", "itemId", "name", "desc", "buyPrice", "sellPrice"};
    for (const auto &key : object.keys())
        if (known.count(key) == 0) return false;
    return true;
}

}  // namespace

eve::Result<int> ShopCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    if (!document.valid()) return failure(parseError.empty() ? "invalid JSON" : parseError, "$");
    const auto root = document.root();
    if (!root.isArray() || root.size() == 0) return failure("shop catalogue must be a non-empty array", "$");

    std::vector<ShopOffer>                       proposed;
    std::unordered_map<std::string, std::size_t> proposedIndex;
    proposed.reserve(root.size());
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto        object = root.at(index);
        const std::string path   = "$[" + std::to_string(index) + "]";
        if (!object.isObject()) return failure("shop offer must be an object", path);
        if (!onlyKnownFields(object)) return failure("shop offer contains an unknown field", path);
        const auto id          = object.get("id");
        const auto itemIdValue = object.get("itemId");
        const auto name        = object.get("name");
        const auto description = object.get("desc");
        const auto buyPrice    = object.get("buyPrice");
        const auto sellPrice   = object.get("sellPrice");
        if (!id.isString() || !validId(id.asString()))
            return failure("offer id must be a stable non-empty id", path + ".id");
        const std::string itemId = itemIdValue.isString() ? itemIdValue.asString() : std::string{};
        if (!validId(itemId)) return failure("itemId must be a stable non-empty id", path + ".itemId");
        if (!inventory::ItemRegistry::find(itemId))
            return failure("shop offer references an unknown inventory item", path + ".itemId");
        if (!name.isString() || name.asString().empty() || name.asString().size() > 512)
            return failure("name must be a non-empty string of at most 512 bytes", path + ".name");
        if (!description.isString() || description.asString().size() > 2048)
            return failure("desc must be a string of at most 2048 bytes", path + ".desc");
        if (!buyPrice.isInt64() || buyPrice.asInt64() < 0 || buyPrice.asInt64() > INT_MAX)
            return failure("buyPrice must be a non-negative integer", path + ".buyPrice");
        if (!sellPrice.isInt64() || sellPrice.asInt64() < 0 || sellPrice.asInt64() > buyPrice.asInt64())
            return failure("sellPrice must be a non-negative integer no greater than buyPrice", path + ".sellPrice");
        if (proposedIndex.count(id.asString()) != 0) return failure("duplicate offer id", path + ".id");
        ShopOffer offer;
        offer.id          = id.asString();
        offer.itemId      = itemId;
        offer.displayName = name.asString();
        offer.description = description.asString();
        offer.buyPrice    = static_cast<int>(buyPrice.asInt64());
        offer.sellPrice   = static_cast<int>(sellPrice.asInt64());
        proposedIndex.emplace(offer.id, proposed.size());
        proposed.push_back(std::move(offer));
    }
    offers()     = std::move(proposed);
    offerIndex() = std::move(proposedIndex);
    return eve::Result<int>::success(static_cast<int>(offers().size()));
}

void ShopCatalogue::clear() {
    offers().clear();
    offerIndex().clear();
}

int ShopCatalogue::count() { return static_cast<int>(offers().size()); }

const ShopOffer *ShopCatalogue::find(const std::string &id) {
    const auto found = offerIndex().find(id);
    return found == offerIndex().end() ? nullptr : &offers()[found->second];
}

const ShopOffer *ShopCatalogue::at(int index) {
    return index < 0 || index >= count() ? nullptr : &offers()[static_cast<std::size_t>(index)];
}

}  // namespace eve::rpg

// ShopTransaction.cpp
namespace eve::rpg {
namespace {

eve::Result<int> tradeFailure(eve::DiagnosticCode code, std::string message, std::string path,
                              const std::string &itemId) {
    return eve::Result<int>::failure(
        eve::Diagnostic::error(code, message, path, {{"itemId", itemId}}, "rpg.shop-transaction"));
}

eve::Result<double> validateTrade(const GameState &gameState, const std::string &currencyId, const std::string &itemId,
                                  int quantity, double unitPrice, bool purchase) {
    if (currencyId.empty() || itemId.empty() || quantity <= 0 || !std::isfinite(unitPrice) || unitPrice < 0.0)
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument,
            "shop trade requires currency/item ids, positive quantity, and finite non-negative unit price", "trade",
            {{"itemId", itemId}}, "rpg.shop-transaction"));
    const double total   = unitPrice * static_cast<double>(quantity);
    const double balance = gameState.getVariable(currencyId);
    const double result  = purchase ? balance - total : balance + total;
    if (!std::isfinite(total) || !std::isfinite(balance) || !std::isfinite(result))
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "shop trade would produce a non-finite currency value", "unitPrice",
            {{"itemId", itemId}}, "rpg.shop-transaction"));
    if (purchase && balance < total)
        return eve::Result<double>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "shop purchase has insufficient currency", "currency",
            {{"itemId", itemId}}, "rpg.shop-transaction"));
    return eve::Result<double>::success(total);
}

eve::Result<int> rollbackFailure(GameState &gameState, const std::string &snapshot, const eve::Status &commitStatus,
                                 const std::string &itemId) {
    auto restored = gameState.restoreSnapshotJson(snapshot);
    if (!restored.ok())
        return tradeFailure(eve::DiagnosticCode::InvariantViolation, "shop trade rollback could not restore GameState",
                            "rollback", itemId);
    return eve::Result<int>::failure(commitStatus);
}

}  // namespace

eve::Result<int> ShopTransaction::buyOffer(GameState &gameState, inventory::Bag &bag, const std::string &currencyId,
                                           const std::string &offerId, int quantity) {
    const ShopOffer *offer = ShopCatalogue::find(offerId);
    if (!offer)
        return tradeFailure(eve::DiagnosticCode::NotFound, "shop purchase references an unknown offer", "offerId",
                            offerId);
    return buy(gameState, bag, currencyId, offer->itemId, quantity, static_cast<double>(offer->buyPrice));
}

eve::Result<int> ShopTransaction::sellOffer(GameState &gameState, inventory::Bag &bag, const std::string &currencyId,
                                            const std::string &offerId, int quantity) {
    const ShopOffer *offer = ShopCatalogue::find(offerId);
    if (!offer)
        return tradeFailure(eve::DiagnosticCode::NotFound, "shop sale references an unknown offer", "offerId", offerId);
    return sell(gameState, bag, currencyId, offer->itemId, quantity, static_cast<double>(offer->sellPrice));
}

eve::Result<int> ShopTransaction::buy(GameState &gameState, inventory::Bag &bag, const std::string &currencyId,
                                      const std::string &itemId, int quantity, double unitPrice) {
    auto validated = validateTrade(gameState, currencyId, itemId, quantity, unitPrice, true);
    if (!validated.ok()) return eve::Result<int>::failure(validated.status());
    const double total    = std::move(validated).takeValue();
    auto         prepared = inventory::InventorySystem::prepareAddBatch(&bag, {{itemId, quantity}});
    if (!prepared.ok()) return eve::Result<int>::failure(prepared.status());
    auto before = gameState.snapshotJson();
    if (!before.ok()) return eve::Result<int>::failure(before.status());
    std::string snapshot = std::move(before).takeValue();

    gameState.addVariable(currencyId, -total);
    auto committed = inventory::InventorySystem::commitAddBatch(std::move(prepared).takeValue());
    if (!committed.ok()) return rollbackFailure(gameState, snapshot, committed.status(), itemId);
    return committed;
}

eve::Result<int> ShopTransaction::sell(GameState &gameState, inventory::Bag &bag, const std::string &currencyId,
                                       const std::string &itemId, int quantity, double unitPrice) {
    auto validated = validateTrade(gameState, currencyId, itemId, quantity, unitPrice, false);
    if (!validated.ok()) return eve::Result<int>::failure(validated.status());
    const double total    = std::move(validated).takeValue();
    auto         prepared = inventory::InventorySystem::prepareRemove(&bag, itemId, quantity);
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
