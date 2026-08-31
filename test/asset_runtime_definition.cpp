#include "asset/RuntimeDefinition.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset;

TEST_CASE("asset.runtimeDefinition.isDeterministicAndRoundTripsAllValueKinds") {
    Value::Object root;
    root["array"] = Value(Value::Array{Value(), Value(false), Value(std::int64_t(-7)),
                                        Value(1.25), Value("text")});
    root["enabled"] = Value(true);
    root["nested"] = Value(Value::Object{{"z", Value(std::int64_t(9))}});
    auto first = encodeRuntimeDefinition(Value(root));
    auto second = encodeRuntimeDefinition(Value(root));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK_EQ(first.value(), second.value());
    REQUIRE(first.value().size() > 8);
    CHECK_EQ(std::string(reinterpret_cast<const char*>(first.value().data()), 5),
             std::string("EVDEF"));
    auto decoded = decodeRuntimeDefinition(first.value());
    REQUIRE(decoded.ok());
    auto json = decoded.value().toJson();
    auto expected = Value(std::move(root)).toJson();
    REQUIRE(json.ok());
    REQUIRE(expected.ok());
    CHECK_EQ(json.value(), expected.value());
}

TEST_CASE("asset.runtimeDefinition.rejectsUnknownTagsTrailingBytesAndBudgets") {
    auto encoded = encodeRuntimeDefinition(Value(Value::Object{{"key", Value("value")}}));
    REQUIRE(encoded.ok());
    auto unknown = encoded.value();
    unknown[8] = 0xff;
    auto badTag = decodeRuntimeDefinition(unknown);
    REQUIRE(!badTag.ok());
    CHECK_EQ(badTag.error()->code(), DiagnosticCode::ParseError);

    auto trailing = encoded.value();
    trailing.push_back(0);
    auto badTrailing = decodeRuntimeDefinition(trailing);
    REQUIRE(!badTrailing.ok());
    CHECK_EQ(badTrailing.error()->code(), DiagnosticCode::ParseError);

    RuntimeDefinitionLimits limits;
    limits.maximumBytes = encoded.value().size() - 1;
    auto tooLarge = decodeRuntimeDefinition(encoded.value(), limits);
    REQUIRE(!tooLarge.ok());
    CHECK_EQ(tooLarge.error()->code(), DiagnosticCode::ParseError);

    limits = {};
    limits.maximumStringBytes = 3;
    auto stringBudget = decodeRuntimeDefinition(encoded.value(), limits);
    REQUIRE(!stringBudget.ok());

    auto invalidUtf8 = encodeRuntimeDefinition(Value("x"));
    REQUIRE(invalidUtf8.ok());
    invalidUtf8.value().back() = 0xff;
    auto utf8Rejected = decodeRuntimeDefinition(invalidUtf8.value());
    REQUIRE(!utf8Rejected.ok());
    CHECK_EQ(utf8Rejected.error()->code(), DiagnosticCode::ParseError);
}
