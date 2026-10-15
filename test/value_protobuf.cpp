#include "common/ProtobufDynamicValue.h"

#include "dynamic_value.pb.h"
#include "zeroerr/unittest.h"

#include <limits>
#include <string>

TEST_CASE("value-protobuf.roundTripsExactDynamicValues") {
    auto source = eve::Value::fromJson(
        R"({"large":9223372036854775807,"small":-9223372036854775808,"items":[null,true,1.25,"text"]})");
    REQUIRE(source.ok());
    eve::persistence::proto::DynamicValue encoded;
    REQUIRE(eve::proto_support::encodeDynamicValue(source.value(), encoded).ok());
    std::string wire;
    REQUIRE(encoded.SerializeToString(&wire));
    eve::persistence::proto::DynamicValue parsed;
    REQUIRE(parsed.ParseFromString(wire));
    auto restored = eve::proto_support::decodeDynamicValue(parsed);
    REQUIRE(restored.ok());
    CHECK_EQ(restored.value(), source.value());
}

TEST_CASE("value-protobuf.rejectsInvalidValuesWithoutChangingDestination") {
    eve::persistence::proto::DynamicValue destination;
    destination.set_string_value("before");
    CHECK(
        !eve::proto_support::encodeDynamicValue(eve::Value(std::numeric_limits<double>::infinity()), destination).ok());
    CHECK_EQ(destination.string_value(), std::string("before"));

    eve::persistence::proto::DynamicValue missing;
    CHECK(!eve::proto_support::decodeDynamicValue(missing).ok());
    eve::persistence::proto::DynamicValue invalidNull;
    invalidNull.set_null_value(false);
    CHECK(!eve::proto_support::decodeDynamicValue(invalidNull).ok());

    eve::persistence::proto::DynamicValue duplicate;
    auto*                                 object = duplicate.mutable_object_value();
    for (int index = 0; index < 2; ++index) {
        auto* field = object->add_fields();
        field->set_key("same");
        field->mutable_value()->set_int_value(index);
    }
    CHECK(!eve::proto_support::decodeDynamicValue(duplicate).ok());
}
