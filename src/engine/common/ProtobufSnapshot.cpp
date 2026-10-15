#include "common/ProtobufSnapshot.h"

#include "snapshot_envelope.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>

#include <limits>
#include <utility>

namespace eve::proto_support {
namespace {

bool serializeDeterministic(const google::protobuf::MessageLite& message, std::string& bytes) {
    google::protobuf::io::StringOutputStream output(&bytes);
    google::protobuf::io::CodedOutputStream  coded(&output);
    coded.SetSerializationDeterministic(true);
    return message.SerializeToCodedStream(&coded) && !coded.HadError();
}

}  // namespace

Result<std::string> serializeBinarySnapshot(const BinarySnapshotData& data, const SnapshotHashProvider& hashProvider) {
    if (data.type.empty() || !data.schema.isValid() || data.schemaVersion.value() == 0 ||
        data.schemaVersion.value() > std::numeric_limits<std::uint32_t>::max() || data.payloadWire.empty())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid typed snapshot header or payload"));
    if (!hashProvider)
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "typed snapshot requires a hash provider"));

    persistence::proto::BinarySnapshotEnvelope envelope;
    envelope.set_type(data.type);
    envelope.set_schema(data.schema.format());
    envelope.set_version(static_cast<std::uint32_t>(data.schemaVersion.value()));
    envelope.set_instance_id(data.instanceId.format());
    envelope.set_revision(data.revision.value());
    envelope.set_tick(data.tick.value());
    envelope.set_payload_wire(data.payloadWire);
    std::string hashInput;
    if (!serializeDeterministic(envelope, hashInput))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "typed snapshot envelope serialization failed"));
    auto digest = hashProvider(hashInput);
    if (!digest.ok()) return Result<std::string>::failure(digest.status());
    const auto digestBytes = digest.value().bytes();
    envelope.set_digest(std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()));
    std::string wire;
    if (!serializeDeterministic(envelope, wire))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "typed snapshot envelope serialization failed"));
    return Result<std::string>::success(std::move(wire));
}

Result<BinarySnapshotData> parseBinarySnapshot(std::string_view bytes, const SnapshotHashProvider& hashProvider) {
    if (!hashProvider)
        return Result<BinarySnapshotData>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "typed snapshot requires a hash provider"));
    persistence::proto::BinarySnapshotEnvelope envelope;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !envelope.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) ||
        !envelope.GetReflection()->GetUnknownFields(envelope).empty() || !envelope.has_type() ||
        envelope.type().empty() || !envelope.has_schema() || !envelope.has_version() || envelope.version() == 0 ||
        !envelope.has_instance_id() || !envelope.has_revision() || !envelope.has_tick() ||
        !envelope.has_payload_wire() || envelope.payload_wire().empty() || !envelope.has_digest() ||
        envelope.digest().size() != ContentId::Bytes{}.size())
        return Result<BinarySnapshotData>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "invalid typed Protobuf snapshot envelope"));
    const auto schema     = LogicalId::parse(envelope.schema());
    const auto instanceId = PersistentId::parse(envelope.instance_id());
    if (!schema || !instanceId)
        return Result<BinarySnapshotData>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "invalid typed snapshot identity fields"));

    const std::string expectedDigest = envelope.digest();
    envelope.clear_digest();
    std::string hashInput;
    if (!serializeDeterministic(envelope, hashInput))
        return Result<BinarySnapshotData>::failure(
            Diagnostic::error(DiagnosticCode::SerializationError, "typed snapshot envelope serialization failed"));
    auto digest = hashProvider(hashInput);
    if (!digest.ok()) return Result<BinarySnapshotData>::failure(digest.status());
    const auto digestBytes = digest.value().bytes();
    if (expectedDigest != std::string(reinterpret_cast<const char*>(digestBytes.data()), digestBytes.size()))
        return Result<BinarySnapshotData>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "typed snapshot digest differs from payload"));

    return Result<BinarySnapshotData>::success(
        BinarySnapshotData{envelope.type(), *schema, SchemaVersion(envelope.version()), *instanceId,
                           Revision(envelope.revision()), SimulationTick(envelope.tick()), envelope.payload_wire()});
}

}  // namespace eve::proto_support
