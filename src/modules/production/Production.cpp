#include "production/Production.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace eve::production {
namespace {

std::string quote(const std::string& value) {
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

bool normalize(const std::string& input, std::string& output) {
    auto doc = eve::json::Document::parse(input);
    if (!doc.valid()) return false;
    output = canonicalJson(doc.root());
    return true;
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

bool terminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Cancelled || state == TaskState::Failed;
}

}  // namespace

std::string taskStateName(TaskState state) {
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

std::string eventKindName(ProductionEventKind kind) {
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

void ProductionQueue::emit(ProductionEventKind kind, const ProductionTask& task, const std::string& reason) {
    events_.push_back({nextEventSequence_++, kind, task.id, task.owner, task.kind, task.product, reason});
}

int ProductionQueue::slotCount(const std::string& owner) const {
    const auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                                     [](const auto& entry, const std::string& key) { return entry.first < key; });
    return it != slots_.end() && it->first == owner ? it->second : 1;
}

int ProductionQueue::runningCount(const std::string& owner) const {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& task) {
        return task->owner == owner && task->state == TaskState::Running;
    }));
}

void ProductionQueue::schedule(const std::string& owner) {
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

std::string ProductionQueue::enqueue(const std::string& owner, const std::string& kind, const std::string& product,
                                     const std::string& contextJson, double duration, int priority) {
    std::string context;
    if (owner.empty() || kind.empty() || product.empty() || !std::isfinite(duration) || duration <= 0.0 ||
        !normalize(contextJson, context))
        return {};
    auto               task = std::make_unique<ProductionTask>();
    std::ostringstream id;
    id << "task-" << std::setw(16) << std::setfill('0') << nextTaskId_++;
    task->id                 = id.str();
    task->owner              = owner;
    task->kind               = kind;
    task->product            = product;
    task->contextJson        = context;
    task->duration           = duration;
    task->priority           = priority;
    task->enqueueSequence    = nextEnqueueSequence_++;
    const std::string result = task->id;
    tasks_.push_back(std::move(task));
    emit(ProductionEventKind::Enqueued, *tasks_.back());
    schedule(owner);
    return result;
}

ProductionTask* ProductionQueue::find(const std::string& taskId) {
    const auto it =
        std::find_if(tasks_.begin(), tasks_.end(), [&taskId](const auto& task) { return task->id == taskId; });
    return it == tasks_.end() ? nullptr : it->get();
}

bool ProductionQueue::pause(const std::string& taskId) {
    auto* task = find(taskId);
    if (!task || (task->state != TaskState::Queued && task->state != TaskState::Running)) return false;
    task->state = TaskState::Paused;
    emit(ProductionEventKind::Paused, *task);
    schedule(task->owner);
    return true;
}

bool ProductionQueue::resume(const std::string& taskId) {
    auto* task = find(taskId);
    if (!task || task->state != TaskState::Paused) return false;
    task->state = TaskState::Queued;
    emit(ProductionEventKind::Resumed, *task);
    schedule(task->owner);
    return true;
}

bool ProductionQueue::cancel(const std::string& taskId, const std::string& reason) {
    auto* task = find(taskId);
    if (!task || terminal(task->state)) return false;
    task->state  = TaskState::Cancelled;
    task->reason = reason;
    emit(ProductionEventKind::Cancelled, *task, reason);
    schedule(task->owner);
    return true;
}

bool ProductionQueue::fail(const std::string& taskId, const std::string& reason) {
    auto* task = find(taskId);
    if (!task || terminal(task->state)) return false;
    task->state  = TaskState::Failed;
    task->reason = reason;
    emit(ProductionEventKind::Failed, *task, reason);
    schedule(task->owner);
    return true;
}

void ProductionQueue::update(double dt, double speedMultiplier) {
    if (!std::isfinite(dt) || !std::isfinite(speedMultiplier) || dt <= 0.0 || speedMultiplier < 0.0) return;
    const double          advance = dt * speedMultiplier;
    std::set<std::string> owners;
    for (auto& task : tasks_) {
        if (task->state != TaskState::Running) continue;
        task->progress = std::min(task->duration, task->progress + advance);
        if (task->progress >= task->duration) {
            task->state = TaskState::Completed;
            emit(ProductionEventKind::Completed, *task);
            owners.insert(task->owner);
        }
    }
    for (const auto& owner : owners) schedule(owner);
}

bool ProductionQueue::setSlotCount(const std::string& owner, int slots) {
    if (owner.empty() || slots < 0) return false;
    auto it = std::lower_bound(slots_.begin(), slots_.end(), owner,
                               [](const auto& entry, const std::string& key) { return entry.first < key; });
    if (it != slots_.end() && it->first == owner)
        it->second = slots;
    else
        slots_.insert(it, {owner, slots});
    schedule(owner);
    return true;
}

int             ProductionQueue::taskCount() const { return static_cast<int>(tasks_.size()); }
ProductionTask* ProductionQueue::taskAt(int index) {
    return index < 0 || static_cast<size_t>(index) >= tasks_.size() ? nullptr
                                                                    : tasks_[static_cast<size_t>(index)].get();
}
int ProductionQueue::ownerTaskCount(const std::string& owner) const {
    return static_cast<int>(
        std::count_if(tasks_.begin(), tasks_.end(), [&owner](const auto& t) { return t->owner == owner; }));
}
ProductionTask* ProductionQueue::ownerTaskAt(const std::string& owner, int index) {
    if (index < 0) return nullptr;
    for (auto& task : tasks_)
        if (task->owner == owner && index-- == 0) return task.get();
    return nullptr;
}
int              ProductionQueue::eventCount() const { return static_cast<int>(events_.size()); }
ProductionEvent* ProductionQueue::eventAt(int index) {
    return index < 0 || static_cast<size_t>(index) >= events_.size() ? nullptr : &events_[static_cast<size_t>(index)];
}
void               ProductionQueue::clearEvents() { events_.clear(); }
const std::string& ProductionQueue::lastError() const { return lastError_; }

std::string ProductionQueue::snapshot() const {
    std::ostringstream out;
    out << "{\"nextEnqueueSequence\":" << quote(std::to_string(nextEnqueueSequence_))
        << ",\"nextEventSequence\":" << quote(std::to_string(nextEventSequence_))
        << ",\"nextTaskId\":" << quote(std::to_string(nextTaskId_)) << ",\"slots\":[";
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
            << ",\"taskKind\":" << quote(event.taskKind) << '}';
    }
    out << "],\"tasks\":[";
    for (size_t i = 0; i < tasks_.size(); ++i) {
        if (i) out << ',';
        const auto& t = *tasks_[i];
        out << "{\"context\":" << t.contextJson << ",\"duration\":" << std::setprecision(17) << t.duration
            << ",\"enqueueSequence\":" << quote(std::to_string(t.enqueueSequence)) << ",\"id\":" << quote(t.id)
            << ",\"kind\":" << quote(t.kind) << ",\"owner\":" << quote(t.owner) << ",\"priority\":" << t.priority
            << ",\"product\":" << quote(t.product) << ",\"progress\":" << t.progress
            << ",\"reason\":" << quote(t.reason) << ",\"state\":" << quote(taskStateName(t.state)) << '}';
    }
    return out.str() + "]}";
}

bool ProductionQueue::restore(const std::string& json) {
    std::string error;
    auto        doc = eve::json::Document::parse(json, &error);
    if (!doc.valid() || !doc.root().isObject()) {
        lastError_ = error.empty() ? "snapshot must be an object" : error;
        return false;
    }
    ProductionQueue candidate;
    const auto      root = doc.root();
    if (!parseU64(root.get("nextTaskId"), candidate.nextTaskId_) ||
        !parseU64(root.get("nextEnqueueSequence"), candidate.nextEnqueueSequence_) ||
        !parseU64(root.get("nextEventSequence"), candidate.nextEventSequence_) || !root.get("slots").isArray() ||
        !root.get("events").isArray() || !root.get("tasks").isArray()) {
        lastError_ = "invalid snapshot counters or arrays";
        return false;
    }
    for (size_t i = 0; i < root.get("slots").size(); ++i) {
        auto value = root.get("slots").at(i);
        if (!value.isObject() || !value.get("owner").isString() || !value.get("value").isNumber()) {
            lastError_ = "invalid slot entry";
            return false;
        }
        int slots = value.get("value").asInt();
        if (!candidate.setSlotCount(value.get("owner").asString(), slots)) {
            lastError_ = "invalid slot entry";
            return false;
        }
    }
    std::set<std::string> ids;
    for (size_t i = 0; i < root.get("tasks").size(); ++i) {
        auto      value    = root.get("tasks").at(i);
        auto      task     = std::make_unique<ProductionTask>();
        uint64_t  sequence = 0;
        TaskState state;
        if (!value.isObject() || !value.get("id").isString() || !value.get("owner").isString() ||
            !value.get("kind").isString() || !value.get("product").isString() || !value.get("duration").isNumber() ||
            !value.get("progress").isNumber() || !value.get("priority").isNumber() || !value.get("state").isString() ||
            !value.get("reason").isString() || !parseU64(value.get("enqueueSequence"), sequence) ||
            !parseState(value.get("state").asString(), state) || value.get("id").asString().empty() ||
            value.get("owner").asString().empty() || value.get("kind").asString().empty() ||
            value.get("product").asString().empty() || !ids.insert(value.get("id").asString()).second) {
            lastError_ = "invalid task entry";
            return false;
        }
        task->id              = value.get("id").asString();
        task->owner           = value.get("owner").asString();
        task->kind            = value.get("kind").asString();
        task->product         = value.get("product").asString();
        task->contextJson     = canonicalJson(value.get("context"));
        task->duration        = value.get("duration").asDouble();
        task->progress        = value.get("progress").asDouble();
        task->priority        = value.get("priority").asInt();
        task->state           = state;
        task->enqueueSequence = sequence;
        task->reason          = value.get("reason").asString();
        if (!std::isfinite(task->duration) || task->duration <= 0 || !std::isfinite(task->progress) ||
            task->progress < 0 || task->progress > task->duration) {
            lastError_ = "invalid task progress";
            return false;
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
            lastError_ = "invalid event entry";
            return false;
        }
        previousEventSequence = event.sequence;
        event.owner           = value.get("owner").asString();
        event.product         = value.get("product").asString();
        event.reason          = value.get("reason").asString();
        event.taskId          = value.get("taskId").asString();
        event.taskKind        = value.get("taskKind").asString();
        candidate.events_.push_back(std::move(event));
    }
    *this = std::move(candidate);
    lastError_.clear();
    return true;
}

void ProductionQueue::clear() {
    tasks_.clear();
    events_.clear();
    slots_.clear();
    lastError_.clear();
    nextTaskId_ = nextEnqueueSequence_ = nextEventSequence_ = 1;
}

ProductionQueue* Production::newQueue() {
    Production* module = Production::create();
    module->queues_.push_back(std::make_unique<ProductionQueue>());
    return module->queues_.back().get();
}

Module_IMPL(Production, new Production());

void Production::expose(ssq::Table& table) {
    auto task = table.addClass<ProductionTask>("ProductionTask",
                                               std::function<ProductionTask*()>([] { return nullptr; }), false);
    task.addFunc("getId", [](ProductionTask* v) { return v ? v->id : std::string{}; });
    task.addFunc("getOwner", [](ProductionTask* v) { return v ? v->owner : std::string{}; });
    task.addFunc("getKind", [](ProductionTask* v) { return v ? v->kind : std::string{}; });
    task.addFunc("getProduct", [](ProductionTask* v) { return v ? v->product : std::string{}; });
    task.addFunc("getContextJson", [](ProductionTask* v) { return v ? v->contextJson : std::string{}; });
    task.addFunc("getDuration", [](ProductionTask* v) { return v ? float(v->duration) : 0.0f; });
    task.addFunc("getProgress", [](ProductionTask* v) { return v ? float(v->progress) : 0.0f; });
    task.addFunc("getPriority", [](ProductionTask* v) { return v ? v->priority : 0; });
    task.addFunc("getState", [](ProductionTask* v) { return v ? taskStateName(v->state) : std::string{}; });
    task.addFunc("getReason", [](ProductionTask* v) { return v ? v->reason : std::string{}; });
    auto event = table.addClass<ProductionEvent>("ProductionEvent",
                                                 std::function<ProductionEvent*()>([] { return nullptr; }), false);
    event.addFunc("getSequence", [](ProductionEvent* v) { return v ? int64_t(v->sequence) : int64_t{0}; });
    event.addFunc("getKind", [](ProductionEvent* v) { return v ? eventKindName(v->kind) : std::string{}; });
    event.addFunc("getTaskId", [](ProductionEvent* v) { return v ? v->taskId : std::string{}; });
    event.addFunc("getOwner", [](ProductionEvent* v) { return v ? v->owner : std::string{}; });
    event.addFunc("getTaskKind", [](ProductionEvent* v) { return v ? v->taskKind : std::string{}; });
    event.addFunc("getProduct", [](ProductionEvent* v) { return v ? v->product : std::string{}; });
    event.addFunc("getReason", [](ProductionEvent* v) { return v ? v->reason : std::string{}; });
    auto queue = table.addClass<ProductionQueue>("ProductionQueue",
                                                 std::function<ProductionQueue*()>([] { return nullptr; }), false);
    queue.addFunc("enqueue", [](ProductionQueue* q, const std::string& owner, const std::string& kind,
                                const std::string& product, const std::string& context, float duration, int priority) {
        return q ? q->enqueue(owner, kind, product, context, duration, priority) : std::string{};
    });
    queue.addFunc("pause", &ProductionQueue::pause);
    queue.addFunc("resume", &ProductionQueue::resume);
    queue.addFunc("cancel", &ProductionQueue::cancel);
    queue.addFunc("fail", &ProductionQueue::fail);
    queue.addFunc("update", [](ProductionQueue* q, float dt, float speed) {
        if (q) q->update(dt, speed);
    });
    queue.addFunc("setSlotCount", &ProductionQueue::setSlotCount);
    queue.addFunc("slotCount", &ProductionQueue::slotCount);
    queue.addFunc("runningCount", &ProductionQueue::runningCount);
    queue.addFunc("find", &ProductionQueue::find);
    queue.addFunc("taskCount", &ProductionQueue::taskCount);
    queue.addFunc("taskAt", &ProductionQueue::taskAt);
    queue.addFunc("ownerTaskCount", &ProductionQueue::ownerTaskCount);
    queue.addFunc("ownerTaskAt", &ProductionQueue::ownerTaskAt);
    queue.addFunc("eventCount", &ProductionQueue::eventCount);
    queue.addFunc("eventAt", &ProductionQueue::eventAt);
    queue.addFunc("clearEvents", &ProductionQueue::clearEvents);
    queue.addFunc("snapshot", &ProductionQueue::snapshot);
    queue.addFunc("restore", &ProductionQueue::restore);
    queue.addFunc("lastError", &ProductionQueue::lastError);
    queue.addFunc("clear", &ProductionQueue::clear);
    auto cls = table.addClass(name, Production::create, false);
    expose(cls);
}

void Production::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Production::getName);
    cls.addFunc("newQueue", [](Production*) { return Production::newQueue(); });
}

}  // namespace eve::production
