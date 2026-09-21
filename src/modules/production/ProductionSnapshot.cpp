#include "production/Production.h"

#include <exception>
#include <utility>

namespace eve::production {
namespace {

eve::LogicalId productionSchema() {
    const auto schema = eve::LogicalId::parse("production:queue");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& productionMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto                  registration =
            result.add(productionSchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           if (!migrated.contains("version")) migrated.emplace("version", eve::Value(std::int64_t(1)));
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!registration.ok()) std::terminate();
        const auto settlementRegistration =
            result.add(productionSchema(), eve::SchemaVersion(1), eve::SchemaVersion(2),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(2));
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (!task) continue;
                                       task->try_emplace("definition", eve::Value(std::string{}));
                                       task->try_emplace("definitionGeneration", eve::Value(std::string("0")));
                                       task->try_emplace("reservation", eve::Value(eve::Value::Object{}));
                                       const auto state = task->find("state");
                                       const bool completed = state != task->end() &&
                                           state->second.getIf<std::string>() != nullptr &&
                                           *state->second.getIf<std::string>() == "completed";
                                       const auto id = task->find("id");
                                       const auto* taskId = id == task->end() ? nullptr : id->second.getIf<std::string>();
                                       task->try_emplace("settlementId", eve::Value(
                                           completed && taskId ? "legacy:" + *taskId : std::string{}));
                                       task->try_emplace("settlementPayload", eve::Value(eve::Value::Object{}));
                                       task->try_emplace("settlementRequired", eve::Value(false));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!settlementRegistration.ok()) std::terminate();
        const auto dependencyRegistration =
            result.add(productionSchema(), eve::SchemaVersion(2), eve::SchemaVersion(3),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(3));
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (!task) continue;
                                       task->try_emplace("dependencyMode", eve::Value("all_of"));
                                       task->try_emplace("prerequisites", eve::Value(eve::Value::Array{}));
                                       task->try_emplace("blocks", eve::Value(eve::Value::Array{}));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!dependencyRegistration.ok()) std::terminate();
        const auto executionRegistration =
            result.add(productionSchema(), eve::SchemaVersion(3), eve::SchemaVersion(4),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(4));
                           migrated.try_emplace("resources", eve::Value(eve::Value::Array{}));
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (!task) continue;
                                       task->try_emplace("batchSize", eve::Value(std::int64_t(1)));
                                       task->try_emplace("efficiencyPermille", eve::Value(std::int64_t(1000)));
                                       task->try_emplace("refundCancellation", eve::Value("full"));
                                       task->try_emplace("refundFailure", eve::Value("full"));
                                       task->try_emplace("refundPermille", eve::Value(std::int64_t(0)));
                                       task->try_emplace("requirements", eve::Value(eve::Value::Array{}));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!executionRegistration.ok()) std::terminate();
        const auto advancedRegistration =
            result.add(productionSchema(), eve::SchemaVersion(4), eve::SchemaVersion(5),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(5));
                           migrated.try_emplace("schedulers", eve::Value(eve::Value::Array{}));
                           if (auto events = migrated.find("events"); events != migrated.end()) {
                               if (auto* array = events->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* event = item.getIf<eve::Value::Object>();
                                       if (!event) continue;
                                       const auto taskId = event->find("taskId");
                                       const auto* id = taskId == event->end() ? nullptr : taskId->second.getIf<std::string>();
                                       event->try_emplace("correlationId", eve::Value(id ? *id : std::string{}));
                                   }
                               }
                           }
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (!task) continue;
                                       const auto taskId = task->find("id");
                                       const auto* id = taskId == task->end() ? nullptr : taskId->second.getIf<std::string>();
                                       task->try_emplace("completedCycles", eve::Value(std::int64_t(0)));
                                       task->try_emplace("continuous", eve::Value(false));
                                       task->try_emplace("correlationId", eve::Value(id ? *id : std::string{}));
                                       task->try_emplace("lastSettlementId", eve::Value(std::string{}));
                                       task->try_emplace("maintainStockTarget", eve::Value(std::int64_t(-1)));
                                       task->try_emplace("observedStock", eve::Value(std::int64_t(0)));
                                       task->try_emplace("randomDrawCount", eve::Value(std::int64_t(0)));
                                       task->try_emplace("randomDrawStart", eve::Value(std::string("0")));
                                       task->try_emplace("randomSeed", eve::Value(std::string("0")));
                                       task->try_emplace("randomStream", eve::Value(std::string{}));
                                       task->try_emplace("totalCycles", eve::Value(std::int64_t(1)));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!advancedRegistration.ok()) std::terminate();
        const auto requirementRegistration =
            result.add(productionSchema(), eve::SchemaVersion(5), eve::SchemaVersion(6),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(6));
                           migrated.try_emplace("availableDefinitions", eve::Value(eve::Value::Array{}));
                           migrated.try_emplace("availableTags", eve::Value(eve::Value::Array{}));
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (!task) continue;
                                       task->try_emplace("requiredDefinitions", eve::Value(eve::Value::Array{}));
                                       task->try_emplace("requiredTags", eve::Value(eve::Value::Array{}));
                                       const auto reservation = task->find("reservation");
                                       const auto* reservationObject = reservation == task->end()
                                           ? nullptr : reservation->second.getIf<eve::Value::Object>();
                                       const bool reserved = reservationObject != nullptr && !reservationObject->empty();
                                       std::string reservationState = "none";
                                       if (reserved) {
                                           const auto state = task->find("state");
                                           const auto* stateName = state == task->end()
                                               ? nullptr : state->second.getIf<std::string>();
                                           reservationState = stateName != nullptr && *stateName == "queued"
                                               ? "reserved" : stateName != nullptr &&
                                                   (*stateName == "ready_to_settle" ||
                                                    *stateName == "settlement_failed" || *stateName == "completed")
                                               ? "consumed" : "started";
                                       }
                                       task->try_emplace("reservationReleaseId", eve::Value(std::string{}));
                                       task->try_emplace("reservationReleasePayload",
                                                         eve::Value(eve::Value::Object{}));
                                       task->try_emplace("reservationReleaseRefundPermille",
                                                         eve::Value(std::int64_t(0)));
                                       task->try_emplace("reservationState", eve::Value(reservationState));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!requirementRegistration.ok()) std::terminate();
        const auto remainderRegistration =
            result.add(productionSchema(), eve::SchemaVersion(6), eve::SchemaVersion(7),
                       [](const eve::Value& payload) -> eve::Result<eve::Value> {
                           const auto* object = payload.getIf<eve::Value::Object>();
                           if (!object)
                               return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                                   eve::DiagnosticCode::ParseError, "production queue payload must be an object"));
                           eve::Value::Object migrated = *object;
                           migrated["version"] = eve::Value(std::int64_t(7));
                           if (auto tasks = migrated.find("tasks"); tasks != migrated.end()) {
                               if (auto* array = tasks->second.getIf<eve::Value::Array>()) {
                                   for (auto& item : *array) {
                                       auto* task = item.getIf<eve::Value::Object>();
                                       if (task)
                                           task->try_emplace("workRemainderPermille", eve::Value(std::int64_t(0)));
                                   }
                               }
                           }
                           return eve::Result<eve::Value>::success(eve::Value(std::move(migrated)));
                       });
        if (!remainderRegistration.ok()) std::terminate();
        return result;
    }();
    return chain;
}

template <class T>
eve::Result<T> snapshotFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

eve::Result<eve::SnapshotEnvelope> WorkQueue::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    auto serialized = snapshot();
    if (!serialized.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(serialized.status());
    auto payload = eve::Value::fromJson(std::move(serialized).takeValue());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("production.queue", productionSchema(), eve::SchemaVersion(7), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> WorkQueue::restoreSnapshot(const eve::SnapshotEnvelope&     source,
                                             const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "production.queue" || source.schema != productionSchema())
        return snapshotFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to production::WorkQueue");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "snapshot instanceId does not match production::WorkQueue");
    auto migrated = productionMigrations().migrate(source, eve::SchemaVersion(7), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    const auto& candidateEnvelope = migrated.value();
    auto        metadata = eve::validateSnapshotPayloadMetadata(candidateEnvelope.payload, candidateEnvelope.revision,
                                                                candidateEnvelope.tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto payload = candidateEnvelope.payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());

    WorkQueue candidate(instanceId_);
    auto      restored = candidate.restore(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    candidate.instanceId_ = candidateEnvelope.instanceId;
    candidate.revision_   = candidateEnvelope.revision;
    candidate.tick_       = candidateEnvelope.tick;
    *this                 = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<std::string> WorkQueue::snapshotEnvelopeJson(const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> WorkQueue::restoreSnapshotJson(std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

}  // namespace eve::production
