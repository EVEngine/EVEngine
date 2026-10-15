#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "schema/ProtobufSchema.h"
#include "schema/SchemaRegistry.h"

#include <google/protobuf/any.pb.h>
#include <google/protobuf/descriptor.pb.h>

#include <string>

namespace {

std::string descriptorSet() {
    google::protobuf::FileDescriptorSet set;
    auto*                               file = set.add_file();
    file->set_name("runtime/hero.proto");
    file->set_package("runtime");
    file->set_syntax("proto3");
    auto* message = file->add_message_type();
    message->set_name("Hero");
    auto* name = message->add_field();
    name->set_name("name");
    name->set_number(1);
    name->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    name->set_type(google::protobuf::FieldDescriptorProto::TYPE_STRING);
    return set.SerializeAsString();
}

std::string descriptorSetWithImport() {
    google::protobuf::FileDescriptorSet set;
    auto*                               file = set.add_file();
    file->set_name("runtime/imported.proto");
    file->set_package("runtime");
    file->set_syntax("proto3");
    file->add_dependency("google/protobuf/any.proto");
    auto* message = file->add_message_type();
    message->set_name("Imported");
    auto* payload = message->add_field();
    payload->set_name("payload");
    payload->set_number(1);
    payload->set_label(google::protobuf::FieldDescriptorProto::LABEL_OPTIONAL);
    payload->set_type(google::protobuf::FieldDescriptorProto::TYPE_MESSAGE);
    payload->set_type_name(".google.protobuf.Any");
    google::protobuf::Any::descriptor()->file()->CopyTo(set.add_file());
    return set.SerializeAsString();
}

}  // namespace

TEST_CASE("schema.protobufDynamicDescriptorEncodesWithoutValue") {
    eve::schema::SchemaRegistry::clear();
    eve::schema::ProtobufSchema::clear();
    const auto set = descriptorSet();
    REQUIRE(eve::schema::ProtobufSchema::registerDescriptorSet("runtime:hero", 1, "runtime.Hero", set).ok());
    CHECK(eve::schema::ProtobufSchema::contains("runtime:hero", 1));
    auto wire = eve::schema::ProtobufSchema::encodeJson("runtime:hero", 1, R"({"name":"Ada"})");
    REQUIRE(wire.ok());
    auto json = eve::schema::ProtobufSchema::decodeJson("runtime:hero", 1, wire.value());
    REQUIRE(json.ok());
    CHECK(json.value().find("Ada") != std::string::npos);
    CHECK(!eve::schema::ProtobufSchema::encodeJson("runtime:hero", 1, R"({"unknown":1})").ok());
    std::string futureWire = wire.value();
    futureWire.push_back('\x10');
    futureWire.push_back('\x01');
    CHECK(!eve::schema::ProtobufSchema::decodeJson("runtime:hero", 1, futureWire).ok());
    CHECK(!eve::schema::ProtobufSchema::decodeJson("runtime:hero", 2, wire.value()).ok());
    CHECK(!eve::schema::ProtobufSchema::registerDescriptorSet("runtime:hero", 1, "runtime.Hero", set).ok());
    CHECK(!eve::schema::ProtobufSchema::registerDescriptorSet("runtime:broken", 1, "runtime.Hero", "bad").ok());
    CHECK(!eve::schema::ProtobufSchema::contains("runtime:broken", 1));
    eve::schema::ProtobufSchema::clear();
    eve::schema::SchemaRegistry::clear();
}

TEST_CASE("schema.protobufAndLegacyDefinitionsCannotOwnSameVersion") {
    eve::schema::SchemaRegistry::clear();
    eve::schema::ProtobufSchema::clear();
    eve::schema::SchemaDefinition legacy;
    legacy.id      = "runtime:hero";
    legacy.version = 1;
    REQUIRE(eve::schema::SchemaRegistry::registerVersioned(legacy).ok());
    const auto set = descriptorSet();
    CHECK(!eve::schema::ProtobufSchema::registerDescriptorSet("runtime:hero", 1, "runtime.Hero", set).ok());
    REQUIRE(eve::schema::SchemaRegistry::remove("runtime:hero").ok());
    REQUIRE(eve::schema::ProtobufSchema::registerDescriptorSet("runtime:hero", 1, "runtime.Hero", set).ok());
    CHECK(!eve::schema::SchemaRegistry::registerVersioned(legacy).ok());
    eve::schema::ProtobufSchema::clear();
    eve::schema::SchemaRegistry::clear();
}

TEST_CASE("schema.protobufDescriptorAcceptsMatchingBuiltInImport") {
    eve::schema::SchemaRegistry::clear();
    eve::schema::ProtobufSchema::clear();
    const auto set = descriptorSetWithImport();
    REQUIRE(eve::schema::ProtobufSchema::registerDescriptorSet("runtime:imported", 1, "runtime.Imported", set).ok());
    CHECK(eve::schema::ProtobufSchema::contains("runtime:imported", 1));
    eve::schema::ProtobufSchema::clear();
    eve::schema::SchemaRegistry::clear();
}
