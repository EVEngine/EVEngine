#include "economy/Market.h"

#include "common/Diagnostic.h"
#include "common/Value.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

namespace eve::economy {
namespace {

std::string lowerCopy(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

const Value* objectField(const Value::Object& object, const char* key) {
    auto it = object.find(key);
    if (it == object.end()) return nullptr;
    return &it->second;
}

template <class T>
Result<T> fail(const Status& status) {
    return Result<T>::failure(status);
}

Result<void> readString(const Value::Object& object, const char* key, std::string& out, bool required) {
    const Value* value = objectField(object, key);
    if (!value) {
        if (required) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           std::string(key) + " is required",
                                                           std::string("economy.market.") + key));
        }
        return Result<void>::success();
    }
    const auto* text = value->getIf<std::string>();
    if (!text) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       std::string(key) + " must be a string",
                                                       std::string("economy.market.") + key));
    }
    out = *text;
    return Result<void>::success();
}

Result<void> readInt(const Value::Object& object, const char* key, int& out, bool required) {
    const Value* value = objectField(object, key);
    if (!value) {
        if (required) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           std::string(key) + " is required",
                                                           std::string("economy.market.") + key));
        }
        return Result<void>::success();
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        out = static_cast<int>(*integer);
        return Result<void>::success();
    }
    if (const auto* number = value->getIf<double>()) {
        out = static_cast<int>(*number);
        return Result<void>::success();
    }
    return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                   std::string(key) + " must be an integer",
                                                   std::string("economy.market.") + key));
}

Result<void> readBool(const Value::Object& object, const char* key, bool& out) {
    const Value* value = objectField(object, key);
    if (!value) return Result<void>::success();
    const auto* flag = value->getIf<bool>();
    if (!flag) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       std::string(key) + " must be a bool",
                                                       std::string("economy.market.") + key));
    }
    out = *flag;
    return Result<void>::success();
}

Result<MarketListing> parseListing(const Value::Object& object) {
    MarketListing listing;
    if (auto ok = readString(object, "resourceType", listing.resourceType, true); !ok)
        return fail<MarketListing>(ok.status());
    if (auto ok = readInt(object, "basePrice", listing.basePrice, true); !ok) return fail<MarketListing>(ok.status());
    if (auto ok = readInt(object, "buyPremiumBps", listing.buyPremiumBps, false); !ok)
        return fail<MarketListing>(ok.status());
    if (auto ok = readInt(object, "sellDiscountBps", listing.sellDiscountBps, false); !ok)
        return fail<MarketListing>(ok.status());
    if (auto ok = readInt(object, "stockCap", listing.stockCap, false); !ok) return fail<MarketListing>(ok.status());
    return Result<MarketListing>::success(std::move(listing));
}

Result<MarketConfig> parseMarketObject(const Value::Object& object) {
    MarketConfig config;
    if (auto ok = readString(object, "id", config.id, true); !ok) return fail<MarketConfig>(ok.status());

    std::string scopeText;
    if (auto ok = readString(object, "scope", scopeText, true); !ok) return fail<MarketConfig>(ok.status());
    auto scope = parseMarketScope(scopeText);
    if (!scope) return fail<MarketConfig>(scope.status());
    config.scope = scope.value();

    if (auto ok = readString(object, "regionId", config.regionId, false); !ok) return fail<MarketConfig>(ok.status());
    if (auto ok = readString(object, "nationId", config.nationId, false); !ok) return fail<MarketConfig>(ok.status());
    if (auto ok = readString(object, "currencyType", config.currencyType, false); !ok)
        return fail<MarketConfig>(ok.status());
    if (auto ok = readInt(object, "tariffBps", config.tariffBps, false); !ok) return fail<MarketConfig>(ok.status());
    if (auto ok = readBool(object, "enabled", config.enabled); !ok) return fail<MarketConfig>(ok.status());

    if (const Value* listingsValue = objectField(object, "listings")) {
        const auto* listings = listingsValue->getIf<Value::Array>();
        if (!listings) {
            return Result<MarketConfig>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "listings must be an array", "economy.market.listings"));
        }
        config.listings.reserve(listings->size());
        for (const Value& entry : *listings) {
            const auto* listingObject = entry.getIf<Value::Object>();
            if (!listingObject) {
                return Result<MarketConfig>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "listing entries must be objects", "economy.market.listings"));
            }
            auto listing = parseListing(*listingObject);
            if (!listing) return fail<MarketConfig>(listing.status());
            config.listings.push_back(std::move(listing).takeValue());
        }
    }

    if (const Value* schema = objectField(object, "schemaId")) {
        const auto* text = schema->getIf<std::string>();
        if (!text || *text != "eve.economy.market") {
            return Result<MarketConfig>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "schemaId must be eve.economy.market", "economy.market.schemaId"));
        }
    }
    if (const Value* version = objectField(object, "schemaVersion")) {
        int schemaVersion = 0;
        if (const auto* integer = version->getIf<std::int64_t>()) {
            schemaVersion = static_cast<int>(*integer);
        } else if (const auto* number = version->getIf<double>()) {
            schemaVersion = static_cast<int>(*number);
        } else {
            return Result<MarketConfig>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "schemaVersion must be an integer", "economy.market.schemaVersion"));
        }
        if (schemaVersion != 1) {
            return Result<MarketConfig>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "unsupported economy.market schemaVersion",
                                                                   "economy.market.schemaVersion"));
        }
    }

    return Result<MarketConfig>::success(std::move(config));
}

const MarketListing* findListing(const MarketConfig& market, const std::string& resourceType) {
    for (const MarketListing& listing : market.listings) {
        if (listing.resourceType == resourceType) return &listing;
    }
    return nullptr;
}

}  // namespace

Result<MarketScope> parseMarketScope(std::string_view text) {
    const std::string lowered = lowerCopy(text);
    if (lowered == "regional" || lowered == "region") return Result<MarketScope>::success(MarketScope::Regional);
    if (lowered == "national" || lowered == "nation") return Result<MarketScope>::success(MarketScope::National);
    return Result<MarketScope>::failure(Diagnostic::error(
        DiagnosticCode::InvalidArgument, "scope must be regional or national", "economy.market.scope"));
}

std::string marketScopeName(MarketScope scope) {
    switch (scope) {
        case MarketScope::Regional: return "regional";
        case MarketScope::National: return "national";
    }
    return {};
}

Result<void> normalizeMarketConfig(MarketConfig& config) {
    if (config.id.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "market id is required", "economy.market.id"));
    }
    if (config.currencyType.empty()) config.currencyType = "gold";
    if (config.tariffBps < 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "tariffBps must be >= 0", "economy.market.tariffBps"));
    }

    if (config.scope == MarketScope::Regional) {
        if (config.regionId.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "regional market requires regionId", "economy.market.regionId"));
        }
    } else if (config.scope == MarketScope::National) {
        if (config.nationId.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "national market requires nationId", "economy.market.nationId"));
        }
        if (!config.regionId.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "national market must not set regionId", "economy.market.regionId"));
        }
    } else {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown market scope", "economy.market.scope"));
    }

    std::unordered_set<std::string> seenTypes;
    for (const MarketListing& listing : config.listings) {
        if (listing.resourceType.empty()) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "listing.resourceType is required", "economy.market.listings"));
        }
        if (listing.basePrice <= 0) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "listing.basePrice must be > 0", "economy.market.listings"));
        }
        if (listing.buyPremiumBps < 0) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "listing.buyPremiumBps must be >= 0", "economy.market.listings"));
        }
        if (listing.sellDiscountBps < 0 || listing.sellDiscountBps > 10000) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "listing.sellDiscountBps must be in [0,10000]",
                                                           "economy.market.listings"));
        }
        if (listing.stockCap < 0) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "listing.stockCap must be >= 0", "economy.market.listings"));
        }
        if (!seenTypes.insert(listing.resourceType).second) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "duplicate listing.resourceType in one market", "economy.market.listings"));
        }
    }
    return Result<void>::success();
}

std::unordered_map<std::string, MarketConfig>& MarketRegistry::markets() {
    static std::unordered_map<std::string, MarketConfig> s_markets;
    return s_markets;
}

Result<void> MarketRegistry::registerMarket(const MarketConfig& config) {
    MarketConfig copy = config;
    if (auto ok = normalizeMarketConfig(copy); !ok) return ok;
    markets()[copy.id] = std::move(copy);
    return Result<void>::success();
}

Result<int> MarketRegistry::loadFromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return fail<int>(parsed.status());
    Value root = std::move(parsed).takeValue();

    int registered = 0;
    if (const auto* array = root.getIf<Value::Array>()) {
        for (const Value& entry : *array) {
            const auto* object = entry.getIf<Value::Object>();
            if (!object) {
                return Result<int>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "market array entries must be objects", "economy.market"));
            }
            auto config = parseMarketObject(*object);
            if (!config) return fail<int>(config.status());
            if (auto ok = registerMarket(config.value()); !ok) return fail<int>(ok.status());
            ++registered;
        }
        return Result<int>::success(registered);
    }

    const auto* object = root.getIf<Value::Object>();
    if (!object) {
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                      "market JSON must be an object or array", "economy.market"));
    }
    auto config = parseMarketObject(*object);
    if (!config) return fail<int>(config.status());
    if (auto ok = registerMarket(config.value()); !ok) return fail<int>(ok.status());
    return Result<int>::success(1);
}

const MarketConfig* MarketRegistry::find(const std::string& id) {
    auto it = markets().find(id);
    if (it == markets().end()) return nullptr;
    return &it->second;
}

const MarketConfig* MarketRegistry::findRegional(const std::string& regionId) {
    if (regionId.empty()) return nullptr;
    const MarketConfig* best = nullptr;
    for (const auto& [id, market] : markets()) {
        if (market.scope != MarketScope::Regional || !market.enabled) continue;
        if (market.regionId != regionId) continue;
        if (best == nullptr || market.id < best->id) best = &market;
    }
    return best;
}

const MarketConfig* MarketRegistry::findNational(const std::string& nationId) {
    if (nationId.empty()) return nullptr;
    const MarketConfig* best = nullptr;
    for (const auto& [id, market] : markets()) {
        if (market.scope != MarketScope::National || !market.enabled) continue;
        if (market.nationId != nationId) continue;
        if (best == nullptr || market.id < best->id) best = &market;
    }
    return best;
}

int MarketRegistry::count() { return static_cast<int>(markets().size()); }

bool MarketRegistry::has(const std::string& id) { return find(id) != nullptr; }

const MarketConfig* MarketRegistry::marketAt(int index) {
    if (index < 0) return nullptr;
    std::vector<const std::string*> ids;
    ids.reserve(markets().size());
    for (const auto& entry : markets()) ids.push_back(&entry.first);
    if (index >= static_cast<int>(ids.size())) return nullptr;
    std::sort(ids.begin(), ids.end(), [](const std::string* left, const std::string* right) { return *left < *right; });
    return find(*ids[static_cast<std::size_t>(index)]);
}

void MarketRegistry::clear() { markets().clear(); }

Result<int> MarketRegistry::quoteBuyPrice(const std::string& marketId, const std::string& resourceType) {
    const MarketConfig* market = find(marketId);
    if (market == nullptr) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "market was not found", "economy.market.id"));
    }
    const MarketListing* listing = findListing(*market, resourceType);
    if (listing == nullptr) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "market listing was not found", "economy.market.listings"));
    }
    const std::int64_t scaled =
        static_cast<std::int64_t>(listing->basePrice) * (10000 + static_cast<std::int64_t>(listing->buyPremiumBps));
    const int price = static_cast<int>(scaled / 10000);
    return Result<int>::success(price < 1 ? 1 : price);
}

Result<int> MarketRegistry::quoteSellPrice(const std::string& marketId, const std::string& resourceType) {
    const MarketConfig* market = find(marketId);
    if (market == nullptr) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "market was not found", "economy.market.id"));
    }
    const MarketListing* listing = findListing(*market, resourceType);
    if (listing == nullptr) {
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "market listing was not found", "economy.market.listings"));
    }
    const std::int64_t scaled =
        static_cast<std::int64_t>(listing->basePrice) * (10000 - static_cast<std::int64_t>(listing->sellDiscountBps));
    const int price = static_cast<int>(scaled / 10000);
    return Result<int>::success(price < 0 ? 0 : price);
}

}  // namespace eve::economy
