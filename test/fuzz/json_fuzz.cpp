#include "zeroerr/assert.h"
#include "zeroerr/fuzztest.h"

#include "common/Json.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string>
#include <vector>

using eve::json::Document;
using eve::json::Value;
using namespace zeroerr;

namespace {

class NonEmptyStringDomain final : public Domain<std::string, std::vector<char>> {
public:
    using CorpusType = std::vector<char>;

    NonEmptyStringDomain() {
        impl_.WithMinSize(1);
        impl_.WithMaxSize(4096);
    }

    CorpusType GetRandomCorpus(Rng& rng) const override { return impl_.GetRandomCorpus(rng); }
    CorpusType FromValue(const std::string& value) const override {
        if (value.empty()) return {'\0'};
        return impl_.FromValue(value);
    }
    std::string GetValue(const CorpusType& value) const override { return impl_.GetValue(value); }
    void Mutate(Rng& rng, CorpusType& value, bool onlyShrink) const override {
        // zeroerr's sequence domain indexes the corpus after choosing a mutation
        // action, even when a libFuzzer-generated corpus deserializes as empty.
        if (value.empty()) value.push_back('\0');
        impl_.Mutate(rng, value, onlyShrink);
    }

private:
    SequenceContainerOf<std::string, Arbitrary<char>> impl_{Arbitrary<char>()};
};

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

bool exerciseValue(Value value, size_t depth = 0) {
    if (depth > 256) return false;

    const int kindCount = static_cast<int>(value.isNull()) + static_cast<int>(value.isBool()) +
                          static_cast<int>(value.isNumber()) + static_cast<int>(value.isString()) +
                          static_cast<int>(value.isObject()) + static_cast<int>(value.isArray());
    if (kindCount != 1) return false;

    (void)value.asBool(true);
    (void)value.asInt(17);
    (void)value.asFloat(1.25f);
    (void)value.asDouble(2.5);
    (void)value.asString("fallback");
    (void)value.at(value.size());
    (void)value.get(nullptr);

    if (value.isArray()) {
        for (size_t i = 0; i < value.size(); ++i)
            if (!exerciseValue(value.at(i), depth + 1)) return false;
    } else if (value.isObject()) {
        const auto keys = value.keys();
        if (keys.size() != value.size()) return false;
        for (const auto& key : keys) {
            if (!value.has(key.c_str()) || !exerciseValue(value.get(key.c_str()), depth + 1)) return false;
        }
    }
    return true;
}

}  // namespace

FUZZ_TEST_CASE("fuzz.json.document") {
    FUZZ_FUNC([=](const std::string& input) {
        std::string error;
        Document    document = Document::parse(input, &error);
        if (!document.valid()) {
            REQUIRE(!document.root());
            REQUIRE(!error.empty());
            return;
        }

        REQUIRE(error.empty());
        REQUIRE(exerciseValue(document.root()));
    })
        .WithDomains(NonEmptyStringDomain())
        .WithSeeds({{"null"},
                    {"{}"},
                    {"[]"},
                    {R"({"name":"eve","values":[0,-1,2.5,true,null]})"},
                    {R"("\ud83d\ude00")"},
                    {std::string(257, '[') + "0" + std::string(257, ']')}})
        .Run(envInt("EVENGINE_FUZZ_RUNS", 2000, 1000000), envInt("EVENGINE_FUZZ_SEED", 1, 2147483647));
}
