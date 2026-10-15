#include "sensing/Sensing.h"

#include "common/Diagnostic.h"
#include "common/Identity.h"
#include "sensing_world.pb.h"

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
#include <google/protobuf/util/json_util.h>

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace eve::sensing {
namespace {

bool hasUnknownFields(const google::protobuf::Message& message) {
    const auto* reflection = message.GetReflection();
    if (!reflection->GetUnknownFields(message).empty()) return true;
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

eve::Result<void> invalid(std::string message, std::string path = {}) {
    return eve::Result<void>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message), std::move(path), {}, "sensing"));
}

eve::Result<void> readSet(const google::protobuf::RepeatedPtrField<std::string>& source,
                          std::set<std::string>& destination, bool logicalIds, std::string_view path) {
    for (const auto& item : source) {
        if (item.empty() || (logicalIds && !eve::LogicalId::parse(item)) || !destination.insert(item).second)
            return invalid("invalid or duplicate subject set entry", std::string(path));
    }
    return eve::Result<void>::success();
}

}  // namespace

void SensingWorld::captureMessage(proto::WorldSave& save) const {
    save.set_schema("eve.sensing.world");
    save.set_version(2);
    for (const auto& [id, subject] : subjects_) {
        auto* record = save.add_subjects();
        record->set_id(id);
        record->set_x(subject.x);
        record->set_y(subject.y);
        record->set_faction(subject.faction);
        for (const auto& tag : subject.tags) record->add_tags(tag);
        for (const auto& scope : subject.visibleTo) record->add_visible_to(scope);
        for (const auto& zone : subject.zones) record->add_zones(zone);
    }
}

eve::Result<void> SensingWorld::restoreMessage(const proto::WorldSave& save) {
    if (hasUnknownFields(save) || !save.has_schema() || save.schema() != "eve.sensing.world" || !save.has_version() ||
        save.version() != 2)
        return invalid("invalid sensing world snapshot header");
    std::map<std::string, Subject> next;
    for (const auto& record : save.subjects()) {
        if (!record.has_id() || record.id().empty() || !record.has_x() || !record.has_y() ||
            !std::isfinite(record.x()) || !std::isfinite(record.y()) || !record.has_faction())
            return invalid("invalid subject", "subjects");
        Subject subject;
        subject.id      = record.id();
        subject.x       = record.x();
        subject.y       = record.y();
        subject.faction = record.faction();
        if (auto result = readSet(record.tags(), subject.tags, false, "subjects.tags"); !result) return result;
        if (auto result = readSet(record.visible_to(), subject.visibleTo, false, "subjects.visibleTo"); !result)
            return result;
        if (auto result = readSet(record.zones(), subject.zones, true, "subjects.zones"); !result) return result;
        if (!next.emplace(subject.id, std::move(subject)).second) return invalid("duplicate subject id", "subjects.id");
    }
    subjects_ = std::move(next);
    results_.clear();
    lastQuery_ = {};
    if (spatialIndex_) rebuildSpatialIndex();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> SensingWorld::snapshotBinary() const {
    proto::WorldSave save;
    captureMessage(save);
    std::string bytes;
    bool        encoded = false;
    {
        google::protobuf::io::StringOutputStream output(&bytes);
        google::protobuf::io::CodedOutputStream  coded(&output);
        coded.SetSerializationDeterministic(true);
        encoded = save.SerializeToCodedStream(&coded) && !coded.HadError();
    }
    if (!encoded)
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "sensing Protobuf encoding failed"));
    return eve::Result<std::string>::success(std::move(bytes));
}

eve::Result<void> SensingWorld::restoreBinary(std::string_view bytes) {
    proto::WorldSave save;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return invalid("invalid sensing Protobuf bytes");
    return restoreMessage(save);
}

std::string SensingWorld::snapshotJson() const {
    proto::WorldSave save;
    captureMessage(save);
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(save, &json).ok()) return {};
    return json;
}

}  // namespace eve::sensing
