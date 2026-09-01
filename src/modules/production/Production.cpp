#include "production/Production.h"

#include "common/Json.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>

namespace eve::production {
namespace {

/** @brief Script-owned handle proxy; the queue remains owned by Production. */
struct ScriptWorkQueue {
    explicit ScriptWorkQueue(WorkQueueHandleRef value) : reference(value) {}
    WorkQueueHandleRef reference;
};

/** @brief Squirrel-owned task view that re-resolves its queue handle per call. */
struct ScriptWorkTask {
    ScriptWorkTask(WorkQueueHandleRef queue, std::string id) : reference(queue), taskId(std::move(id)) {}
    WorkQueueHandleRef reference;
    std::string        taskId;
};

/** @brief Squirrel-owned event view that re-resolves its queue handle per call. */
struct ScriptWorkEvent {
    ScriptWorkEvent(WorkQueueHandleRef queue, int eventIndex) : reference(queue), index(eventIndex) {}
    WorkQueueHandleRef reference;
    int                index = -1;
};

template <class T>
eve::Result<T> productionBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "production.squirrel"));
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release, const char* errorPath) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned production proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned production allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    (void)errorPath;
    return result;
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

ssq::Object projectValueObject(HSQUIRRELVM vm, const eve::Value& value) {
    if (!vm) return ssq::Object();
    const SQInteger top    = sq_gettop(vm);
    auto            pushed = eve::script::pushValue(vm, value);
    if (!pushed.ok()) {
        pushed.ignore("failed to project a work-task Value payload");
        sq_settop(vm, top);
        return ssq::Object(vm);
    }
    ssq::Object result(vm);
    if (SQ_SUCCEEDED(sq_getstackobj(vm, -1, &result.getRaw()))) sq_addref(vm, &result.getRaw());
    sq_settop(vm, top);
    return result;
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
        return result;
    }();
    return chain;
}

template <class T>
eve::Result<T> snapshotFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

bool terminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Cancelled || state == TaskState::Failed;
}

}  // namespace

WorkQueue::WorkQueue(eve::PersistentId instanceId) : instanceId_(instanceId) {}

std::string_view taskStateName(TaskState state) {
    switch (state) {
        case TaskState::Queued: return "queued";
        case TaskState::Running: return "running";
        case TaskState::Paused: return "paused";
        case TaskState::Completed: return "completed";
        case TaskState::Cancelled: return "cancelled";
        case TaskState::Failed: return "failed";
    }
    return "unknown";
}

std::string_view eventKindName(ProductionEventKind kind) {
    switch (kind) {
        case ProductionEventKind::Enqueued: return "enqueued";
        case ProductionEventKind::Started: return "started";
        case ProductionEventKind::Paused: return "paused";
        case ProductionEventKind::Resumed: return "resumed";
        case ProductionEventKind::Completed: return "completed";
        case ProductionEventKind::Cancelled: return "cancelled";
        case ProductionEventKind::Failed: return "failed";
    }
    return "unknown";
}

void WorkQueue::emit(ProductionEventKind kind, const ProductionTask& task, std::string_view reason) {
    events_.push_back(
        {{nextEventSequence_++, std::string(reason)}, tick_, kind, task.id, task.owner, task.kind, task.product});
    if (const auto next = revision_.incremented()) revision_ = *next;
}

int WorkQueue::slotCount(std::string_view owner) const {
    const auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                                     [](const auto& entry, std::string_view key) { return entry.first < key; });
    return it != slots_.end() && it->first == owner ? it->second : 1;
}

int WorkQueue::runningCount(std::string_view owner) const {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& task) {
        return task->owner == owner && task->state == TaskState::Running;
    }));
}

void WorkQueue::schedule(std::string_view owner) {
    int available = slotCount(owner) - runningCount(owner);
    while (available-- > 0) {
        ProductionTask* best = nullptr;
        for (auto& candidate : tasks_) {
            if (candidate->owner != owner || candidate->state != TaskState::Queued) continue;
            if (!best || candidate->priority > best->priority ||
                (candidate->priority == best->priority && candidate->enqueueSequence < best->enqueueSequence))
                best = candidate.get();
        }
        if (!best) break;
        best->state = TaskState::Running;
        emit(ProductionEventKind::Started, *best);
    }
}

eve::Result<std::string> WorkQueue::enqueue(std::string_view owner, std::string_view kind, std::string_view product,
                                            eve::Value context, double duration, int priority) {
    if (owner.empty() || kind.empty() || product.empty())
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task owner, kind and product are required");
    if (!context.isObject())
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task context must be a Value object", "context");
    if (!std::isfinite(duration) || duration <= 0.0)
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task duration must be finite and positive", "duration");
    auto durationValue = eve::Duration::fromSeconds(duration);
    if (!durationValue.ok()) return eve::Result<std::string>::failure(durationValue.status());
    if (durationValue.value().nanoseconds() <= 0)
        return productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                     "work task duration must be positive", "duration");
    auto               task = std::make_unique<ProductionTask>();
    std::ostringstream id;
    id << "task-" << std::setw(16) << std::setfill('0') << nextTaskId_;
    task->id                 = id.str();
    task->owner              = std::string(owner);
    task->kind               = std::string(kind);
    task->product            = std::string(product);
    task->context            = std::move(context);
    task->duration           = std::move(durationValue).takeValue();
    task->priority           = priority;
    task->enqueueSequence    = nextEnqueueSequence_++;
    const std::string result = task->id;
    tasks_.push_back(std::move(task));
    ++nextTaskId_;
    emit(ProductionEventKind::Enqueued, *tasks_.back());
    schedule(owner);
    return eve::Result<std::string>::success(result);
}

eve::OptionalRef<ProductionTask> WorkQueue::find(std::string_view taskId) {
    const auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&taskId](const auto& task) { return task->id == taskId; });
    return it == tasks_.end() ? eve::OptionalRef<ProductionTask>{}
                              : eve::OptionalRef<ProductionTask>(std::ref(*it->get()));
}

eve::OptionalRef<const ProductionTask> WorkQueue::find(std::string_view taskId) const {
    const auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&taskId](const auto& task) { return task->id == taskId; });
    return it == tasks_.end() ? eve::OptionalRef<const ProductionTask>{}
                              : eve::OptionalRef<const ProductionTask>(std::cref(*it->get()));
}

eve::Result<void> WorkQueue::pause(std::string_view taskId) {
    auto taskRef = find(taskId);
    if (!taskRef)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    auto& task = taskRef->get();
    if (task.state != TaskState::Queued && task.state != TaskState::Running)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict,
                                              "only queued or running tasks can be paused", "taskId");
    task.state = TaskState::Paused;
    emit(ProductionEventKind::Paused, task);
    schedule(task.owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::resume(std::string_view taskId) {
    auto taskRef = find(taskId);
    if (!taskRef)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    auto& task = taskRef->get();
    if (task.state != TaskState::Paused)
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "only paused tasks can be resumed",
                                              "taskId");
    task.state = TaskState::Queued;
    emit(ProductionEventKind::Resumed, task);
    schedule(task.owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::cancel(std::string_view taskId, std::string_view reason) {
    auto taskRef = find(taskId);
    if (!taskRef)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    auto& task = taskRef->get();
    if (terminal(task.state))
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "terminal work tasks cannot be cancelled",
                                              "taskId");
    task.state  = TaskState::Cancelled;
    task.reason = std::string(reason);
    emit(ProductionEventKind::Cancelled, task, reason);
    schedule(task.owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::fail(std::string_view taskId, std::string_view reason) {
    auto taskRef = find(taskId);
    if (!taskRef)
        return productionBindingFailure<void>(eve::DiagnosticCode::NotFound, "work task was not found", "taskId");
    auto& task = taskRef->get();
    if (terminal(task.state))
        return productionBindingFailure<void>(eve::DiagnosticCode::Conflict, "terminal work tasks cannot fail",
                                              "taskId");
    task.state  = TaskState::Failed;
    task.reason = std::string(reason);
    emit(ProductionEventKind::Failed, task, reason);
    schedule(task.owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> WorkQueue::advance(const eve::SimulationStep& step) {
    if (step.tick <= tick_)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "production step tick must be strictly newer"));
    if (step.delta.nanoseconds() < 0)
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "production step duration must be non-negative"));

    tick_                       = step.tick;
    const std::int64_t    delta = step.delta.nanoseconds();
    std::set<std::string> owners;
    for (auto& task : tasks_) {
        if (task->state != TaskState::Running) continue;
        const std::int64_t remaining = task->duration.nanoseconds() - task->progress.nanoseconds();
        const std::int64_t applied   = delta >= remaining ? remaining : delta;
        task->progress               = eve::Duration::fromNanoseconds(task->progress.nanoseconds() + applied);
        if (task->progress >= task->duration) {
            task->state = TaskState::Completed;
            emit(ProductionEventKind::Completed, *task);
            owners.insert(task->owner);
        }
    }
    for (const auto& owner : owners) schedule(owner);
    return eve::Result<void>::success();
}

eve::Result<void> WorkQueue::setSlotCount(std::string_view owner, int slots) {
    if (owner.empty() || slots < 0)
        return productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                              "owner must be non-empty and slots must be non-negative");
    auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                               [](const auto& entry, std::string_view key) { return entry.first < key; });
    if (it != slots_.end() && it->first == owner)
        it->second = slots;
    else
        slots_.insert(it, {std::string(owner), slots});
    schedule(owner);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

int WorkQueue::taskCount() const { return static_cast<int>(tasks_.size()); }

eve::OptionalRef<ProductionTask> WorkQueue::taskAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= tasks_.size()) return {};
    return std::ref(*tasks_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const ProductionTask> WorkQueue::taskAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= tasks_.size()) return {};
    return std::cref(*tasks_[static_cast<size_t>(index)]);
}

int WorkQueue::ownerTaskCount(std::string_view owner) const {
    return static_cast<int>(
        std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& t) { return t->owner == owner; }));
}

eve::OptionalRef<ProductionTask> WorkQueue::ownerTaskAt(std::string_view owner, int index) {
    if (index < 0) return {};
    for (auto& task : tasks_)
        if (task->owner == owner && index-- == 0) return std::ref(*task);
    return {};
}

eve::OptionalRef<const ProductionTask> WorkQueue::ownerTaskAt(std::string_view owner, int index) const {
    if (index < 0) return {};
    for (const auto& task : tasks_)
        if (task->owner == owner && index-- == 0) return std::cref(*task);
    return {};
}

int WorkQueue::eventCount() const { return static_cast<int>(events_.size()); }

eve::OptionalRef<ProductionEvent> WorkQueue::eventAt(int index) {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::ref(events_[static_cast<size_t>(index)]);
}

eve::OptionalRef<const ProductionEvent> WorkQueue::eventAt(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return std::cref(events_[static_cast<size_t>(index)]);
}

void WorkQueue::clearEvents() { events_.clear(); }

eve::Result<std::string> WorkQueue::snapshot() const {
    std::ostringstream out;
    out << "{\"version\":1,\"revision\":" << quote(std::to_string(revision_.value()))
        << ",\"nextEnqueueSequence\":" << quote(std::to_string(nextEnqueueSequence_))
        << ",\"nextEventSequence\":" << quote(std::to_string(nextEventSequence_))
        << ",\"nextTaskId\":" << quote(std::to_string(nextTaskId_))
        << ",\"tick\":" << quote(std::to_string(tick_.value())) << ",\"slots\":[";
    for (size_t i = 0; i < slots_.size(); ++i) {
        if (i) out << ',';
        out << "{\"owner\":" << quote(slots_[i].first) << ",\"value\":" << slots_[i].second << '}';
    }
    out << "],\"events\":[";
    for (size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        const auto& event = events_[i];
        out << "{\"kind\":" << quote(eventKindName(event.kind)) << ",\"owner\":" << quote(event.owner)
            << ",\"product\":" << quote(event.product) << ",\"reason\":" << quote(event.reason)
            << ",\"sequence\":" << quote(std::to_string(event.sequence)) << ",\"taskId\":" << quote(event.taskId)
            << ",\"taskKind\":" << quote(event.taskKind) << ",\"tick\":" << quote(std::to_string(event.tick.value()))
            << '}';
    }
    out << "],\"tasks\":[";
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (i) out << ',';
        const auto& t = *tasks_[i];
        auto        context = t.context.toJson();
        if (!context.ok()) return eve::Result<std::string>::failure(context.status());
        out << "{\"context\":" << std::move(context).takeValue()
            << ",\"durationNs\":" << quote(std::to_string(t.duration.nanoseconds()))
            << ",\"enqueueSequence\":" << quote(std::to_string(t.enqueueSequence)) << ",\"id\":" << quote(t.id)
            << ",\"kind\":" << quote(t.kind) << ",\"owner\":" << quote(t.owner) << ",\"priority\":" << t.priority
            << ",\"product\":" << quote(t.product)
            << ",\"progressNs\":" << quote(std::to_string(t.progress.nanoseconds()))
            << ",\"reason\":" << quote(t.reason) << ",\"state\":" << quote(taskStateName(t.state)) << '}';
    }
    return eve::Result<std::string>::success(out.str() + "]}");
}

eve::Result<void> WorkQueue::restore(std::string_view json) {
    std::string error;
    auto        doc = eve::json::Document::parse(std::string(json), &error);
    if (!doc.valid() || !doc.root().isObject()) {
        return productionBindingFailure<void>(eve::DiagnosticCode::ParseError,
                                              error.empty() ? "snapshot must be an object" : error);
    }
    WorkQueue       candidate(instanceId_);
    const auto      root = doc.root();
    if (root.getInt("version") != 1 || !parseU64(root.get("nextTaskId"), candidate.nextTaskId_) ||
        !parseU64(root.get("nextEnqueueSequence"), candidate.nextEnqueueSequence_) ||
        !parseU64(root.get("nextEventSequence"), candidate.nextEventSequence_) || candidate.nextTaskId_ == 0 ||
        candidate.nextEnqueueSequence_ == 0 || candidate.nextEventSequence_ == 0 || !root.get("slots").isArray() ||
        !root.get("events").isArray() || !root.get("tasks").isArray()) {
        return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot counters or arrays");
    }
    if (!root.get("tick").isNull()) {
        uint64_t tick = 0;
        if (!parseU64(root.get("tick"), tick)) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot tick", "tick");
        }
        candidate.tick_ = eve::SimulationTick(tick);
    }
    if (!root.get("revision").isNull()) {
        uint64_t revision = 0;
        if (!parseU64(root.get("revision"), revision)) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid snapshot revision",
                                                  "revision");
        }
        candidate.revision_ = eve::Revision(revision);
    } else {
        candidate.revision_ = eve::Revision(candidate.nextEventSequence_ - 1);
    }
    for (size_t i = 0; i < root.get("slots").size(); ++i) {
        auto value = root.get("slots").at(i);
        if (!value.isObject() || !value.get("owner").isString() || !value.get("value").isNumber()) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid slot entry", "slots");
        }
        int slots = value.get("value").asInt();
        auto setSlots = candidate.setSlotCount(value.get("owner").asString(), slots);
        if (!setSlots.ok()) return eve::Result<void>::failure(setSlots.status());
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
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid task entry", "tasks");
        }
        task->id              = value.get("id").asString();
        task->owner           = value.get("owner").asString();
        task->kind            = value.get("kind").asString();
        task->product         = value.get("product").asString();
        auto context          = eve::Value::fromJson(canonicalJson(value.get("context")));
        if (!context.ok()) return eve::Result<void>::failure(context.status());
        if (!context.value().isObject())
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError,
                                                  "work task context must be an object", "tasks.context");
        task->context = std::move(context).takeValue();
        if (!parseDuration(value.get("durationNs"), value.get("duration"), task->duration) ||
            !parseDuration(value.get("progressNs"), value.get("progress"), task->progress)) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid task duration",
                                                  "tasks.duration");
        }
        task->priority        = value.get("priority").asInt();
        task->state           = state;
        task->enqueueSequence = sequence;
        task->reason          = value.get("reason").asString();
        if (task->duration.nanoseconds() <= 0 || task->progress.nanoseconds() < 0 || task->progress > task->duration) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid task progress",
                                                  "tasks.progress");
        }
        candidate.tasks_.push_back(std::move(task));
    }
    uint64_t previousEventSequence = 0;
    for (size_t i = 0; i < root.get("events").size(); ++i) {
        auto            value = root.get("events").at(i);
        ProductionEvent event;
        if (!value.isObject() || !value.get("kind").isString() || !value.get("owner").isString() ||
            !value.get("product").isString() || !value.get("reason").isString() || !value.get("taskId").isString() ||
            !value.get("taskKind").isString() || !parseU64(value.get("sequence"), event.sequence) ||
            !parseEventKind(value.get("kind").asString(), event.kind) || event.sequence <= previousEventSequence ||
            event.sequence >= candidate.nextEventSequence_) {
            return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid event entry", "events");
        }
        previousEventSequence = event.sequence;
        event.owner           = value.get("owner").asString();
        event.product         = value.get("product").asString();
        event.reason          = value.get("reason").asString();
        event.taskId          = value.get("taskId").asString();
        event.taskKind        = value.get("taskKind").asString();
        if (!value.get("tick").isNull()) {
            uint64_t tick = 0;
            if (!parseU64(value.get("tick"), tick)) {
                return productionBindingFailure<void>(eve::DiagnosticCode::ParseError, "invalid event tick",
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
    nextTaskId_ = nextEnqueueSequence_ = nextEventSequence_ = 1;
    revision_                                               = eve::Revision::zero();
    tick_                                                   = eve::SimulationTick::zero();
}

eve::Result<eve::SnapshotEnvelope> WorkQueue::snapshot(const eve::SnapshotHashProvider& hashProvider) const {
    auto serialized = snapshot();
    if (!serialized.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(serialized.status());
    auto payload = eve::Value::fromJson(std::move(serialized).takeValue());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("production.queue", productionSchema(), eve::SchemaVersion(1), instanceId_,
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
    auto migrated = productionMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
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

eve::Result<WorkQueueHandleRef> Production::newQueueHandle() {
    Production* module = Production::create();
    return module->queues_.emplace(std::make_unique<WorkQueue>());
}

eve::ResultRef<WorkQueue> Production::resolve(WorkQueueHandleRef reference) {
    Production* module = ModuleManager::getInstance<Production>("Production");
    if (!module)
        return productionBindingFailure<std::reference_wrapper<WorkQueue>>(
            eve::DiagnosticCode::StaleHandle, "Production module is no longer loaded", "queue");
    auto view = module->queues_.resolve(reference);
    if (!view.isBound())
        return productionBindingFailure<std::reference_wrapper<WorkQueue>>(eve::DiagnosticCode::StaleHandle,
                                                                           "work queue handle is stale", "queue");
    return eve::ResultRef<WorkQueue>::success(std::ref(*view));
}

eve::Result<void> Production::release(WorkQueueHandleRef reference) {
    Production* module = ModuleManager::getInstance<Production>("Production");
    if (!module)
        return productionBindingFailure<void>(eve::DiagnosticCode::StaleHandle, "Production module is no longer loaded",
                                              "queue");
    return module->queues_.erase(reference);
}

bool Production::isStale(WorkQueueHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    Production* module = ModuleManager::getInstance<Production>("Production");
    return !module || module->queues_.isStale(reference);
}

Module_IMPL(Production, new Production());

void Production::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto              task =
        table.addClass<ScriptWorkTask>("WorkTask", std::function<ScriptWorkTask*()>([] { return nullptr; }), true);
    auto resolveTask = [](ScriptWorkTask* proxy) -> eve::OptionalRef<ProductionTask> {
        if (!proxy) return {};
        auto queue = Production::resolve(proxy->reference);
        if (!queue.ok()) {
            queue.ignore("stale work queue while reading a task proxy");
            return {};
        }
        return queue.value().get().find(proxy->taskId);
    };
    task.addFunc("getId", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().id : std::string{};
    });
    task.addFunc("getOwner", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().owner : std::string{};
    });
    task.addFunc("getKind", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().kind : std::string{};
    });
    task.addFunc("getProduct", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().product : std::string{};
    });
    task.addFunc("getContext", [vm, resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? projectValueObject(vm, value->get().context) : ssq::Object(vm);
    });
    task.addFunc("getDuration", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? float(value->get().duration.seconds()) : 0.0f;
    });
    task.addFunc("getProgress", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? float(value->get().progress.seconds()) : 0.0f;
    });
    task.addFunc("getPriority", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().priority : 0;
    });
    task.addFunc("getState", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? std::string(taskStateName(value->get().state)) : std::string{};
    });
    task.addFunc("getReason", [resolveTask](ScriptWorkTask* v) {
        auto value = resolveTask(v);
        return value ? value->get().reason : std::string{};
    });

    auto event =
        table.addClass<ScriptWorkEvent>("WorkEvent", std::function<ScriptWorkEvent*()>([] { return nullptr; }), true);
    auto resolveEvent = [](ScriptWorkEvent* proxy) -> eve::OptionalRef<ProductionEvent> {
        if (!proxy) return {};
        auto queue = Production::resolve(proxy->reference);
        if (!queue.ok()) {
            queue.ignore("stale work queue while reading an event proxy");
            return {};
        }
        return queue.value().get().eventAt(proxy->index);
    };
    event.addFunc("getSequence", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? int64_t(value->get().sequence) : int64_t{0};
    });
    event.addFunc("getTick", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? int64_t(value->get().tick.value()) : int64_t{0};
    });
    event.addFunc("getKind", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? std::string(eventKindName(value->get().kind)) : std::string{};
    });
    event.addFunc("getTaskId", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().taskId : std::string{};
    });
    event.addFunc("getOwner", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().owner : std::string{};
    });
    event.addFunc("getTaskKind", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().taskKind : std::string{};
    });
    event.addFunc("getProduct", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().product : std::string{};
    });
    event.addFunc("getReason", [resolveEvent](ScriptWorkEvent* v) {
        auto value = resolveEvent(v);
        return value ? value->get().reason : std::string{};
    });

    auto queue =
        table.addClass<ScriptWorkQueue>("WorkQueue", std::function<ScriptWorkQueue*()>([] { return nullptr; }), true);
    queue.addFunc("ownership", [](ScriptWorkQueue*) { return std::string("owned"); });
    queue.addFunc("ownerEpoch", [](ScriptWorkQueue* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    queue.addFunc("handle", [](ScriptWorkQueue* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    queue.addFunc("isStale", [](ScriptWorkQueue* value) { return !value || Production::isStale(value->reference); });
    queue.addFunc("release", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        return eve::script::projectResult(vm, Production::release(value->reference));
    });
    queue.addFunc("enqueue", [vm](ScriptWorkQueue* value, const std::string& owner, const std::string& kind,
                                  const std::string& product, const std::string& contextJson, float duration,
                                  int priority) {
        if (!value)
            return eve::script::projectStatusResult(
                vm,
                productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                               "work queue proxy must not be null", "queue")
                    .status(),
                false, false);
        auto payload = eve::Value::fromJson(contextJson);
        if (!payload.ok()) return eve::script::projectStatusResult(vm, payload.status(), false, false);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(
            vm,
            queueView.value().get().enqueue(owner, kind, product, std::move(payload).takeValue(), duration, priority),
            [](std::string id) { return eve::Value(std::move(id)); });
    });
    queue.addFunc("pause", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().pause(id));
    });
    queue.addFunc("resume", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().resume(id));
    });
    queue.addFunc("cancel", [vm](ScriptWorkQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().cancel(id, reason));
    });
    queue.addFunc("fail", [vm](ScriptWorkQueue* value, const std::string& id, const std::string& reason) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().fail(id, reason));
    });
    queue.addFunc("advance", [vm](ScriptWorkQueue* value, std::int64_t tick, float seconds) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto delta = eve::Duration::fromSeconds(seconds);
        if (!delta.ok()) return eve::script::projectStatusResult(vm, delta.status(), false, false);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(
            vm, queueView.value().get().advance(
                    {eve::SimulationTick(static_cast<std::uint64_t>(tick)), std::move(delta).takeValue()}));
    });
    queue.addFunc("setSlotCount", [vm](ScriptWorkQueue* value, const std::string& owner, int slots) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().setSlotCount(owner, slots));
    });
    queue.addFunc("slotCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading slot count");
            return 0;
        }
        return queueView.value().get().slotCount(owner);
    });
    queue.addFunc("runningCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading running count");
            return 0;
        }
        return queueView.value().get().runningCount(owner);
    });
    queue.addFunc("find", [vm](ScriptWorkQueue* value, const std::string& id) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while finding a task");
            return ssq::Object(vm);
        }
        if (!queueView.value().get().find(id)) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("taskCount", [](ScriptWorkQueue* value) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading task count");
            return 0;
        }
        return queueView.value().get().taskCount();
    });
    queue.addFunc("taskAt", [vm](ScriptWorkQueue* value, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok() || !queueView.value().get().taskAt(index)) {
            queueView.ignore("work task lookup did not produce a task proxy");
            return ssq::Object(vm);
        }
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, queueView.value().get().taskAt(index)->get().id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("ownerTaskCount", [](ScriptWorkQueue* value, const std::string& owner) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading owner task count");
            return 0;
        }
        return queueView.value().get().ownerTaskCount(owner);
    });
    queue.addFunc("ownerTaskAt", [vm](ScriptWorkQueue* value, const std::string& owner, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while finding an owner task");
            return ssq::Object(vm);
        }
        auto taskRef = queueView.value().get().ownerTaskAt(owner, index);
        if (!taskRef) return ssq::Object(vm);
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkTask>(
            vm, std::make_unique<ScriptWorkTask>(value->reference, taskRef->get().id));
        if (!object.ok()) {
            object.ignore("failed to create work task proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("eventCount", [](ScriptWorkQueue* value) {
        auto queueView = value ? Production::resolve(value->reference)
                               : eve::ResultRef<WorkQueue>::failure(eve::Diagnostic::error(
                                     eve::DiagnosticCode::InvalidArgument, "work queue proxy must not be null"));
        if (!queueView.ok()) {
            queueView.ignore("stale work queue while reading event count");
            return 0;
        }
        return queueView.value().get().eventCount();
    });
    queue.addFunc("eventAt", [vm](ScriptWorkQueue* value, int index) {
        if (!value) return ssq::Object(vm);
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok() || !queueView.value().get().eventAt(index)) {
            queueView.ignore("work event lookup did not produce an event proxy");
            return ssq::Object(vm);
        }
        auto object = eve::script::makeOwnedSquirrelInstance<ScriptWorkEvent>(
            vm, std::make_unique<ScriptWorkEvent>(value->reference, index));
        if (!object.ok()) {
            object.ignore("failed to create work event proxy");
            return ssq::Object(vm);
        }
        return std::move(object).takeValue();
    });
    queue.addFunc("clearEvents", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        queueView.value().get().clearEvents();
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });
    queue.addFunc("snapshot", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm,
                productionBindingFailure<std::string>(eve::DiagnosticCode::InvalidArgument,
                                                      "work queue proxy must not be null", "queue"),
                [](std::string text) { return eve::Value(std::move(text)); });
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok())
            return eve::script::projectResult(
                vm,
                productionBindingFailure<std::string>(queueView.status().code() == eve::StatusCode::NotFound
                                                          ? eve::DiagnosticCode::NotFound
                                                          : eve::DiagnosticCode::StaleHandle,
                                                      "work queue handle is stale", "queue"),
                [](std::string text) { return eve::Value(std::move(text)); });
        return eve::script::projectResult(vm, queueView.value().get().snapshot(),
                                          [](std::string text) { return eve::Value(std::move(text)); });
    });
    queue.addFunc("restore", [vm](ScriptWorkQueue* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        return eve::script::projectResult(vm, queueView.value().get().restore(json));
    });
    queue.addFunc("clear", [vm](ScriptWorkQueue* value) {
        if (!value)
            return eve::script::projectResult(
                vm, productionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                   "work queue proxy must not be null", "queue"));
        auto queueView = Production::resolve(value->reference);
        if (!queueView.ok()) return eve::script::projectStatusResult(vm, queueView.status(), false, false);
        queueView.value().get().clear();
        return eve::script::projectResult(vm, eve::Result<void>::success());
    });
    auto cls = table.addClass(name, Production::create, false);
    expose(cls);
}

void Production::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Production::getName);
    cls.addFunc("newWorkQueue", [vm = cls.getHandle()](Production*) -> ssq::Table {
        return makeOwnedProxy<WorkQueueHandleRef, ScriptWorkQueue>(
            vm, Production::newQueueHandle(), [](WorkQueueHandleRef ref) { return Production::release(ref); }, "queue");
    });
}

}  // namespace eve::production
