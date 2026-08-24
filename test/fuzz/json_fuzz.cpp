#include "zeroerr/assert.h"
#include "zeroerr/fuzztest.h"

#include "common/Json.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>

using eve::json::Document;
using eve::json::Value;
using namespace zeroerr;

namespace {

int envInt(const char* name, int fallback, int maximum) {
    const char* text = std::getenv(name);
    if (!text || !*text) return fallback;
    try {
        const long long value = std::stoll(text);
        if (value < 1) return 1;
        return static_cast<int>(std::min<long long>(value, maximum));
    } catch (...) {
        return fallback;
    }
}

void exerciseValue(Value value, size_t depth = 0) {
    REQUIRE(depth <= 256);

    const int kindCount = static_cast<int>(value.isNull()) + static_cast<int>(value.isBool()) +
                          static_cast<int>(value.isNumber()) + static_cast<int>(value.isString()) +
                          static_cast<int>(value.isObject()) + static_cast<int>(value.isArray());
    REQUIRE_EQ(kindCount, 1);

    (void)value.asBool(true);
    (void)value.asInt(17);
    (void)value.asFloat(1.25f);
    (void)value.asDouble(2.5);
    (void)value.asString("fallback");
    (void)value.at(value.size());
    (void)value.get(nullptr);

    if (value.isArray()) {
        for (size_t i = 0; i < value.size(); ++i) exerciseValue(value.at(i), depth + 1);
    } else if (value.isObject()) {
        const auto keys = value.keys();
        REQUIRE_EQ(keys.size(), value.size());
        for (const auto& key : keys) {
            REQUIRE(value.has(key.c_str()));
            exerciseValue(value.get(key.c_str()), depth + 1);
        }
    }
}

}  // namespace

FUZZ_TEST_CASE("fuzz.json.document") {
    FUZZ_FUNC([](const std::string& input) {
        std::string error;
        Document    document = Document::parse(input, &error);
        if (!document.valid()) {
            REQUIRE(!document.root());
            REQUIRE(!error.empty());
            return;
        }

        REQUIRE(error.empty());
        exerciseValue(document.root());
    })
        .WithDomains(Arbitrary<std::string>())
        .WithSeeds({{"null"},
                    {"{}"},
                    {"[]"},
                    {R"({"name":"eve","values":[0,-1,2.5,true,null]})"},
                    {R"("\ud83d\ude00")"},
                    {std::string(257, '[') + "0" + std::string(257, ']')}})
        .Run(envInt("EVENGINE_FUZZ_RUNS", 2000, 1000000), envInt("EVENGINE_FUZZ_SEED", 1, 2147483647));
}
