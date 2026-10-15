#include "schema/ProtobufSchema.h"

#include "schema/SchemaRegistry.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/descriptor.pb.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/json/json.h>
#include <google/protobuf/message.h>

#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace eve::schema {
namespace {

using Key = std::pair<std::string, int>;

struct Entry {
    google::protobuf::DescriptorPool        pool{google::protobuf::DescriptorPool::generated_pool()};
    google::protobuf::DynamicMessageFactory factory{&pool};
    const google::protobuf::Descriptor*     descriptor = nullptr;
};

std::map<Key, std::unique_ptr<Entry>>& entries() {
    static std::map<Key, std::unique_ptr<Entry>> value;
    return value;
}

Entry* find(std::string_view id, int version) {
    const auto it = entries().find(Key{std::string(id), version});
    return it == entries().end() ? nullptr : it->second.get();
}

Result<std::unique_ptr<google::protobuf::Message>> newMessage(std::string_view id, int version) {
    Entry* entry = find(id, version);
    if (!entry)
        return Result<std::unique_ptr<google::protobuf::Message>>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "protobuf schema version is not registered", std::string(id)));
    const auto* prototype = entry->factory.GetPrototype(entry->descriptor);
    if (!prototype)
        return Result<std::unique_ptr<google::protobuf::Message>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "protobuf message prototype is unavailable", std::string(id)));
    return Result<std::unique_ptr<google::protobuf::Message>>::success(
        std::unique_ptr<google::protobuf::Message>(prototype->New()));
}

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (reflection->GetUnknownFields(message).field_count() != 0) return true;
    std::vector<const google::protobuf::FieldDescriptor*> fields;
    reflection->ListFields(message, &fields);
    for (const auto* field : fields) {
        if (field->cpp_type() != google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE) continue;
        if (field->is_repeated()) {
            for (int index = 0; index < reflection->FieldSize(message, field); ++index)
                if (hasUnknownFields(reflection->GetRepeatedMessage(message, field, index))) return true;
        } else if (hasUnknownFields(reflection->GetMessage(message, field))) {
            return true;
        }
    }
    return false;
}

}  // namespace

Result<void> ProtobufSchema::registerDescriptorSet(std::string id, int version, std::string messageName,
                                                   std::string_view descriptorSet) {
    if (id.empty() || version <= 0 || messageName.empty() || descriptorSet.empty() ||
        descriptorSet.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "invalid protobuf schema registration", id));
    const Key key{id, version};
    if (entries().contains(key) || SchemaRegistry::resolve(id, version))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "schema version is already registered", id));

    google::protobuf::FileDescriptorSet files;
    if (!files.ParseFromArray(descriptorSet.data(), static_cast<int>(descriptorSet.size())) || files.file().empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::ParseError, "invalid FileDescriptorSet", id));
    if (hasUnknownFields(files))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "descriptor set contains unknown fields", id));
    auto                                                         entry = std::make_unique<Entry>();
    std::map<std::string, google::protobuf::FileDescriptorProto> pending;
    std::set<std::string>                                        declaredFiles;
    for (const auto& file : files.file()) {
        if (const auto* inherited = entry->pool.FindFileByName(file.name())) {
            google::protobuf::FileDescriptorProto known;
            inherited->CopyTo(&known);
            auto supplied = file;
            known.clear_source_code_info();
            supplied.clear_source_code_info();
            if (known.SerializeAsString() != supplied.SerializeAsString())
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::Conflict, "descriptor conflicts with a built-in file", id));
            continue;
        }
        if (file.name().empty() || !pending.emplace(file.name(), file).second)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "duplicate or unnamed descriptor file", id));
        declaredFiles.insert(file.name());
    }
    while (!pending.empty()) {
        bool advanced = false;
        for (auto it = pending.begin(); it != pending.end();) {
            bool ready = true;
            for (const auto& dependency : it->second.dependency()) {
                if (pending.contains(dependency)) {
                    ready = false;
                    break;
                }
                if (!entry->pool.FindFileByName(dependency))
                    return Result<void>::failure(Diagnostic::error(
                        DiagnosticCode::InvalidArgument, "descriptor dependency is unavailable: " + dependency, id));
            }
            if (!ready) {
                ++it;
                continue;
            }
            if (!entry->pool.BuildFile(it->second))
                return Result<void>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "protobuf descriptor build failed", id));
            it       = pending.erase(it);
            advanced = true;
        }
        if (!advanced)
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "descriptor dependency cycle", id));
    }
    entry->descriptor = entry->pool.FindMessageTypeByName(messageName);
    if (!entry->descriptor || !declaredFiles.contains(std::string(entry->descriptor->file()->name())))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "protobuf message type is absent", messageName));
    entries().emplace(std::move(key), std::move(entry));
    return Result<void>::success();
}

Result<std::string> ProtobufSchema::encodeJson(std::string_view id, int version, std::string_view json) {
    auto message = newMessage(id, version);
    if (!message.ok()) return Result<std::string>::failure(message.status());
    google::protobuf::json::ParseOptions options;
    options.allow_legacy_nonconformant_behavior = false;
    const auto status = google::protobuf::json::JsonStringToMessage(json, message.value().get(), options);
    if (!status.ok())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, std::string(status.message()), std::string(id)));
    std::string wire;
    if (!message.value()->SerializeToString(&wire))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "protobuf serialization failed", std::string(id)));
    return Result<std::string>::success(std::move(wire));
}

Result<std::string> ProtobufSchema::decodeJson(std::string_view id, int version, std::string_view wire) {
    auto message = newMessage(id, version);
    if (!message.ok()) return Result<std::string>::failure(message.status());
    if (wire.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        !message.value()->ParseFromArray(wire.data(), static_cast<int>(wire.size())))
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "invalid protobuf wire payload", std::string(id)));
    if (hasUnknownFields(*message.value()))
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::UnknownVersion, "protobuf wire payload contains unknown fields", std::string(id)));
    std::string                          json;
    google::protobuf::json::PrintOptions options;
    options.allow_legacy_nonconformant_behavior = false;
    const auto status = google::protobuf::json::MessageToJsonString(*message.value(), &json, options);
    if (!status.ok())
        return Result<std::string>::failure(
            Diagnostic::error(DiagnosticCode::Failed, std::string(status.message()), std::string(id)));
    return Result<std::string>::success(std::move(json));
}

bool ProtobufSchema::contains(std::string_view id, int version) { return find(id, version) != nullptr; }

void ProtobufSchema::clear() { entries().clear(); }

}  // namespace eve::schema
