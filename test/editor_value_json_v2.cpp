#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorValueJson.h"

#include <limits>
#include <string>

using namespace eve::editor;

TEST_CASE("editor.v2.json_round_trips_without_host_dependencies") {
    const std::string source = R"({"z":"quote\"\\","a":[null,true,-7,1.25,"line\n\u4e2d\ud83d\ude80"]})";
    const auto        parsed = editorValueFromJson(source);
    REQUIRE(parsed.accepted());
    REQUIRE(parsed.value.has_value());

    const std::string encoded = editorValueToJson(*parsed.value);
    CHECK(encoded.find(R"("a":[null,true,-7,1.25,"line\n中🚀"])") == 1);
    CHECK(encoded.find(R"("z":"quote\"\\")") != std::string::npos);

    const auto roundTrip = editorValueFromJson(encoded);
    REQUIRE(roundTrip.accepted());
    REQUIRE(roundTrip.value.has_value());
    CHECK(*roundTrip.value == *parsed.value);
    CHECK_EQ(editorValueContentHash(*roundTrip.value), editorValueContentHash(*parsed.value));
}

TEST_CASE("editor.v2.json_rejects_invalid_and_out_of_range_input") {
    CHECK(!editorValueFromJson(R"({"value":01})").accepted());
    CHECK(!editorValueFromJson(R"([1,])").accepted());
    CHECK(!editorValueFromJson(R"("\ud800")").accepted());
    CHECK(!editorValueFromJson("1e9999").accepted());
    CHECK(!editorValueFromJson("true false").accepted());
}

TEST_CASE("editor.v2.json_serializes_non_finite_numbers_as_null") {
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::infinity())), std::string("null"));
    CHECK_EQ(editorValueToJson(EditorValue(std::numeric_limits<double>::quiet_NaN())), std::string("null"));
}
