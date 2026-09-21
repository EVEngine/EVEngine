#include "production/Production.h"

#include "common/Json.h"
#include <algorithm>
#include <exception>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace eve::production {
namespace {

template <class T>
eve::Result<T> persistenceFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "production.persistence"));
}

std::string quote(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20)
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
                else
                    out << static_cast<char>(c);
        }
    }
    return out.str() + '"';
}

std::string canonicalJson(const eve::json::Value& value) {
    if (value.isNull()) return "null";
    if (value.isBool()) return value.asBool() ? "true" : "false";
    if (value.isNumber()) return value.asString();
    if (value.isString()) return quote(value.asString());
    if (value.isArray()) {
        std::string out = "[";
        for (size_t i = 0; i < value.size(); ++i) {
            if (i) out += ',';
            out += canonicalJson(value.at(i));
        }
        return out + ']';
    }
    auto keys = value.keys();
    std::sort(keys.begin(), keys.end());
    std::string out = "{";
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i) out += ',';
        out += quote(keys[i]) + ':' + canonicalJson(value.get(keys[i].c_str()));
    }
    return out + '}';
}

bool parseU64(const eve::json::Value& value, uint64_t& result) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        result      = std::stoull(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parseI64(const eve::json::Value& value, std::int64_t& result) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        result      = std::stoll(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parseDuration(const eve::json::Value& nanoseconds, const eve::json::Value& legacySeconds, eve::Duration& result) {
    if (!nanoseconds.isNull()) {
        std::int64_t value = 0;
        if (!parseI64(nanoseconds, value)) return false;
        result = eve::Duration::fromNanoseconds(value);
        return true;
    }
    if (!legacySeconds.isNumber()) return false;
    auto converted = eve::Duration::fromSeconds(legacySeconds.asDouble());
    if (!converted) return false;
    result = std::move(converted).takeValue();
    return true;
}

bool parseState(const std::string& name, TaskState& state) {
    if (name == "queued")
        state = TaskState::Queued;
    else if (name == "running")
        state = TaskState::Running;
    else if (name == "paused")
        state = TaskState::Paused;
    else if (name == "ready_to_settle")
        state = TaskState::ReadyToSettle;
    else if (name == "settlement_failed")
        state = TaskState::SettlementFailed;
    else if (name == "completed")
        state = TaskState::Completed;
    else if (name == "cancelled")
        state = TaskState::Cancelled;
    else if (name == "failed")
        state = TaskState::Failed;
    else
        return false;
    return true;
}

bool parseEventKind(const std::string& name, ProductionEventKind& kind) {
    if (name == "enqueued")
        kind = ProductionEventKind::Enqueued;
    else if (name == "started")
        kind = ProductionEventKind::Started;
    else if (name == "paused")
        kind = ProductionEventKind::Paused;
    else if (name == "resumed")
        kind = ProductionEventKind::Resumed;
    else if (name == "ready_to_settle")
        kind = ProductionEventKind::ReadyToSettle;
    else if (name == "settlement_failed")
        kind = ProductionEventKind::SettlementFailed;
    else if (name == "completed")
        kind = ProductionEventKind::Completed;
    else if (name == "cancelled")
        kind = ProductionEventKind::Cancelled;
    else if (name == "failed")
        kind = ProductionEventKind::Failed;
    else
        return false;
    return true;
}

}  // namespace

eve::Result<std::string> WorkQueue::snapshot() const {
    std::ostringstream out;
    const auto refundName = [](RefundPolicy policy) {
        if (policy == RefundPolicy::None) return "none";
        if (policy == RefundPolicy::Proportional) return "proportional";
        return "full";
    };
    out << "{\"version\":7,\"revision\":" << quote(std::to_string(revision_.value()))
        << ",\"nextEnqueueSequence\":" << quote(std::to_string(nextEnqueueSequence_))
        << ",\"nextEventSequence\":" << quote(std::to_string(nextEventSequence_))
        << ",\"nextTaskId\":" << quote(std::to_string(nextTaskId_))
        << ",\"tick\":" << quote(std::to_string(tick_.value())) << ",\"slots\":[";
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (i) out << ',';
        out << "{\"owner\":" << quote(slots_[i].first) << ",\"value\":" << slots_[i].second << '}';
    }
    out << "],\"resources\":[";
    for (size_t i = 0; i < resources_.size(); ++i) {
        if (i) out << ',';
        out << "{\"capacity\":" << resources_[i].second << ",\"owner\":" << quote(resources_[i].first.first)
            << ",\"resource\":" << quote(resources_[i].first.second) << '}';
    }
    out << "],\"availableDefinitions\":[";
    for (size_t i = 0; i < availableDefinitions_.size(); ++i) {
        if (i) out << ',';
        out << "{\"definition\":" << quote(std::get<1>(availableDefinitions_[i]))
            << ",\"generation\":" << quote(std::to_string(std::get<2>(availableDefinitions_[i])))
            << ",\"owner\":" << quote(std::get<0>(availableDefinitions_[i])) << '}';
    }
    out << "],\"availableTags\":[";
    for (size_t i = 0; i < availableTags_.size(); ++i) {
        if (i) out << ',';
        out << "{\"owner\":" << quote(availableTags_[i].first)
            << ",\"tag\":" << quote(availableTags_[i].second) << '}';
    }
    out << "],\"events\":[";
    for (size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        const auto& event = events_[i];
        out << "{\"correlationId\":" << quote(event.correlationId)
            << ",\"kind\":" << quote(eventKindName(event.kind)) << ",\"owner\":" << quote(event.owner)
            << ",\"product\":" << quote(event.product) << ",\"reason\":" << quote(event.reason)
            << ",\"sequence\":" << quote(std::to_string(event.sequence)) << ",\"taskId\":" << quote(event.taskId)
            << ",\"taskKind\":" << quote(event.taskKind) << ",\"tick\":" << quote(std::to_string(event.tick.value()))
            << '}';
    }
    out << "],\"schedulers\":[";
    for (size_t i = 0; i < schedulerStrategies_.size(); ++i) {
        if (i) out << ',';
        const auto strategy = schedulerStrategies_[i].second;
        const auto name = strategy == SchedulerStrategy::Fifo ? "fifo" :
                          strategy == SchedulerStrategy::ShortestRemaining ? "shortest_remaining" : "priority";
        out << "{\"owner\":" << quote(schedulerStrategies_[i].first)
            << ",\"strategy\":" << quote(name) << '}';
    }
    out << "],\"tasks\":[";
    const auto writeIds = [&out](const std::vector<std::string>& ids) {
        out << '[';
        for (size_t index = 0; index < ids.size(); ++index) {
            if (index) out << ',';
            out << quote(ids[index]);
        }
        out << ']';
    };
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (i) out << ',';
        const auto& t = *tasks_[i];
        auto        context = t.context.toJson();
        if (!context.ok()) return eve::Result<std::string>::failure(context.status());
        auto reservation = t.reservation.toJson();
        if (!reservation.ok()) return eve::Result<std::string>::failure(reservation.status());
        auto releasePayload = t.reservationRelease.reservation.toJson();
        if (!releasePayload.ok()) return eve::Result<std::string>::failure(releasePayload.status());
        auto settlementPayload = t.settlement.payload.toJson();
        if (!settlementPayload.ok()) return eve::Result<std::string>::failure(settlementPayload.status());
        out << "{\"batchSize\":" << t.batchSize << ",\"blocks\":";
        writeIds(t.dependencies.blocks);
        out << ",\"completedCycles\":" << t.completedCycles
            << ",\"continuous\":" << (t.repeat.continuous ? "true" : "false")
            << ",\"context\":" << std::move(context).takeValue()
            << ",\"correlationId\":" << quote(t.correlationId)
            << ",\"definition\":" << quote(t.definition.isValid() ? t.definition.reference.id().format() : "")
            << ",\"definitionGeneration\":" << quote(std::to_string(t.definition.generation.value()))
            << ",\"dependencyMode\":"
            << quote(t.dependencies.mode == TaskDependencyMode::AllOf ? "all_of" : "any_of")
            << ",\"durationNs\":" << quote(std::to_string(t.duration.nanoseconds()))
            << ",\"efficiencyPermille\":" << t.efficiencyPermille
            << ",\"enqueueSequence\":" << quote(std::to_string(t.enqueueSequence)) << ",\"id\":" << quote(t.id)
            << ",\"kind\":" << quote(t.kind) << ",\"owner\":" << quote(t.owner) << ",\"priority\":" << t.priority
            << ",\"maintainStockTarget\":" << t.repeat.maintainStockTarget
            << ",\"lastSettlementId\":" << quote(t.lastSettlementId)
            << ",\"observedStock\":" << t.repeat.observedStock
            << ",\"product\":" << quote(t.product) << ",\"prerequisites\":";
        writeIds(t.dependencies.prerequisites);
        out << ",\"randomDrawCount\":" << t.random.drawCount
            << ",\"randomDrawStart\":" << quote(std::to_string(t.random.drawStart))
            << ",\"randomSeed\":" << quote(std::to_string(t.random.seed))
            << ",\"randomStream\":" << quote(t.random.stream)
            << ",\"refundCancellation\":" << quote(refundName(t.termination.cancellation))
            << ",\"refundFailure\":" << quote(refundName(t.termination.failure))
            << ",\"refundPermille\":" << t.refundPermille << ",\"requiredDefinitions\":[";
        for (size_t requirementIndex = 0; requirementIndex < t.dependencies.requiredDefinitions.size();
             ++requirementIndex) {
            if (requirementIndex) out << ',';
            const auto& definition = t.dependencies.requiredDefinitions[requirementIndex];
            out << "{\"definition\":" << quote(definition.reference.id().format())
                << ",\"generation\":" << quote(std::to_string(definition.generation.value())) << '}';
        }
        out << "],\"requiredTags\":";
        writeIds(t.dependencies.requiredTags);
        out << ",\"requirements\":[";
        for (size_t requirementIndex = 0; requirementIndex < t.resources.size(); ++requirementIndex) {
            if (requirementIndex) out << ',';
            out << "{\"resource\":" << quote(t.resources[requirementIndex].resource)
                << ",\"units\":" << t.resources[requirementIndex].units << '}';
        }
        out
            << ']'
            << ",\"progressNs\":" << quote(std::to_string(t.progress.nanoseconds()))
            << ",\"workRemainderPermille\":" << t.workRemainderPermille
            << ",\"reason\":" << quote(t.reason) << ",\"reservation\":"
            << std::move(reservation).takeValue()
            << ",\"reservationReleaseId\":" << quote(t.reservationRelease.releaseId)
            << ",\"reservationReleasePayload\":" << std::move(releasePayload).takeValue()
            << ",\"reservationReleaseRefundPermille\":" << t.reservationRelease.refundPermille
            << ",\"reservationState\":" << quote(
                   t.reservationState == ReservationState::Reserved ? "reserved" :
                   t.reservationState == ReservationState::Started ? "started" :
                   t.reservationState == ReservationState::Consumed ? "consumed" :
                   t.reservationState == ReservationState::Released ? "released" : "none")
            << ",\"settlementId\":" << quote(t.settlement.settlementId)
            << ",\"settlementPayload\":" << std::move(settlementPayload).takeValue()
            << ",\"settlementRequired\":" << (t.settlementRequired ? "true" : "false")
            << ",\"state\":" << quote(taskStateName(t.state))
            << ",\"totalCycles\":" << t.repeat.totalCycles << '}';
    }
    return eve::Result<std::string>::success(out.str() + "]}");
}

eve::Result<void> WorkQueue::restore(std::string_view json) {
    std::string error;
    auto        doc = eve::json::Document::parse(std::string(json), &error);
    if (!doc.valid() || !doc.root().isObject()) {
        return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                              error.empty() ? "snapshot must be an object" : error);
    }
    WorkQueue       candidate(instanceId_);
    const auto      root = doc.root();
    const int version = root.getInt("version");
    if ((version != 1 && version != 2 && version != 3 && version != 4 && version != 5 && version != 6 &&
         version != 7) ||
        !parseU64(root.get("nextTaskId"), candidate.nextTaskId_) ||
        !parseU64(root.get("nextEnqueueSequence"), candidate.nextEnqueueSequence_) ||
        !parseU64(root.get("nextEventSequence"), candidate.nextEventSequence_) || candidate.nextTaskId_ == 0 ||
        candidate.nextEnqueueSequence_ == 0 || candidate.nextEventSequence_ == 0 || !root.get("slots").isArray() ||
        !root.get("events").isArray() || !root.get("tasks").isArray() ||
        (version >= 4 && !root.get("resources").isArray()) ||
        (version >= 5 && !root.get("schedulers").isArray()) ||
        (version >= 6 && (!root.get("availableDefinitions").isArray() || !root.get("availableTags").isArray()))) {
        return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot counters or arrays");
    }
    if (!root.get("tick").isNull()) {
        uint64_t tick = 0;
        if (!parseU64(root.get("tick"), tick)) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot tick", "tick");
        }
        candidate.tick_ = eve::SimulationTick(tick);
    }
    if (!root.get("revision").isNull()) {
        uint64_t revision = 0;
        if (!parseU64(root.get("revision"), revision)) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot revision",
                                                  "revision");
        }
        candidate.revision_ = eve::Revision(revision);
    } else {
        candidate.revision_ = eve::Revision(candidate.nextEventSequence_ - 1);
    }
    for (size_t i = 0; i < root.get("slots").size(); ++i) {
        auto value = root.get("slots").at(i);
        if (!value.isObject() || !value.get("owner").isString() || !value.get("value").isNumber()) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid slot entry", "slots");
        }
        int slots = value.get("value").asInt();
        auto setSlots = candidate.setSlotCount(value.get("owner").asString(), slots);
        if (!setSlots.ok()) return eve::Result<void>::failure(setSlots.status());
    }
    if (version >= 4) {
        for (size_t i = 0; i < root.get("resources").size(); ++i) {
            const auto value = root.get("resources").at(i);
            if (!value.isObject() || !value.get("owner").isString() || !value.get("resource").isString() ||
                !value.get("capacity").isNumber())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid resource capacity", "resources");
            auto result = candidate.setResourceCapacity(value.get("owner").asString(),
                                                        value.get("resource").asString(),
                                                        value.get("capacity").asInt());
            if (!result) return eve::Result<void>::failure(result.status());
        }
    }
    if (version >= 5) {
        for (size_t i = 0; i < root.get("schedulers").size(); ++i) {
            const auto value = root.get("schedulers").at(i);
            if (!value.isObject() || !value.get("owner").isString() || !value.get("strategy").isString())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid scheduler strategy", "schedulers");
            SchedulerStrategy strategy;
            const auto name = value.get("strategy").asString();
            if (name == "priority") strategy = SchedulerStrategy::Priority;
            else if (name == "fifo") strategy = SchedulerStrategy::Fifo;
            else if (name == "shortest_remaining") strategy = SchedulerStrategy::ShortestRemaining;
            else return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                        "unknown scheduler strategy", "schedulers.strategy");
            auto result = candidate.setSchedulerStrategy(value.get("owner").asString(), strategy);
            if (!result) return eve::Result<void>::failure(result.status());
        }
    }
    if (version >= 6) {
        for (size_t i = 0; i < root.get("availableDefinitions").size(); ++i) {
            const auto value = root.get("availableDefinitions").at(i);
            uint64_t generation = 0;
            if (!value.isObject() || !value.get("owner").isString() ||
                !value.get("definition").isString() || !parseU64(value.get("generation"), generation))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid available definition", "availableDefinitions");
            auto reference = eve::DefinitionRef::parse(value.get("definition").asString());
            if (!reference || generation == 0)
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid available definition handle", "availableDefinitions");
            const eve::definition::DefinitionHandle handle{
                std::move(reference).takeValue(), eve::Generation(generation)};
            auto result = candidate.setDefinitionAvailable(value.get("owner").asString(), handle, true);
            if (!result) return eve::Result<void>::failure(result.status());
        }
        for (size_t i = 0; i < root.get("availableTags").size(); ++i) {
            const auto value = root.get("availableTags").at(i);
            if (!value.isObject() || !value.get("owner").isString() || !value.get("tag").isString())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid available tag", "availableTags");
            auto result = candidate.setTagAvailable(value.get("owner").asString(), value.get("tag").asString(), true);
            if (!result) return eve::Result<void>::failure(result.status());
        }
    }
    std::set<std::string> ids;
    for (size_t i = 0; i < root.get("tasks").size(); ++i) {
        auto      value    = root.get("tasks").at(i);
        auto      task     = std::make_unique<ProductionTask>();
        uint64_t  sequence = 0;
        TaskState state = TaskState::Queued;
        if (!value.isObject() || !value.get("id").isString() || !value.get("owner").isString() ||
            !value.get("kind").isString() || !value.get("product").isString() ||
            (!value.get("progressNs").isString() && !value.get("progress").isNumber()) ||
            (!value.get("durationNs").isString() && !value.get("duration").isNumber()) ||
            !value.get("priority").isNumber() || !value.get("state").isString() || !value.get("reason").isString() ||
            !parseU64(value.get("enqueueSequence"), sequence) || !parseState(value.get("state").asString(), state) ||
            value.get("id").asString().empty() || value.get("owner").asString().empty() ||
            value.get("kind").asString().empty() || value.get("product").asString().empty() ||
            !ids.insert(value.get("id").asString()).second) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid task entry", "tasks");
        }
        task->id              = value.get("id").asString();
        task->owner           = value.get("owner").asString();
        task->kind            = value.get("kind").asString();
        task->product         = value.get("product").asString();
        auto context          = eve::Value::fromJson(canonicalJson(value.get("context")));
        if (!context.ok()) return eve::Result<void>::failure(context.status());
        if (!context.value().isObject())
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                  "work task context must be an object", "tasks.context");
        task->context = std::move(context).takeValue();
        if (version >= 2) {
            if (!value.get("definition").isString() || !value.get("definitionGeneration").isString() ||
                !value.get("settlementId").isString() || !value.get("settlementRequired").isBool())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid task production metadata", "tasks");
            uint64_t generation = 0;
            if (!parseU64(value.get("definitionGeneration"), generation))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid definition generation", "tasks.definitionGeneration");
            const std::string definition = value.get("definition").asString();
            if (!definition.empty()) {
                auto reference = eve::DefinitionRef::parse(definition);
                if (!reference || generation == 0)
                    return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                           "invalid pinned definition", "tasks.definition");
                task->definition = {std::move(reference).takeValue(), eve::Generation(generation)};
            } else if (generation != 0) {
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "definition generation has no definition", "tasks.definition");
            }
            auto reservation = eve::Value::fromJson(canonicalJson(value.get("reservation")));
            auto settlementPayload = eve::Value::fromJson(canonicalJson(value.get("settlementPayload")));
            if (!reservation || !settlementPayload || !reservation.value().isObject() ||
                !settlementPayload.value().isObject())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid reservation or settlement payload", "tasks");
            task->reservation = std::move(reservation).takeValue();
            task->settlement.settlementId = value.get("settlementId").asString();
            task->settlement.payload = std::move(settlementPayload).takeValue();
            task->settlementRequired = value.get("settlementRequired").asBool();
            if (state == TaskState::Completed && task->settlement.settlementId.empty())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "settled task has no receipt", "tasks.settlementId");
        } else if (state == TaskState::Completed) {
            task->settlement.settlementId = "legacy:" + task->id;
            task->settlementRequired = false;
        }
        if (version >= 3) {
            if (!value.get("dependencyMode").isString() || !value.get("prerequisites").isArray() ||
                !value.get("blocks").isArray())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid production dependency metadata", "tasks");
            const std::string mode = value.get("dependencyMode").asString();
            if (mode == "all_of")
                task->dependencies.mode = TaskDependencyMode::AllOf;
            else if (mode == "any_of")
                task->dependencies.mode = TaskDependencyMode::AnyOf;
            else
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid production dependency mode", "tasks.dependencyMode");
            const auto parseIds = [](const eve::json::Value& source,
                                     std::vector<std::string>& target) -> bool {
                std::set<std::string> unique;
                for (size_t index = 0; index < source.size(); ++index) {
                    const auto entry = source.at(index);
                    if (!entry.isString() || entry.asString().empty() ||
                        !unique.insert(entry.asString()).second)
                        return false;
                    target.push_back(entry.asString());
                }
                return true;
            };
            if (!parseIds(value.get("prerequisites"), task->dependencies.prerequisites) ||
                !parseIds(value.get("blocks"), task->dependencies.blocks))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid production dependency ids", "tasks");
        }
        if (version >= 4) {
            if (!value.get("batchSize").isNumber() || !value.get("efficiencyPermille").isNumber() ||
                !value.get("refundCancellation").isString() || !value.get("refundFailure").isString() ||
                !value.get("refundPermille").isNumber() || !value.get("requirements").isArray())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid production execution metadata", "tasks");
            const auto parseRefund = [](std::string_view text, RefundPolicy& output) {
                if (text == "none") output = RefundPolicy::None;
                else if (text == "full") output = RefundPolicy::Full;
                else if (text == "proportional") output = RefundPolicy::Proportional;
                else return false;
                return true;
            };
            const int batchSize = value.get("batchSize").asInt();
            const int efficiency = value.get("efficiencyPermille").asInt();
            const int refund = value.get("refundPermille").asInt();
            if (batchSize <= 0 || efficiency <= 0 || refund < 0 || refund > 1000 ||
                !parseRefund(value.get("refundCancellation").asString(), task->termination.cancellation) ||
                !parseRefund(value.get("refundFailure").asString(), task->termination.failure))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid production batch, efficiency or refund", "tasks");
            task->batchSize = static_cast<std::uint32_t>(batchSize);
            task->efficiencyPermille = static_cast<std::uint32_t>(efficiency);
            task->refundPermille = static_cast<std::uint32_t>(refund);
            std::set<std::string> resourceNames;
            for (size_t index = 0; index < value.get("requirements").size(); ++index) {
                const auto requirement = value.get("requirements").at(index);
                if (!requirement.isObject() || !requirement.get("resource").isString() ||
                    !requirement.get("units").isNumber() || requirement.get("resource").asString().empty() ||
                    requirement.get("units").asInt() <= 0 ||
                    !resourceNames.insert(requirement.get("resource").asString()).second)
                    return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                           "invalid task resource requirement", "tasks.requirements");
                task->resources.push_back(
                    {requirement.get("resource").asString(), requirement.get("units").asInt()});
            }
        }
        if (version >= 5) {
            if (!value.get("completedCycles").isNumber() || !value.get("continuous").isBool() ||
                !value.get("correlationId").isString() || !value.get("lastSettlementId").isString() ||
                !value.get("maintainStockTarget").isNumber() ||
                !value.get("observedStock").isNumber() || !value.get("randomDrawCount").isNumber() ||
                !value.get("randomDrawStart").isString() || !value.get("randomSeed").isString() ||
                !value.get("randomStream").isString() || !value.get("totalCycles").isNumber())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid advanced production metadata", "tasks");
            uint64_t randomSeed = 0;
            uint64_t randomDrawStart = 0;
            const int completedCycles = value.get("completedCycles").asInt();
            const int drawCount = value.get("randomDrawCount").asInt();
            const int totalCycles = value.get("totalCycles").asInt();
            const int stockTarget = value.get("maintainStockTarget").asInt();
            const int observedStock = value.get("observedStock").asInt();
            if (completedCycles < 0 || drawCount < 0 || totalCycles <= 0 || stockTarget < -1 || observedStock < 0 ||
                !parseU64(value.get("randomSeed"), randomSeed) ||
                !parseU64(value.get("randomDrawStart"), randomDrawStart) ||
                (drawCount > 0 && value.get("randomStream").asString().empty()))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid advanced production values", "tasks");
            task->completedCycles = static_cast<std::uint32_t>(completedCycles);
            task->repeat.continuous = value.get("continuous").asBool();
            task->repeat.totalCycles = static_cast<std::uint32_t>(totalCycles);
            task->repeat.maintainStockTarget = stockTarget;
            task->repeat.observedStock = observedStock;
            task->correlationId = value.get("correlationId").asString();
            task->lastSettlementId = value.get("lastSettlementId").asString();
            task->random = {value.get("randomStream").asString(), randomSeed, randomDrawStart,
                            static_cast<std::uint32_t>(drawCount)};
        } else {
            task->correlationId = task->id;
        }
        if (version >= 6) {
            if (!value.get("requiredDefinitions").isArray() || !value.get("requiredTags").isArray() ||
                !value.get("reservationReleaseId").isString() ||
                !value.get("reservationReleaseRefundPermille").isNumber() ||
                !value.get("reservationState").isString())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid definition or tag requirements", "tasks");
            std::set<std::pair<std::string, std::uint64_t>> definitions;
            for (size_t index = 0; index < value.get("requiredDefinitions").size(); ++index) {
                const auto requirement = value.get("requiredDefinitions").at(index);
                uint64_t generation = 0;
                if (!requirement.isObject() || !requirement.get("definition").isString() ||
                    !parseU64(requirement.get("generation"), generation))
                    return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                    "invalid required definition", "tasks.requiredDefinitions");
                const auto name = requirement.get("definition").asString();
                auto reference = eve::DefinitionRef::parse(name);
                if (!reference || generation == 0 || !definitions.insert({name, generation}).second)
                    return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                    "invalid required definition handle", "tasks.requiredDefinitions");
                task->dependencies.requiredDefinitions.push_back(
                    {std::move(reference).takeValue(), eve::Generation(generation)});
            }
            std::set<std::string> tags;
            for (size_t index = 0; index < value.get("requiredTags").size(); ++index) {
                const auto requirement = value.get("requiredTags").at(index);
                if (!requirement.isString() || requirement.asString().empty() ||
                    !tags.insert(requirement.asString()).second)
                    return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                    "invalid required tag", "tasks.requiredTags");
                task->dependencies.requiredTags.push_back(requirement.asString());
            }
            const auto reservationState = value.get("reservationState").asString();
            if (reservationState == "none") task->reservationState = ReservationState::None;
            else if (reservationState == "reserved") task->reservationState = ReservationState::Reserved;
            else if (reservationState == "started") task->reservationState = ReservationState::Started;
            else if (reservationState == "consumed") task->reservationState = ReservationState::Consumed;
            else if (reservationState == "released") task->reservationState = ReservationState::Released;
            else return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                 "invalid reservation state", "tasks.reservationState");
            auto releasePayload = eve::Value::fromJson(canonicalJson(value.get("reservationReleasePayload")));
            const int releaseRefund = value.get("reservationReleaseRefundPermille").asInt();
            if (!releasePayload || !releasePayload.value().isObject() || releaseRefund < 0 || releaseRefund > 1000)
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid reservation release", "tasks.reservationRelease");
            task->reservationRelease = {value.get("reservationReleaseId").asString(),
                                        std::move(releasePayload).takeValue(),
                                        static_cast<std::uint32_t>(releaseRefund)};
            if ((task->reservationState == ReservationState::Released) !=
                !task->reservationRelease.releaseId.empty())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "reservation release state and receipt disagree",
                                                "tasks.reservationReleaseId");
        }
        if (version >= 7) {
            if (!value.get("workRemainderPermille").isNumber())
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "invalid production work remainder", "tasks.workRemainderPermille");
            const int remainder = value.get("workRemainderPermille").asInt();
            if (remainder < 0 || remainder >= 1000)
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                "production work remainder must be between 0 and 999",
                                                "tasks.workRemainderPermille");
            task->workRemainderPermille = static_cast<std::uint32_t>(remainder);
        }
        if (!parseDuration(value.get("durationNs"), value.get("duration"), task->duration) ||
            !parseDuration(value.get("progressNs"), value.get("progress"), task->progress)) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid task duration",
                                                  "tasks.duration");
        }
        task->priority        = value.get("priority").asInt();
        task->state           = state;
        task->enqueueSequence = sequence;
        task->reason          = value.get("reason").asString();
        if (task->duration.nanoseconds() <= 0 || task->progress.nanoseconds() < 0 || task->progress > task->duration) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid task progress",
                                                  "tasks.progress");
        }
        candidate.tasks_.push_back(std::move(task));
    }
    for (const auto& task : candidate.tasks_) {
        for (const auto& dependencyId : task->dependencies.prerequisites) {
            if (dependencyId == task->id || !ids.contains(dependencyId))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid prerequisite reference", "tasks.prerequisites");
        }
        for (const auto& blockerId : task->dependencies.blocks) {
            if (blockerId == task->id || !ids.contains(blockerId))
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                       "invalid blocker reference", "tasks.blocks");
        }
    }
    std::set<std::string> visiting;
    std::set<std::string> visited;
    std::function<bool(const ProductionTask&)> acyclic = [&](const ProductionTask& task) {
        if (visited.contains(task.id)) return true;
        if (!visiting.insert(task.id).second) return false;
        for (const auto& dependencyId : task.dependencies.prerequisites) {
            const auto dependency = candidate.find(dependencyId);
            if (!dependency || !acyclic(dependency->get())) return false;
        }
        visiting.erase(task.id);
        visited.insert(task.id);
        return true;
    };
    for (const auto& task : candidate.tasks_)
        if (!acyclic(*task))
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError,
                                                   "production dependency graph contains a cycle",
                                                   "tasks.prerequisites");
    uint64_t previousEventSequence = 0;
    for (size_t i = 0; i < root.get("events").size(); ++i) {
        auto            value = root.get("events").at(i);
        ProductionEvent event;
        if (!value.isObject() || !value.get("kind").isString() || !value.get("owner").isString() ||
            !value.get("product").isString() || !value.get("reason").isString() || !value.get("taskId").isString() ||
            !value.get("taskKind").isString() || !parseU64(value.get("sequence"), event.sequence) ||
            !parseEventKind(value.get("kind").asString(), event.kind) || event.sequence <= previousEventSequence ||
            event.sequence >= candidate.nextEventSequence_) {
            return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid event entry", "events");
        }
        previousEventSequence = event.sequence;
        event.owner           = value.get("owner").asString();
        event.product         = value.get("product").asString();
        event.reason          = value.get("reason").asString();
        event.taskId          = value.get("taskId").asString();
        event.taskKind        = value.get("taskKind").asString();
        event.correlationId   = version >= 5 && value.get("correlationId").isString()
            ? value.get("correlationId").asString() : event.taskId;
        if (!value.get("tick").isNull()) {
            uint64_t tick = 0;
            if (!parseU64(value.get("tick"), tick)) {
                return persistenceFailure<void>(eve::DiagnosticCode::ParseError, "invalid event tick",
                                                      "events.tick");
            }
            event.tick = eve::SimulationTick(tick);
        }
        candidate.events_.push_back(std::move(event));
    }
    *this = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void WorkQueue::clear() {
    tasks_.clear();
    events_.clear();
    slots_.clear();
    resources_.clear();
    schedulerStrategies_.clear();
    availableDefinitions_.clear();
    availableTags_.clear();
    nextTaskId_ = nextEnqueueSequence_ = nextEventSequence_ = 1;
    revision_                                               = eve::Revision::zero();
    tick_                                                   = eve::SimulationTick::zero();
}

}  // namespace eve::production
