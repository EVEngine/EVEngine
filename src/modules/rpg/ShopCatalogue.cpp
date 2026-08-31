#include "rpg/ShopCatalogue.h"

#include "common/Json.h"
#include "inventory/Item.h"

#include <climits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

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
    return eve::Result<int>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, std::move(message), std::move(path), {},
        "rpg.shop-catalogue"));
}

bool onlyKnownFields(const eve::json::Value &object) {
    static const std::unordered_set<std::string> known = {
        "id", "itemId", "name", "desc", "buyPrice", "sellPrice"};
    for (const auto &key : object.keys())
        if (known.count(key) == 0) return false;
    return true;
}

}  // namespace

eve::Result<int> ShopCatalogue::replaceFromJsonStrict(const std::string &json) {
    std::string parseError;
    const auto document = eve::json::Document::parse(json, &parseError);
    if (!document.valid()) return failure(parseError.empty() ? "invalid JSON" : parseError, "$");
    const auto root = document.root();
    if (!root.isArray() || root.size() == 0)
        return failure("shop catalogue must be a non-empty array", "$");

    std::vector<ShopOffer> proposed;
    std::unordered_map<std::string, std::size_t> proposedIndex;
    proposed.reserve(root.size());
    for (std::size_t index = 0; index < root.size(); ++index) {
        const auto object = root.at(index);
        const std::string path = "$[" + std::to_string(index) + "]";
        if (!object.isObject()) return failure("shop offer must be an object", path);
        if (!onlyKnownFields(object)) return failure("shop offer contains an unknown field", path);
        const auto id = object.get("id");
        const auto itemIdValue = object.get("itemId");
        const auto name = object.get("name");
        const auto description = object.get("desc");
        const auto buyPrice = object.get("buyPrice");
        const auto sellPrice = object.get("sellPrice");
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
            return failure("sellPrice must be a non-negative integer no greater than buyPrice",
                           path + ".sellPrice");
        if (proposedIndex.count(id.asString()) != 0)
            return failure("duplicate offer id", path + ".id");
        ShopOffer offer;
        offer.id = id.asString();
        offer.itemId = itemId;
        offer.displayName = name.asString();
        offer.description = description.asString();
        offer.buyPrice = static_cast<int>(buyPrice.asInt64());
        offer.sellPrice = static_cast<int>(sellPrice.asInt64());
        proposedIndex.emplace(offer.id, proposed.size());
        proposed.push_back(std::move(offer));
    }
    offers() = std::move(proposed);
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
