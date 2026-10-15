#include "production/Production.h"

#include "common/ProtobufDynamicValue.h"
#include "common/ProtobufSnapshot.h"
#include "dynamic_value.pb.h"
#include "production_queue.pb.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <limits>
#include <set>
#include <utility>

namespace eve::production {
namespace {

eve::Result<void> invalid(std::string message, std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::ParseError, std::move(message),
                                                             std::move(path), {}, "production.persistence"));
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

eve::LogicalId productionProtoSchema() {
    const auto schema = eve::LogicalId::parse("production:queue");
    if (!schema) std::terminate();
    return *schema;
}

template <typename Range>
bool uniqueNonempty(const Range& values) {
    std::set<std::string> seen;
    for (const auto& value : values)
        if (value.empty() || !seen.insert(value).second) return false;
    return true;
}

}  // namespace

eve::Result<std::string> WorkQueue::snapshotBinary() const {
    proto::ProductionQueueSave save;
    save.set_schema("production:queue");
    save.set_version(8);
    save.set_revision(revision_.value());
    save.set_tick(tick_.value());
    save.set_next_task_id(nextTaskId_);
    save.set_next_enqueue_sequence(nextEnqueueSequence_);
    save.set_next_event_sequence(nextEventSequence_);
    for (const auto& [owner, count] : slots_) {
        auto* slot = save.add_slots();
        slot->set_owner(owner);
        slot->set_count(count);
    }
    for (const auto& [key, capacity] : resources_) {
        auto* resource = save.add_resources();
        resource->set_owner(key.first);
        resource->set_resource(key.second);
        resource->set_capacity(capacity);
    }
    for (const auto& [owner, definition, generation] : availableDefinitions_) {
        auto* entry = save.add_available_definitions();
        entry->set_owner(owner);
        entry->set_definition(definition);
        entry->set_generation(generation);
    }
    for (const auto& [owner, tag] : availableTags_) {
        auto* entry = save.add_available_tags();
        entry->set_owner(owner);
        entry->set_tag(tag);
    }
    for (const auto& [owner, strategy] : schedulerStrategies_) {
        auto* entry = save.add_schedulers();
        entry->set_owner(owner);
        entry->set_strategy(static_cast<proto::SchedulerStrategy>(strategy));
    }
    for (const auto& owned : tasks_) {
        const auto& task   = *owned;
        auto*       record = save.add_tasks();
        record->set_id(task.id);
        record->set_owner(task.owner);
        record->set_kind(task.kind);
        record->set_product(task.product);
        auto context = eve::proto_support::encodeDynamicValue(task.context, *record->mutable_context());
        if (!context.ok()) return eve::Result<std::string>::failure(context.status());
        record->set_duration_ns(task.duration.nanoseconds());
        record->set_progress_ns(task.progress.nanoseconds());
        record->set_state(static_cast<proto::TaskState>(task.state));
        record->set_enqueue_sequence(task.enqueueSequence);
        record->set_priority(task.priority);
        record->set_reason(task.reason);
        record->set_definition(task.definition.isValid() ? task.definition.reference.id().format() : "");
        record->set_definition_generation(task.definition.generation.value());
        auto reservation = eve::proto_support::encodeDynamicValue(task.reservation, *record->mutable_reservation());
        if (!reservation.ok()) return eve::Result<std::string>::failure(reservation.status());
        record->set_reservation_state(static_cast<proto::ReservationState>(task.reservationState));
        record->set_reservation_release_id(task.reservationRelease.releaseId);
        auto release = eve::proto_support::encodeDynamicValue(task.reservationRelease.reservation,
                                                              *record->mutable_reservation_release_payload());
        if (!release.ok()) return eve::Result<std::string>::failure(release.status());
        record->set_reservation_release_refund_permille(task.reservationRelease.refundPermille);
        record->set_settlement_id(task.settlement.settlementId);
        auto settlement =
            eve::proto_support::encodeDynamicValue(task.settlement.payload, *record->mutable_settlement_payload());
        if (!settlement.ok()) return eve::Result<std::string>::failure(settlement.status());
        record->set_last_settlement_id(task.lastSettlementId);
        record->set_dependency_mode(static_cast<proto::TaskDependencyMode>(task.dependencies.mode));
        for (const auto& id : task.dependencies.prerequisites) record->add_prerequisites(id);
        for (const auto& id : task.dependencies.blocks) record->add_blocks(id);
        for (const auto& handle : task.dependencies.requiredDefinitions) {
            auto* required = record->add_required_definitions();
            required->set_definition(handle.reference.id().format());
            required->set_generation(handle.generation.value());
        }
        for (const auto& tag : task.dependencies.requiredTags) record->add_required_tags(tag);
        for (const auto& requirement : task.resources) {
            auto* required = record->add_requirements();
            required->set_resource(requirement.resource);
            required->set_units(requirement.units);
        }
        record->set_refund_cancellation(static_cast<proto::RefundPolicy>(task.termination.cancellation));
        record->set_refund_failure(static_cast<proto::RefundPolicy>(task.termination.failure));
        record->set_random_stream(task.random.stream);
        record->set_random_seed(task.random.seed);
        record->set_random_draw_start(task.random.drawStart);
        record->set_random_draw_count(task.random.drawCount);
        record->set_total_cycles(task.repeat.totalCycles);
        record->set_continuous(task.repeat.continuous);
        record->set_maintain_stock_target(task.repeat.maintainStockTarget);
        record->set_observed_stock(task.repeat.observedStock);
        record->set_correlation_id(task.correlationId);
        record->set_completed_cycles(task.completedCycles);
        record->set_batch_size(task.batchSize);
        record->set_efficiency_permille(task.efficiencyPermille);
        record->set_work_remainder_permille(task.workRemainderPermille);
        record->set_refund_permille(task.refundPermille);
        record->set_settlement_required(task.settlementRequired);
    }
    for (const auto& event : events_) {
        auto* record = save.add_events();
        record->set_sequence(event.sequence);
        record->set_tick(event.tick.value());
        record->set_kind(static_cast<proto::ProductionEventKind>(event.kind));
        record->set_task_id(event.taskId);
        record->set_owner(event.owner);
        record->set_task_kind(event.taskKind);
        record->set_product(event.product);
        record->set_correlation_id(event.correlationId);
        record->set_reason(event.reason);
    }
    std::string wire;
    if (!save.SerializeToString(&wire))
        return eve::Result<std::string>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::SerializationError, "production Protobuf encoding failed"));
    return eve::Result<std::string>::success(std::move(wire));
}

eve::Result<void> WorkQueue::restoreBinary(std::string_view bytes) {
    proto::ProductionQueueSave save;
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        !save.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())) || hasUnknownFields(save) ||
        !save.has_schema() || save.schema() != "production:queue" || !save.has_version() || save.version() != 8 ||
        !save.has_revision() || !save.has_tick() || !save.has_next_task_id() || save.next_task_id() == 0 ||
        !save.has_next_enqueue_sequence() || save.next_enqueue_sequence() == 0 || !save.has_next_event_sequence() ||
        save.next_event_sequence() == 0)
        return invalid("invalid production Protobuf header");

    WorkQueue             candidate(instanceId_);
    std::set<std::string> slotOwners;
    for (const auto& slot : save.slots()) {
        if (!slot.has_owner() || slot.owner().empty() || !slot.has_count() || slot.count() < 0 ||
            !slotOwners.insert(slot.owner()).second)
            return invalid("invalid production slot", "slots");
        auto result = candidate.setSlotCount(slot.owner(), slot.count());
        if (!result.ok()) return result;
    }
    std::set<std::pair<std::string, std::string>> resourceNames;
    for (const auto& resource : save.resources()) {
        if (!resource.has_owner() || resource.owner().empty() || !resource.has_resource() ||
            resource.resource().empty() || resource.resource() == "slot" || !resource.has_capacity() ||
            resource.capacity() < 0 || !resourceNames.insert({resource.owner(), resource.resource()}).second)
            return invalid("invalid production capacity", "resources");
        auto result = candidate.setResourceCapacity(resource.owner(), resource.resource(), resource.capacity());
        if (!result.ok()) return result;
    }
    std::set<std::tuple<std::string, std::string, std::uint64_t>> availableDefinitions;
    for (const auto& entry : save.available_definitions()) {
        if (!entry.has_owner() || entry.owner().empty() || !entry.has_definition() || !entry.has_generation() ||
            entry.generation() == 0 ||
            !availableDefinitions.insert({entry.owner(), entry.definition(), entry.generation()}).second)
            return invalid("invalid available definition", "availableDefinitions");
        auto reference = eve::DefinitionRef::parse(entry.definition());
        if (!reference.ok()) return invalid("invalid available definition reference", "availableDefinitions");
        const eve::definition::DefinitionHandle handle{std::move(reference).takeValue(),
                                                       eve::Generation(entry.generation())};
        auto                                    result = candidate.setDefinitionAvailable(entry.owner(), handle, true);
        if (!result.ok()) return result;
    }
    std::set<std::pair<std::string, std::string>> availableTags;
    for (const auto& entry : save.available_tags()) {
        if (!entry.has_owner() || entry.owner().empty() || !entry.has_tag() || entry.tag().empty() ||
            !availableTags.insert({entry.owner(), entry.tag()}).second)
            return invalid("invalid available tag", "availableTags");
        auto result = candidate.setTagAvailable(entry.owner(), entry.tag(), true);
        if (!result.ok()) return result;
    }
    std::set<std::string> schedulerOwners;
    for (const auto& scheduler : save.schedulers()) {
        if (!scheduler.has_owner() || scheduler.owner().empty() || !scheduler.has_strategy() ||
            !proto::SchedulerStrategy_IsValid(scheduler.strategy()) ||
            !schedulerOwners.insert(scheduler.owner()).second)
            return invalid("invalid production scheduler", "schedulers");
        auto result =
            candidate.setSchedulerStrategy(scheduler.owner(), static_cast<SchedulerStrategy>(scheduler.strategy()));
        if (!result.ok()) return result;
    }

    std::set<std::string> ids;
    std::uint64_t         largestEnqueueSequence = 0;
    for (const auto& record : save.tasks()) {
        if (!record.has_id() || record.id().empty() || !ids.insert(record.id()).second || !record.has_owner() ||
            record.owner().empty() || !record.has_kind() || record.kind().empty() || !record.has_product() ||
            record.product().empty() || !record.has_context() || !record.has_duration_ns() ||
            record.duration_ns() <= 0 || !record.has_progress_ns() || record.progress_ns() < 0 ||
            record.progress_ns() > record.duration_ns() || !record.has_state() ||
            !proto::TaskState_IsValid(record.state()) || !record.has_enqueue_sequence() ||
            record.enqueue_sequence() == 0 || !record.has_priority() || !record.has_reason() ||
            !record.has_definition() || !record.has_definition_generation() || !record.has_reservation() ||
            !record.has_reservation_state() || !proto::ReservationState_IsValid(record.reservation_state()) ||
            !record.has_reservation_release_id() || !record.has_reservation_release_payload() ||
            !record.has_reservation_release_refund_permille() || record.reservation_release_refund_permille() > 1000 ||
            !record.has_settlement_id() || !record.has_settlement_payload() || !record.has_last_settlement_id() ||
            !record.has_dependency_mode() || !proto::TaskDependencyMode_IsValid(record.dependency_mode()) ||
            !record.has_refund_cancellation() || !proto::RefundPolicy_IsValid(record.refund_cancellation()) ||
            !record.has_refund_failure() || !proto::RefundPolicy_IsValid(record.refund_failure()) ||
            !record.has_random_stream() || !record.has_random_seed() || !record.has_random_draw_start() ||
            !record.has_random_draw_count() || !record.has_total_cycles() || record.total_cycles() == 0 ||
            !record.has_continuous() || !record.has_maintain_stock_target() || record.maintain_stock_target() < -1 ||
            !record.has_observed_stock() || record.observed_stock() < 0 || !record.has_correlation_id() ||
            !record.has_completed_cycles() || !record.has_batch_size() || record.batch_size() == 0 ||
            !record.has_efficiency_permille() || record.efficiency_permille() == 0 ||
            !record.has_work_remainder_permille() || record.work_remainder_permille() >= 1000 ||
            !record.has_refund_permille() || record.refund_permille() > 1000 || !record.has_settlement_required() ||
            (record.random_draw_count() > 0 && record.random_stream().empty()) ||
            (record.state() == proto::COMPLETED && record.settlement_id().empty()) ||
            (record.reservation_state() == proto::RELEASED) != !record.reservation_release_id().empty() ||
            (record.definition().empty() != (record.definition_generation() == 0)) ||
            !uniqueNonempty(record.prerequisites()) || !uniqueNonempty(record.blocks()) ||
            !uniqueNonempty(record.required_tags()))
            return invalid("invalid production task", "tasks");

        auto context     = eve::proto_support::decodeDynamicValue(record.context());
        auto reservation = eve::proto_support::decodeDynamicValue(record.reservation());
        auto release     = eve::proto_support::decodeDynamicValue(record.reservation_release_payload());
        auto settlement  = eve::proto_support::decodeDynamicValue(record.settlement_payload());
        if (!context.ok() || !reservation.ok() || !release.ok() || !settlement.ok() || !context.value().isObject() ||
            !reservation.value().isObject() || !release.value().isObject() || !settlement.value().isObject())
            return invalid("invalid production dynamic task payload", "tasks");

        auto task             = std::make_unique<ProductionTask>();
        task->id              = record.id();
        task->owner           = record.owner();
        task->kind            = record.kind();
        task->product         = record.product();
        task->context         = std::move(context).takeValue();
        task->duration        = eve::Duration::fromNanoseconds(record.duration_ns());
        task->progress        = eve::Duration::fromNanoseconds(record.progress_ns());
        task->state           = static_cast<TaskState>(record.state());
        task->enqueueSequence = record.enqueue_sequence();
        task->priority        = record.priority();
        task->reason          = record.reason();
        if (!record.definition().empty()) {
            auto reference = eve::DefinitionRef::parse(record.definition());
            if (!reference.ok()) return invalid("invalid pinned production definition", "tasks.definition");
            task->definition = {std::move(reference).takeValue(), eve::Generation(record.definition_generation())};
        }
        task->reservation        = std::move(reservation).takeValue();
        task->reservationState   = static_cast<ReservationState>(record.reservation_state());
        task->reservationRelease = {record.reservation_release_id(), std::move(release).takeValue(),
                                    record.reservation_release_refund_permille()};
        task->settlement         = {record.settlement_id(), std::move(settlement).takeValue()};
        task->lastSettlementId   = record.last_settlement_id();
        task->dependencies.mode  = static_cast<TaskDependencyMode>(record.dependency_mode());
        task->dependencies.prerequisites.assign(record.prerequisites().begin(), record.prerequisites().end());
        task->dependencies.blocks.assign(record.blocks().begin(), record.blocks().end());
        task->dependencies.requiredTags.assign(record.required_tags().begin(), record.required_tags().end());
        std::set<std::pair<std::string, std::uint64_t>> requiredDefinitions;
        for (const auto& requirement : record.required_definitions()) {
            if (!requirement.has_definition() || !requirement.has_generation() || requirement.generation() == 0 ||
                !requiredDefinitions.insert({requirement.definition(), requirement.generation()}).second)
                return invalid("invalid required production definition", "tasks.requiredDefinitions");
            auto reference = eve::DefinitionRef::parse(requirement.definition());
            if (!reference.ok()) return invalid("invalid required definition reference", "tasks.requiredDefinitions");
            task->dependencies.requiredDefinitions.push_back(
                {std::move(reference).takeValue(), eve::Generation(requirement.generation())});
        }
        std::set<std::string> requirementNames;
        for (const auto& requirement : record.requirements()) {
            if (!requirement.has_resource() || requirement.resource().empty() || !requirement.has_units() ||
                requirement.units() <= 0 || !requirementNames.insert(requirement.resource()).second)
                return invalid("invalid production resource requirement", "tasks.requirements");
            task->resources.push_back({requirement.resource(), requirement.units()});
        }
        task->termination           = {static_cast<RefundPolicy>(record.refund_cancellation()),
                                       static_cast<RefundPolicy>(record.refund_failure())};
        task->random                = {record.random_stream(), record.random_seed(), record.random_draw_start(),
                                       record.random_draw_count()};
        task->repeat                = {record.total_cycles(), record.continuous(), record.maintain_stock_target(),
                                       record.observed_stock()};
        task->correlationId         = record.correlation_id();
        task->completedCycles       = record.completed_cycles();
        task->batchSize             = record.batch_size();
        task->efficiencyPermille    = record.efficiency_permille();
        task->workRemainderPermille = record.work_remainder_permille();
        task->refundPermille        = record.refund_permille();
        task->settlementRequired    = record.settlement_required();
        largestEnqueueSequence      = std::max(largestEnqueueSequence, task->enqueueSequence);
        candidate.tasks_.push_back(std::move(task));
    }
    if (largestEnqueueSequence >= save.next_enqueue_sequence())
        return invalid("production enqueue counter does not follow retained tasks", "nextEnqueueSequence");

    for (const auto& task : candidate.tasks_) {
        for (const auto& id : task->dependencies.prerequisites)
            if (id == task->id || !ids.contains(id))
                return invalid("invalid production prerequisite reference", "tasks.prerequisites");
        for (const auto& id : task->dependencies.blocks)
            if (id == task->id || !ids.contains(id))
                return invalid("invalid production blocker reference", "tasks.blocks");
    }
    std::set<std::string>                      visiting;
    std::set<std::string>                      visited;
    std::function<bool(const ProductionTask&)> acyclic = [&](const ProductionTask& task) {
        if (visited.contains(task.id)) return true;
        if (!visiting.insert(task.id).second) return false;
        for (const auto& id : task.dependencies.prerequisites) {
            const auto dependency = candidate.find(id);
            if (!dependency || !acyclic(dependency->get())) return false;
        }
        visiting.erase(task.id);
        visited.insert(task.id);
        return true;
    };
    for (const auto& task : candidate.tasks_)
        if (!acyclic(*task)) return invalid("production dependency graph contains a cycle", "tasks.prerequisites");

    std::uint64_t previousEventSequence = 0;
    for (const auto& record : save.events()) {
        if (!record.has_sequence() || record.sequence() <= previousEventSequence ||
            record.sequence() >= save.next_event_sequence() || !record.has_tick() || !record.has_kind() ||
            !proto::ProductionEventKind_IsValid(record.kind()) || !record.has_task_id() || !record.has_owner() ||
            !record.has_task_kind() || !record.has_product() || !record.has_correlation_id() || !record.has_reason())
            return invalid("invalid production event", "events");
        ProductionEvent event;
        event.sequence        = record.sequence();
        event.tick            = eve::SimulationTick(record.tick());
        event.kind            = static_cast<ProductionEventKind>(record.kind());
        event.taskId          = record.task_id();
        event.owner           = record.owner();
        event.taskKind        = record.task_kind();
        event.product         = record.product();
        event.correlationId   = record.correlation_id();
        event.reason          = record.reason();
        previousEventSequence = event.sequence;
        candidate.events_.push_back(std::move(event));
    }

    candidate.nextTaskId_          = save.next_task_id();
    candidate.nextEnqueueSequence_ = save.next_enqueue_sequence();
    candidate.nextEventSequence_   = save.next_event_sequence();
    candidate.revision_            = eve::Revision(save.revision());
    candidate.tick_                = eve::SimulationTick(save.tick());
    *this                          = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> WorkQueue::snapshotEnvelopeBinary(const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = snapshotBinary();
    if (!payload.ok()) return eve::Result<std::string>::failure(payload.status());
    return eve::proto_support::serializeBinarySnapshot(
        {"production.queue", productionProtoSchema(), eve::SchemaVersion(8), instanceId_, revision_, tick_,
         std::move(payload).takeValue()},
        hashProvider);
}

eve::Result<void> WorkQueue::restoreSnapshotBinary(std::string_view                 bytes,
                                                   const eve::SnapshotHashProvider& hashProvider) {
    auto parsed = eve::proto_support::parseBinarySnapshot(bytes, hashProvider);
    if (!parsed.ok()) return eve::Result<void>::failure(parsed.status());
    auto envelope = std::move(parsed).takeValue();
    if (envelope.type != "production.queue" || envelope.schema != productionProtoSchema() ||
        envelope.schemaVersion != eve::SchemaVersion(8))
        return invalid("snapshot does not belong to production::WorkQueue");
    if (!instanceId_.isNil() && envelope.instanceId != instanceId_)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "snapshot instanceId does not match production::WorkQueue"));
    WorkQueue candidate(instanceId_);
    auto      restored = candidate.restoreBinary(envelope.payloadWire);
    if (!restored.ok()) return restored;
    if (candidate.revision_ != envelope.revision || candidate.tick_ != envelope.tick)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "production payload metadata disagrees with snapshot envelope"));
    candidate.instanceId_ = envelope.instanceId;
    *this                 = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::production
