#include "orders/CommandQueue.h"

#include "common/ProtobufDynamicValue.h"
#include "orders_command_queue.pb.h"

#include <google/protobuf/util/json_util.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace eve::orders {
namespace {

constexpr std::string_view queueSchema  = "orders.command_queue";
constexpr std::uint32_t    queueVersion = 2;
constexpr std::size_t      noActive     = static_cast<std::size_t>(-1);

eve::Result<void> invalid(std::string message, std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message),
                                                             std::move(path), {}, "orders.persistence"));
}

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

bool validState(proto::CommandOrderState state) { return state >= proto::QUEUED && state <= proto::CANCELLED; }

}  // namespace

eve::Result<std::string> CommandQueue::snapshotBinary() const {
    proto::CommandQueueSave save;
    save.set_schema("orders.command_queue");
    save.set_version(queueVersion);
    save.set_next_id(nextId_);
    save.set_next_sequence(nextSequence_);
    if (active_ != noActive) {
        if (active_ >= orders_.size())
            return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                            "active order index is out of range",
                                                                            "active", {}, "orders.persistence"));
        save.set_active_index(active_);
    }
    for (const auto index : queued_) {
        if (index >= orders_.size())
            return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                                                            "queued order index is out of range",
                                                                            "queued", {}, "orders.persistence"));
        save.add_queued_indices(index);
    }
    for (const auto& order : orders_) {
        auto* record = save.add_orders();
        record->set_id(order.id);
        record->set_kind(order.kind);
        record->set_priority(order.priority);
        record->set_timeout_seconds(order.timeoutSeconds);
        record->set_elapsed_seconds(order.elapsedSeconds);
        record->set_state(static_cast<proto::CommandOrderState>(order.state));
        record->set_reason(order.reason);
        auto encoded =
            eve::proto_support::encodeDynamicValue(eve::Value(order.payload.values_), *record->mutable_payload());
        if (!encoded.ok()) return eve::Result<std::string>::failure(encoded.status());
    }
    for (const auto& event : events_) {
        auto* record = save.add_events();
        record->set_sequence(event.sequence);
        record->set_order_id(event.orderId);
        record->set_kind(event.kind);
        record->set_from(static_cast<proto::CommandOrderState>(event.from));
        record->set_to(static_cast<proto::CommandOrderState>(event.to));
        record->set_reason(event.reason);
    }
    std::string wire;
    if (!save.SerializeToString(&wire))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(eve::DiagnosticCode::SerializationError,
                                                                        "command queue Protobuf serialization failed",
                                                                        {}, {}, "orders.persistence"));
    return eve::Result<std::string>::success(std::move(wire));
}

eve::Result<void> CommandQueue::restoreBinary(std::string_view bytes) {
    proto::CommandQueueSave save;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        return invalid("command queue Protobuf bytes are malformed");
    if (hasUnknownFields(save)) return invalid("command queue Protobuf contains unknown fields");
    if (!save.has_schema() || save.schema() != queueSchema)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "command queue schema is invalid", "schema", {},
                                                                 "orders.persistence"));
    if (!save.has_version() || save.version() != queueVersion)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion,
                                                                 "command queue version is unsupported", "version", {},
                                                                 "orders.persistence"));
    if (!save.has_next_id() || save.next_id() == 0 || !save.has_next_sequence() || save.next_sequence() == 0)
        return invalid("command queue counters are invalid", "counters");

    CommandQueue candidate;
    candidate.nextId_       = save.next_id();
    candidate.nextSequence_ = save.next_sequence();
    std::set<std::string> ids;
    for (const auto& record : save.orders()) {
        if (!record.has_id() || !record.has_kind() || !record.has_priority() || !record.has_timeout_seconds() ||
            !record.has_elapsed_seconds() || !record.has_state() || !record.has_reason() || !record.has_payload() ||
            record.id().empty() || record.kind().empty() || !std::isfinite(record.timeout_seconds()) ||
            record.timeout_seconds() < 0.0 || !std::isfinite(record.elapsed_seconds()) ||
            record.elapsed_seconds() < 0.0 || !validState(record.state()) || !ids.insert(record.id()).second)
            return invalid("command queue order is invalid", "orders");
        std::uint64_t idNumber = 0;
        if (!generatedOrderId(record.id(), idNumber) || idNumber >= candidate.nextId_)
            return invalid("command queue order id is outside its counter", "orders.id");
        auto payload = eve::proto_support::decodeDynamicValue(record.payload());
        if (!payload.ok()) return eve::Result<void>::failure(payload.status());
        const auto* values = payload.value().getIf<eve::Value::Object>();
        if (!values) return invalid("command queue payload must be an object", "orders.payload");
        Order order;
        order.id              = record.id();
        order.kind            = record.kind();
        order.priority        = record.priority();
        order.timeoutSeconds  = record.timeout_seconds();
        order.elapsedSeconds  = record.elapsed_seconds();
        order.state           = static_cast<OrderState>(record.state());
        order.reason          = record.reason();
        order.payload.values_ = *values;
        candidate.orders_.push_back(std::move(order));
    }

    std::vector<bool> queuedMembership(candidate.orders_.size(), false);
    for (const auto encoded : save.queued_indices()) {
        if (encoded >= candidate.orders_.size()) return invalid("queued index is out of range", "queued");
        const auto index = static_cast<std::size_t>(encoded);
        if (queuedMembership[index] || candidate.orders_[index].state != OrderState::Queued)
            return invalid("queued membership is invalid", "queued");
        if (!candidate.queued_.empty()) {
            const auto previous = candidate.queued_.back();
            if (candidate.orders_[previous].priority < candidate.orders_[index].priority ||
                (candidate.orders_[previous].priority == candidate.orders_[index].priority && previous > index))
                return invalid("queued order is not deterministically sorted", "queued");
        }
        queuedMembership[index] = true;
        candidate.queued_.push_back(index);
    }
    if (save.has_active_index()) {
        if (save.active_index() >= candidate.orders_.size()) return invalid("active index is out of range", "active");
        candidate.active_ = static_cast<std::size_t>(save.active_index());
        if (candidate.orders_[candidate.active_].state != OrderState::Active)
            return invalid("active membership is invalid", "active");
    }
    for (std::size_t index = 0; index < candidate.orders_.size(); ++index) {
        const auto state = candidate.orders_[index].state;
        if ((state == OrderState::Queued) != queuedMembership[index] ||
            (state == OrderState::Active) != (candidate.active_ == index))
            return invalid("order state and queue membership disagree", "orders");
    }

    std::uint64_t previousSequence = 0;
    for (const auto& record : save.events()) {
        if (!record.has_sequence() || !record.has_order_id() || !record.has_kind() || !record.has_from() ||
            !record.has_to() || !record.has_reason() || record.sequence() == 0 ||
            record.sequence() <= previousSequence || record.sequence() >= candidate.nextSequence_ ||
            record.kind().empty() || !ids.contains(record.order_id()) || !validState(record.from()) ||
            !validState(record.to()))
            return invalid("command queue event is invalid", "events");
        OrderEvent event;
        event.sequence   = record.sequence();
        event.orderId    = record.order_id();
        event.kind       = record.kind();
        event.from       = static_cast<OrderState>(record.from());
        event.to         = static_cast<OrderState>(record.to());
        event.reason     = record.reason();
        previousSequence = event.sequence;
        candidate.events_.push_back(std::move(event));
    }
    *this = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> CommandQueue::snapshot() const {
    auto wire = snapshotBinary();
    if (!wire.ok()) return eve::Result<std::string>::failure(wire.status());
    proto::CommandQueueSave save;
    if (!save.ParseFromString(wire.value()))
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::SerializationError, "command queue Protobuf JSON conversion failed"));
    std::string json;
    if (!google::protobuf::util::MessageToJsonString(save, &json).ok())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::SerializationError, "command queue Protobuf JSON conversion failed"));
    return eve::Result<std::string>::success(std::move(json));
}

eve::Result<void> CommandQueue::restore(std::string_view json) {
    proto::CommandQueueSave save;
    if (google::protobuf::util::JsonStringToMessage(std::string(json), &save).ok() && save.has_version() &&
        save.version() == queueVersion) {
        std::string wire;
        if (!save.SerializeToString(&wire)) return invalid("command queue Protobuf JSON conversion failed");
        return restoreBinary(wire);
    }
    return restoreLegacyJson(json);
}

}  // namespace eve::orders
