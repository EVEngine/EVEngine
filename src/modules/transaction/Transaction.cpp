#include "transaction/Transaction.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace eve::transaction {
namespace {

std::string quote(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
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
    if (value.isObject()) {
        auto keys = value.keys();
        std::sort(keys.begin(), keys.end());
        std::string out = "{";
        for (size_t i = 0; i < keys.size(); ++i) {
            if (i) out += ',';
            out += quote(keys[i]) + ':' + canonicalJson(value.get(keys[i].c_str()));
        }
        return out + '}';
    }
    return {};
}

std::string paddedId(const char* prefix, uint64_t value) {
    std::ostringstream out;
    out << prefix << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

bool parseU64(const eve::json::Value& value, uint64_t& out) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        out         = std::stoull(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

bool parseState(const std::string& text, State& state) {
    if (text == "open")
        state = State::Open;
    else if (text == "validated")
        state = State::Validated;
    else if (text == "committed")
        state = State::Committed;
    else if (text == "rolled_back")
        state = State::RolledBack;
    else if (text == "failed")
        state = State::Failed;
    else
        return false;
    return true;
}

}  // namespace

std::string stateName(State state) {
    switch (state) {
        case State::Open: return "open";
        case State::Validated: return "validated";
        case State::Committed: return "committed";
        case State::RolledBack: return "rolled_back";
        case State::Failed: return "failed";
    }
    return "unknown";
}

Plan::Plan(std::string id, std::string correlation, std::string causation)
    : id_(std::move(id)), correlation_(std::move(correlation)), causation_(std::move(causation)) {
    emit("opened");
}

const std::string& Plan::id() const { return id_; }
State              Plan::state() const { return state_; }
const std::string& Plan::correlation() const { return correlation_; }
const std::string& Plan::causation() const { return causation_; }

void Plan::emit(const std::string& type, const std::string& operationId, const std::string& detail) {
    events_.push_back({nextEvent_++, id_, operationId, type, detail});
}

std::string Plan::stage(const std::string& kind, const std::string& target, const std::string& payloadJson) {
    error_.clear();
    if (state_ != State::Open) {
        error_ = "transaction plan is frozen";
        return {};
    }
    if (kind.empty()) {
        error_ = "operation kind must not be empty";
        return {};
    }
    auto document = eve::json::Document::parse(payloadJson);
    if (!document.valid()) {
        error_ = "payload must be valid JSON";
        return {};
    }
    if (nextOperation_ == std::numeric_limits<uint64_t>::max()) {
        error_ = "operation id exhausted";
        return {};
    }
    Operation operation;
    operation.id      = paddedId("operation-", nextOperation_++);
    operation.kind    = kind;
    operation.target  = target;
    operation.payload = canonicalJson(document.root());
    operations_.push_back(std::move(operation));
    emit("staged", operations_.back().id);
    return operations_.back().id;
}

Operation* findMutable(std::deque<Operation>& operations, const std::string& id) {
    auto it = std::find_if(operations.begin(), operations.end(), [&id](const Operation& op) { return op.id == id; });
    return it == operations.end() ? nullptr : &*it;
}

bool Plan::markValid(const std::string& operationId) {
    if (state_ != State::Open) return false;
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return false;
    operation->checked = true;
    operation->valid   = true;
    operation->error.clear();
    emit("operation_valid", operationId);
    return true;
}

bool Plan::markInvalid(const std::string& operationId, const std::string& error) {
    if (state_ != State::Open) return false;
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return false;
    operation->checked = true;
    operation->valid   = false;
    operation->error   = error.empty() ? "invalid operation" : error;
    emit("operation_invalid", operationId, operation->error);
    return true;
}

bool Plan::validate() {
    error_.clear();
    if (state_ != State::Open) return false;
    if (operations_.empty()) {
        error_ = "transaction has no operations";
        emit("validation_failed", {}, error_);
        return false;
    }
    for (const auto& operation : operations_) {
        if (!operation.checked || !operation.valid) {
            error_ = operation.checked ? operation.error : "operation was not validated: " + operation.id;
            emit("validation_failed", operation.id, error_);
            return false;
        }
    }
    state_ = State::Validated;
    emit("validated");
    return true;
}

bool Plan::commit() {
    if (state_ != State::Validated) return false;
    state_ = State::Committed;
    emit("committed");
    return true;
}

bool Plan::rollback(const std::string& reason) {
    if (state_ != State::Open && state_ != State::Validated) return false;
    state_ = State::RolledBack;
    error_ = reason;
    emit("rolled_back", {}, reason);
    return true;
}

bool Plan::fail(const std::string& error) {
    if (state_ == State::Committed || state_ == State::RolledBack || state_ == State::Failed) return false;
    state_ = State::Failed;
    error_ = error.empty() ? "transaction failed" : error;
    emit("failed", {}, error_);
    return true;
}

const std::string& Plan::error() const { return error_; }
int                Plan::operationCount() const { return static_cast<int>(operations_.size()); }
const Operation*   Plan::operationAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < operations_.size() ? &operations_[static_cast<size_t>(index)]
                                                                         : nullptr;
}
const Operation* Plan::findOperation(const std::string& operationId) const {
    auto it = std::find_if(operations_.begin(), operations_.end(),
                           [&operationId](const Operation& operation) { return operation.id == operationId; });
    return it == operations_.end() ? nullptr : &*it;
}
int          Plan::eventCount() const { return static_cast<int>(events_.size()); }
const Event* Plan::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

std::string Plan::snapshotJson() const {
    std::ostringstream out;
    out << "{\"id\":" << quote(id_) << ",\"state\":" << quote(stateName(state_))
        << ",\"correlation\":" << quote(correlation_) << ",\"causation\":" << quote(causation_)
        << ",\"error\":" << quote(error_) << ",\"nextOperation\":" << quote(std::to_string(nextOperation_))
        << ",\"nextEvent\":" << quote(std::to_string(nextEvent_)) << ",\"operations\":[";
    for (size_t i = 0; i < operations_.size(); ++i) {
        if (i) out << ',';
        const auto& op = operations_[i];
        out << "{\"id\":" << quote(op.id) << ",\"kind\":" << quote(op.kind) << ",\"target\":" << quote(op.target)
            << ",\"payload\":" << op.payload << ",\"checked\":" << (op.checked ? "true" : "false")
            << ",\"valid\":" << (op.valid ? "true" : "false") << ",\"error\":" << quote(op.error) << '}';
    }
    out << "],\"events\":[";
    for (size_t i = 0; i < events_.size(); ++i) {
        if (i) out << ',';
        const auto& event = events_[i];
        out << "{\"sequence\":" << quote(std::to_string(event.sequence))
            << ",\"transactionId\":" << quote(event.transactionId) << ",\"operationId\":" << quote(event.operationId)
            << ",\"type\":" << quote(event.type) << ",\"detail\":" << quote(event.detail) << '}';
    }
    return out.str() + "]}";
}

Plan* Ledger::create(const std::string& correlation, const std::string& causation) {
    if (nextTransaction_ == std::numeric_limits<uint64_t>::max()) return nullptr;
    plans_.push_back(
        std::unique_ptr<Plan>(new Plan(paddedId("transaction-", nextTransaction_++), correlation, causation)));
    return plans_.back().get();
}

Plan* Ledger::find(const std::string& transactionId) {
    auto it = std::find_if(plans_.begin(), plans_.end(),
                           [&transactionId](const auto& plan) { return plan->id() == transactionId; });
    return it == plans_.end() ? nullptr : it->get();
}
int   Ledger::count() const { return static_cast<int>(plans_.size()); }
Plan* Ledger::at(int index) {
    return index >= 0 && static_cast<size_t>(index) < plans_.size() ? plans_[static_cast<size_t>(index)].get()
                                                                    : nullptr;
}

std::string Ledger::snapshotJson() const {
    std::ostringstream out;
    out << "{\"version\":1,\"nextTransaction\":" << quote(std::to_string(nextTransaction_)) << ",\"plans\":[";
    for (size_t i = 0; i < plans_.size(); ++i) {
        if (i) out << ',';
        out << plans_[i]->snapshotJson();
    }
    return out.str() + "]}";
}

bool Ledger::restoreJson(const std::string& json) {
    lastError_.clear();
    auto     document     = eve::json::Document::parse(json);
    auto     root         = document.root();
    uint64_t restoredNext = 0;
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1 ||
        !parseU64(root.get("nextTransaction"), restoredNext) || restoredNext == 0 || !root.get("plans").isArray()) {
        lastError_ = "invalid transaction ledger snapshot";
        return false;
    }
    std::vector<std::unique_ptr<Plan>> restored;
    uint64_t                           previousNumeric = 0;
    auto                               plans           = root.get("plans");
    for (size_t i = 0; i < plans.size(); ++i) {
        auto     value = plans.at(i);
        State    state;
        uint64_t nextOperation = 0, nextEvent = 0;
        if (!value.isObject() || !parseState(value.getString("state"), state) ||
            !parseU64(value.get("nextOperation"), nextOperation) || nextOperation == 0 ||
            !parseU64(value.get("nextEvent"), nextEvent) || nextEvent == 0 || !value.get("operations").isArray() ||
            !value.get("events").isArray()) {
            lastError_ = "invalid transaction plan at index " + std::to_string(i);
            return false;
        }
        const std::string id      = value.getString("id");
        const std::string prefix  = "transaction-";
        uint64_t          numeric = 0;
        try {
            numeric = std::stoull(id.substr(prefix.size()));
        } catch (...) {
            numeric = 0;
        }
        if (!id.starts_with(prefix) || numeric <= previousNumeric || numeric >= restoredNext) {
            lastError_ = "invalid transaction id at index " + std::to_string(i);
            return false;
        }
        auto plan = std::unique_ptr<Plan>(new Plan(id, value.getString("correlation"), value.getString("causation")));
        plan->state_ = state;
        plan->error_ = value.getString("error");
        plan->operations_.clear();
        plan->events_.clear();
        auto operations = value.get("operations");
        for (size_t j = 0; j < operations.size(); ++j) {
            auto opValue = operations.at(j);
            if (!opValue.isObject() || opValue.getString("id").empty() || opValue.getString("kind").empty() ||
                !opValue.get("payload") || !opValue.get("checked").isBool() || !opValue.get("valid").isBool()) {
                lastError_ = "invalid operation in plan " + id;
                return false;
            }
            Operation op;
            op.id      = opValue.getString("id");
            op.kind    = opValue.getString("kind");
            op.target  = opValue.getString("target");
            op.payload = canonicalJson(opValue.get("payload"));
            op.checked = opValue.getBool("checked");
            op.valid   = opValue.getBool("valid");
            op.error   = opValue.getString("error");
            plan->operations_.push_back(std::move(op));
        }
        uint64_t previousEvent = 0;
        auto     events        = value.get("events");
        for (size_t j = 0; j < events.size(); ++j) {
            auto  eventValue = events.at(j);
            Event event;
            if (!eventValue.isObject() || !parseU64(eventValue.get("sequence"), event.sequence) ||
                event.sequence <= previousEvent || eventValue.getString("transactionId") != id ||
                eventValue.getString("type").empty()) {
                lastError_ = "invalid event in plan " + id;
                return false;
            }
            event.transactionId = id;
            event.operationId   = eventValue.getString("operationId");
            event.type          = eventValue.getString("type");
            event.detail        = eventValue.getString("detail");
            previousEvent       = event.sequence;
            plan->events_.push_back(std::move(event));
        }
        if ((!plan->events_.empty() && plan->events_.back().sequence >= nextEvent)) {
            lastError_ = "invalid event allocator in plan " + id;
            return false;
        }
        plan->nextOperation_ = nextOperation;
        plan->nextEvent_     = nextEvent;
        previousNumeric      = numeric;
        restored.push_back(std::move(plan));
    }
    plans_           = std::move(restored);
    nextTransaction_ = restoredNext;
    return true;
}

const std::string& Ledger::lastError() const { return lastError_; }

Ledger* Transaction::newLedger() {
    auto* module = Transaction::create();
    module->ledgers_.push_back(std::make_unique<Ledger>());
    return module->ledgers_.back().get();
}

Module_IMPL(Transaction, new Transaction());

void Transaction::expose(ssq::Table& table) {
    auto operation =
        table.addClass<Operation>("TransactionOperation", std::function<Operation*()>([] { return nullptr; }), false);
    operation.addFunc("getId", [](Operation* o) { return o ? o->id : std::string{}; });
    operation.addFunc("getKind", [](Operation* o) { return o ? o->kind : std::string{}; });
    operation.addFunc("getTarget", [](Operation* o) { return o ? o->target : std::string{}; });
    operation.addFunc("getPayload", [](Operation* o) { return o ? o->payload : std::string{}; });
    operation.addFunc("isChecked", [](Operation* o) { return o && o->checked; });
    operation.addFunc("isValid", [](Operation* o) { return o && o->valid; });
    operation.addFunc("getError", [](Operation* o) { return o ? o->error : std::string{}; });

    auto event = table.addClass<Event>("TransactionEvent", std::function<Event*()>([] { return nullptr; }), false);
    event.addFunc("getSequence", [](Event* e) { return e ? static_cast<int64_t>(e->sequence) : int64_t{0}; });
    event.addFunc("getTransactionId", [](Event* e) { return e ? e->transactionId : std::string{}; });
    event.addFunc("getOperationId", [](Event* e) { return e ? e->operationId : std::string{}; });
    event.addFunc("getType", [](Event* e) { return e ? e->type : std::string{}; });
    event.addFunc("getDetail", [](Event* e) { return e ? e->detail : std::string{}; });

    auto plan = table.addClass<Plan>("TransactionPlan", std::function<Plan*()>([] { return nullptr; }), false);
    plan.addFunc("getId", &Plan::id);
    plan.addFunc("getState", [](Plan* p) { return p ? stateName(p->state()) : std::string{}; });
    plan.addFunc("getCorrelation", &Plan::correlation);
    plan.addFunc("getCausation", &Plan::causation);
    plan.addFunc("stage", &Plan::stage);
    plan.addFunc("markValid", &Plan::markValid);
    plan.addFunc("markInvalid", &Plan::markInvalid);
    plan.addFunc("validate", &Plan::validate);
    plan.addFunc("commit", &Plan::commit);
    plan.addFunc("rollback", &Plan::rollback);
    plan.addFunc("fail", &Plan::fail);
    plan.addFunc("getError", &Plan::error);
    plan.addFunc("operationCount", &Plan::operationCount);
    plan.addFunc("operationAt",
                 [](Plan* p, int i) -> Operation* { return p ? const_cast<Operation*>(p->operationAt(i)) : nullptr; });
    plan.addFunc("findOperation", [](Plan* p, const std::string& id) -> Operation* {
        return p ? const_cast<Operation*>(p->findOperation(id)) : nullptr;
    });
    plan.addFunc("eventCount", &Plan::eventCount);
    plan.addFunc("eventAt", [](Plan* p, int i) -> Event* { return p ? const_cast<Event*>(p->eventAt(i)) : nullptr; });
    plan.addFunc("snapshotJson", &Plan::snapshotJson);

    auto ledger = table.addClass<Ledger>("TransactionLedger", std::function<Ledger*()>([] { return nullptr; }), false);
    ledger.addFunc("create", &Ledger::create);
    ledger.addFunc("find", &Ledger::find);
    ledger.addFunc("count", &Ledger::count);
    ledger.addFunc("at", &Ledger::at);
    ledger.addFunc("snapshotJson", &Ledger::snapshotJson);
    ledger.addFunc("restoreJson", &Ledger::restoreJson);
    ledger.addFunc("lastError", &Ledger::lastError);

    auto cls = table.addClass(name, Transaction::create, false);
    expose(cls);
}

void Transaction::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Transaction::getName);
    cls.addFunc("newLedger", [](Transaction*) { return Transaction::newLedger(); });
}

}  // namespace eve::transaction
