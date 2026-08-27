#include "transaction/Transaction.h"

#include "common/Json.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <exception>
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

template <class T>
eve::Result<T> transactionBindingFailure(eve::DiagnosticCode code, std::string message,
                                         std::string path = {}) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "transaction"));
}

eve::Value planBindingValue(const Plan* plan) {
    if (!plan) return eve::Value();
    return eve::Value(eve::Value::Object{
        {"id", plan->id()},
        {"identity", plan->identity().isNil() ? std::string{} : plan->identity().format()},
        {"state", stateName(plan->state())},
        {"correlation", plan->correlation()},
        {"causation", plan->causation()},
    });
}

eve::LogicalId transactionSchema() {
    const auto schema = eve::LogicalId::parse("transaction:ledger");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& transactionMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto registration = result.add(
            transactionSchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
            [](const eve::Value& payload) -> eve::Result<eve::Value> {
                const auto* object = payload.getIf<eve::Value::Object>();
                if (!object)
                    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "transaction ledger payload must be an object"));
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

namespace {

template <class Call>
eve::Result<void> invokeParticipant(Call&& call, std::string_view phase, const ITransactionParticipant& participant) {
    try {
        return std::forward<Call>(call)();
    } catch (const std::exception& exception) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed,
            std::string(phase) + " threw for participant " + std::string(participant.name()) + ": " + exception.what(),
            "transaction." + std::string(phase)));
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed,
            std::string(phase) + " threw an unknown exception for participant " + std::string(participant.name()),
            "transaction." + std::string(phase)));
    }
}

void appendFailure(std::vector<eve::Diagnostic>& diagnostics, const eve::Status& status, std::string_view phase,
                   size_t index, const ITransactionParticipant& participant) {
    const auto& original = status.diagnostics();
    diagnostics.insert(diagnostics.end(), original.begin(), original.end());
    diagnostics.push_back(eve::Diagnostic::error(
        eve::DiagnosticCode::Failed,
        std::string(phase) + " failed for participant " + std::string(participant.name()),
        "transaction." + std::string(phase) + "[" + std::to_string(index) + "]"));
}

eve::Result<TransactionReceipt> coordinatorFailure(eve::StatusCode code, std::vector<eve::Diagnostic> diagnostics) {
    if (diagnostics.empty())
        diagnostics.push_back(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "transaction participant lifecycle failed"));
    return eve::Result<TransactionReceipt>::failure(eve::Status(code, std::move(diagnostics)));
}

}  // namespace

Ledger::Ledger(eve::PersistentId instanceId, eve::UuidEntropySource transactionEntropy,
               eve::UuidClock transactionClock)
    : instanceId_(instanceId), transactionEntropy_(std::move(transactionEntropy)),
      transactionClock_(std::move(transactionClock)) {
    if (transactionEntropy_)
        transactionIdGenerator_.emplace(transactionEntropy_, transactionClock_);
}

eve::Result<TransactionReceipt> Coordinator::execute(
    const TransactionContext& context, std::span<ITransactionParticipant*> participants) const {
    if (context.transactionId().empty())
        return coordinatorFailure(
            eve::StatusCode::Rejected,
            {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transaction id must not be empty",
                                    "transactionId")});
    if (participants.empty())
        return coordinatorFailure(
            eve::StatusCode::Rejected,
            {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                    "transaction requires at least one participant", "participants")});
    for (size_t i = 0; i < participants.size(); ++i) {
        if (!participants[i])
            return coordinatorFailure(
                eve::StatusCode::Rejected,
                {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "participant must not be null",
                                        "participants[" + std::to_string(i) + "]")});
        for (size_t previous = 0; previous < i; ++previous)
            if (participants[previous] == participants[i])
                return coordinatorFailure(
                    eve::StatusCode::Rejected,
                    {eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                            "a participant may occur only once in a transaction",
                                            "participants[" + std::to_string(i) + "]")});
    }

    TransactionReceipt receipt;
    receipt.identity          = context.identity();
    receipt.transactionId    = context.transactionId();
    receipt.correlationId    = context.correlationId();
    receipt.causationId      = context.causationId();
    receipt.participantCount = participants.size();

    std::vector<size_t> prepared;
    prepared.reserve(participants.size());
    for (size_t i = 0; i < participants.size(); ++i) {
        auto result = invokeParticipant(
            [&] { return participants[i]->prepare(context); }, "prepare", *participants[i]);
        if (!result) {
            std::vector<eve::Diagnostic> diagnostics;
            appendFailure(diagnostics, result.status(), "prepare", i, *participants[i]);
            bool cleanupFailed = false;
            for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
                auto rollback = invokeParticipant(
                    [&] { return participants[*it]->rollback(context); }, "rollback", *participants[*it]);
                if (!rollback) {
                    cleanupFailed = true;
                    appendFailure(diagnostics, rollback.status(), "rollback", *it, *participants[*it]);
                }
            }
            return coordinatorFailure(cleanupFailed ? eve::StatusCode::Failed : result.code(),
                                      std::move(diagnostics));
        }
        prepared.push_back(i);
    }
    receipt.preparedCount = prepared.size();

    std::vector<size_t> committed;
    committed.reserve(prepared.size());
    for (size_t i : prepared) {
        auto result = invokeParticipant(
            [&] { return participants[i]->commit(context); }, "commit", *participants[i]);
        if (!result) {
            std::vector<eve::Diagnostic> diagnostics;
            appendFailure(diagnostics, result.status(), "commit", i, *participants[i]);
            bool cleanupFailed = false;

            // A failed commit is contractually still uncommitted. Roll it and
            // every later prepared participant back before compensating effects
            // that were already made observable by earlier commits.
            for (auto it = prepared.rbegin(); it != prepared.rend(); ++it) {
                if (std::find(committed.begin(), committed.end(), *it) != committed.end()) continue;
                auto rollback = invokeParticipant(
                    [&] { return participants[*it]->rollback(context); }, "rollback", *participants[*it]);
                if (!rollback) {
                    cleanupFailed = true;
                    appendFailure(diagnostics, rollback.status(), "rollback", *it, *participants[*it]);
                }
            }
            for (auto it = committed.rbegin(); it != committed.rend(); ++it) {
                auto compensation = invokeParticipant(
                    [&] { return participants[*it]->compensate(context); }, "compensation", *participants[*it]);
                if (!compensation) {
                    cleanupFailed = true;
                    appendFailure(diagnostics, compensation.status(), "compensation", *it, *participants[*it]);
                }
            }
            return coordinatorFailure(cleanupFailed ? eve::StatusCode::Failed : result.code(),
                                      std::move(diagnostics));
        }
        committed.push_back(i);
    }

    receipt.committedCount = committed.size();
    receipt.state          = CoordinatorState::Committed;
    return eve::Result<TransactionReceipt>::success(std::move(receipt), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<TransactionReceipt> Coordinator::compensate(
    const TransactionContext& context, std::span<ITransactionParticipant*> participants) const {
    if (context.transactionId().empty())
        return coordinatorFailure(
            eve::StatusCode::Rejected,
            {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "transaction id must not be empty",
                                    "transactionId")});
    if (participants.empty())
        return coordinatorFailure(
            eve::StatusCode::Rejected,
            {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                    "compensation requires at least one participant", "participants")});
    for (size_t i = 0; i < participants.size(); ++i) {
        if (!participants[i])
            return coordinatorFailure(
                eve::StatusCode::Rejected,
                {eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "participant must not be null",
                                        "participants[" + std::to_string(i) + "]")});
        for (size_t previous = 0; previous < i; ++previous)
            if (participants[previous] == participants[i])
                return coordinatorFailure(
                    eve::StatusCode::Rejected,
                    {eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                            "a participant may occur only once in compensation",
                                            "participants[" + std::to_string(i) + "]")});
    }

    TransactionReceipt receipt;
    receipt.identity          = context.identity();
    receipt.transactionId    = context.transactionId();
    receipt.correlationId    = context.correlationId();
    receipt.causationId      = context.causationId();
    receipt.participantCount = participants.size();
    std::vector<eve::Diagnostic> diagnostics;
    for (size_t offset = 0; offset < participants.size(); ++offset) {
        const size_t index = participants.size() - 1 - offset;
        auto result = invokeParticipant(
            [&] { return participants[index]->compensate(context); }, "compensation", *participants[index]);
        if (!result) {
            appendFailure(diagnostics, result.status(), "compensation", index, *participants[index]);
            continue;
        }
        ++receipt.compensatedCount;
    }
    if (!diagnostics.empty()) {
        receipt.state = CoordinatorState::CompensationFailed;
        return coordinatorFailure(eve::StatusCode::Failed, std::move(diagnostics));
    }
    receipt.state = CoordinatorState::Compensated;
    return eve::Result<TransactionReceipt>::success(std::move(receipt),
                                                    eve::Status::success(eve::StatusCode::Applied));
}

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

Plan::Plan(std::string id, std::string correlation, std::string causation, eve::TransactionId identity,
           eve::UuidEntropySource operationEntropy, eve::UuidClock operationClock)
    : id_(std::move(id)), identity_(identity), correlation_(std::move(correlation)),
      causation_(std::move(causation)) {
    if (operationEntropy)
        operationIdGenerator_.emplace(std::move(operationEntropy), std::move(operationClock));
    emit("opened");
}

const std::string& Plan::id() const { return id_; }
const eve::TransactionId& Plan::identity() const noexcept { return identity_; }
State              Plan::state() const { return state_; }
const std::string& Plan::correlation() const { return correlation_; }
const std::string& Plan::causation() const { return causation_; }

void Plan::emit(const std::string& type, const std::string& operationId, const std::string& detail) {
    Event event;
    event.sequence          = nextEvent_++;
    event.transactionId     = id_;
    event.operationId       = operationId;
    event.type              = type;
    event.detail            = detail;
    event.transactionIdentity = identity_;
    if (const auto parsed = eve::OperationId::parse(operationId)) event.operationIdentity = *parsed;
    events_.push_back(std::move(event));
}

eve::Result<eve::OperationId> Plan::stage(const std::string& kind, const std::string& target,
                                          const std::string& payloadJson, eve::OperationId operationId) {
    const auto failure = [](eve::DiagnosticCode code, std::string message) {
        return eve::Result<eve::OperationId>::failure(eve::Diagnostic::error(code, std::move(message)));
    };
    error_.clear();
    if (state_ != State::Open) return failure(eve::DiagnosticCode::Conflict, "transaction plan is frozen");
    if (kind.empty()) return failure(eve::DiagnosticCode::InvalidArgument, "operation kind must not be empty");
    auto document = eve::json::Document::parse(payloadJson);
    if (!document.valid()) return failure(eve::DiagnosticCode::ParseError, "payload must be valid JSON");
    if (nextOperation_ == std::numeric_limits<uint64_t>::max())
        return failure(eve::DiagnosticCode::Failed, "operation sequence exhausted");

    if (operationId.isNil()) {
        if (!operationIdGenerator_)
            return failure(eve::DiagnosticCode::Unsupported,
                           "canonical operation identity requires an injected UUID entropy source");
        const auto generated = operationIdGenerator_->generate();
        if (!generated)
            return failure(eve::DiagnosticCode::Failed, "canonical operation identity generation failed");
        operationId = eve::OperationId::fromUuid(*generated);
    }
    if (findOperation(operationId))
        return failure(eve::DiagnosticCode::Conflict, "operation identity already exists in this plan");

    Operation operation;
    operation.identity = operationId;
    operation.id       = operationId.format();
    operation.kind     = kind;
    operation.target   = target;
    operation.payload  = canonicalJson(document.root());
    operations_.push_back(std::move(operation));
    ++nextOperation_;
    emit("staged", operations_.back().id);
    return eve::Result<eve::OperationId>::success(
        operationId, eve::Status::success(eve::StatusCode::Applied));
}

Operation* findMutable(std::deque<Operation>& operations, const std::string& id) {
    auto it = std::find_if(operations.begin(), operations.end(), [&id](const Operation& op) { return op.id == id; });
    return it == operations.end() ? nullptr : &*it;
}

Operation* findMutable(std::deque<Operation>& operations, eve::OperationId id) {
    auto it = std::find_if(operations.begin(), operations.end(), [id](const Operation& op) {
        return !id.isNil() && op.identity == id;
    });
    return it == operations.end() ? nullptr : &*it;
}

template <class T = void>
eve::Result<T> planFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), {}, {}, "transaction"));
}

eve::Result<void> Plan::markValid(const std::string& operationId) {
    if (state_ != State::Open) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not open");
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return planFailure(eve::DiagnosticCode::NotFound, "operation was not found");
    operation->checked = true;
    operation->valid   = true;
    operation->error.clear();
    emit("operation_valid", operationId);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::markValid(eve::OperationId operationId) {
    if (state_ != State::Open) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not open");
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return planFailure(eve::DiagnosticCode::NotFound, "operation was not found");
    operation->checked = true;
    operation->valid   = true;
    operation->error.clear();
    emit("operation_valid", operation->id);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::markInvalid(const std::string& operationId, const std::string& error) {
    if (state_ != State::Open) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not open");
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return planFailure(eve::DiagnosticCode::NotFound, "operation was not found");
    operation->checked = true;
    operation->valid   = false;
    operation->error   = error.empty() ? "invalid operation" : error;
    emit("operation_invalid", operationId, operation->error);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::markInvalid(eve::OperationId operationId, const std::string& error) {
    if (state_ != State::Open) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not open");
    Operation* operation = findMutable(operations_, operationId);
    if (!operation) return planFailure(eve::DiagnosticCode::NotFound, "operation was not found");
    operation->checked = true;
    operation->valid   = false;
    operation->error   = error.empty() ? "invalid operation" : error;
    emit("operation_invalid", operation->id, operation->error);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::validate() {
    error_.clear();
    if (state_ != State::Open) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not open");
    if (operations_.empty()) {
        error_ = "transaction has no operations";
        emit("validation_failed", {}, error_);
        return planFailure(eve::DiagnosticCode::InvalidArgument, error_);
    }
    for (const auto& operation : operations_) {
        if (!operation.checked || !operation.valid) {
            error_ = operation.checked ? operation.error : "operation was not validated: " + operation.id;
            emit("validation_failed", operation.id, error_);
            return planFailure(eve::DiagnosticCode::InvalidArgument, error_);
        }
    }
    state_ = State::Validated;
    emit("validated");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::commit() {
    if (state_ != State::Validated) return planFailure(eve::DiagnosticCode::Conflict, "transaction is not validated");
    state_ = State::Committed;
    emit("committed");
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::rollback(const std::string& reason) {
    if (state_ != State::Open && state_ != State::Validated)
        return planFailure(eve::DiagnosticCode::Conflict, "transaction cannot be rolled back in its current state");
    state_ = State::RolledBack;
    error_ = reason;
    emit("rolled_back", {}, reason);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> Plan::fail(const std::string& error) {
    if (state_ == State::Committed || state_ == State::RolledBack || state_ == State::Failed)
        return planFailure(eve::DiagnosticCode::Conflict, "transaction is already terminal");
    state_ = State::Failed;
    error_ = error.empty() ? "transaction failed" : error;
    emit("failed", {}, error_);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
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
const Operation* Plan::findOperation(eve::OperationId operationId) const {
    auto it = std::find_if(operations_.begin(), operations_.end(), [operationId](const Operation& operation) {
        return !operationId.isNil() && operation.identity == operationId;
    });
    return it == operations_.end() ? nullptr : &*it;
}
int          Plan::eventCount() const { return static_cast<int>(events_.size()); }
const Event* Plan::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

std::string Plan::snapshotJson() const {
    std::ostringstream out;
    out << "{\"id\":" << quote(id_);
    if (!identity_.isNil()) out << ",\"identity\":" << quote(identity_.format());
    out << ",\"state\":" << quote(stateName(state_))
        << ",\"correlation\":" << quote(correlation_) << ",\"causation\":" << quote(causation_)
        << ",\"error\":" << quote(error_) << ",\"nextOperation\":" << quote(std::to_string(nextOperation_))
        << ",\"nextEvent\":" << quote(std::to_string(nextEvent_)) << ",\"operations\":[";
    for (size_t i = 0; i < operations_.size(); ++i) {
        if (i) out << ',';
        const auto& op = operations_[i];
        out << "{\"id\":" << quote(op.id);
        if (!op.identity.isNil()) out << ",\"identity\":" << quote(op.identity.format());
        out << ",\"kind\":" << quote(op.kind) << ",\"target\":" << quote(op.target)
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

eve::Result<Plan*> Ledger::create(const std::string& correlation, const std::string& causation,
                                  eve::TransactionId identity) {
    const auto failure = [](eve::DiagnosticCode code, std::string message) {
        return eve::Result<Plan*>::failure(eve::Diagnostic::error(code, std::move(message)));
    };
    if (identity.isNil()) {
        if (!transactionIdGenerator_)
            return failure(eve::DiagnosticCode::Unsupported,
                           "canonical transaction identity requires an injected UUID entropy source");
        const auto generated = transactionIdGenerator_->generate();
        if (!generated)
            return failure(eve::DiagnosticCode::Failed, "canonical transaction identity generation failed");
        identity = eve::TransactionId::fromUuid(*generated);
    }
    if (find(identity)) return failure(eve::DiagnosticCode::Conflict, "transaction identity already exists");
    plans_.push_back(std::unique_ptr<Plan>(new Plan(
        identity.format(), correlation, causation, identity, transactionEntropy_, transactionClock_)));
    return eve::Result<Plan*>::success(plans_.back().get(), eve::Status::success(eve::StatusCode::Applied));
}

Plan* Ledger::find(const std::string& transactionId) {
    auto it = std::find_if(plans_.begin(), plans_.end(),
                           [&transactionId](const auto& plan) { return plan->id() == transactionId; });
    return it == plans_.end() ? nullptr : it->get();
}
Plan* Ledger::find(eve::TransactionId transactionId) {
    if (transactionId.isNil()) return nullptr;
    auto it = std::find_if(plans_.begin(), plans_.end(), [transactionId](const auto& plan) {
        return plan->identity() == transactionId;
    });
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

eve::Result<void> Ledger::restore(std::string_view json) {
    const auto failure = [](eve::DiagnosticCode code, std::string message) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(code, std::move(message), "transaction.ledger"));
    };
    auto     document     = eve::json::Document::parse(std::string(json));
    auto     root         = document.root();
    uint64_t restoredNext = 0;
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1 ||
        !parseU64(root.get("nextTransaction"), restoredNext) || restoredNext == 0 ||
        !root.get("plans").isArray()) {
        return failure(eve::DiagnosticCode::ParseError, "invalid transaction ledger snapshot");
    }
    std::vector<std::unique_ptr<Plan>> restored;
    uint64_t previousNumeric = 0;
    std::vector<std::string> restoredPlanIds;
    auto plans = root.get("plans");
    for (size_t i = 0; i < plans.size(); ++i) {
        auto     value = plans.at(i);
        State    state = State::Open;
        uint64_t nextOperation = 0, nextEvent = 0;
        if (!value.isObject() || !parseState(value.getString("state"), state) ||
            !parseU64(value.get("nextOperation"), nextOperation) || nextOperation == 0 ||
            !parseU64(value.get("nextEvent"), nextEvent) || nextEvent == 0 ||
            !value.get("operations").isArray() || !value.get("events").isArray()) {
            return failure(eve::DiagnosticCode::ParseError,
                           "invalid transaction plan at index " + std::to_string(i));
        }
        const std::string id = value.getString("id");
        const std::string identityText = value.getString("identity");
        eve::TransactionId identity;
        bool canonicalIdentity = false;
        if (!identityText.empty()) {
            const auto parsed = eve::TransactionId::parse(identityText);
            if (!parsed || parsed->isNil() || id != parsed->format()) {
                return failure(eve::DiagnosticCode::ParseError,
                               "invalid canonical transaction id at index " + std::to_string(i));
            }
            identity = *parsed;
            canonicalIdentity = true;
        } else if (const auto parsed = eve::TransactionId::parse(id); parsed && !parsed->isNil()) {
            identity = *parsed;
            canonicalIdentity = true;
        }

        uint64_t numeric = 0;
        if (!canonicalIdentity) {
            const std::string prefix = "transaction-";
            try {
                numeric = std::stoull(id.substr(prefix.size()));
            } catch (...) {
                numeric = 0;
            }
            if (!id.starts_with(prefix) || numeric <= previousNumeric || numeric >= restoredNext) {
                return failure(eve::DiagnosticCode::ParseError,
                               "invalid transaction id at index " + std::to_string(i));
            }
        }
        if (id.empty() || std::find(restoredPlanIds.begin(), restoredPlanIds.end(), id) !=
                               restoredPlanIds.end()) {
            return failure(eve::DiagnosticCode::Conflict,
                           "duplicate transaction id at index " + std::to_string(i));
        }
        restoredPlanIds.push_back(id);
        auto plan = std::unique_ptr<Plan>(new Plan(
            id, value.getString("correlation"), value.getString("causation"), identity,
            transactionEntropy_, transactionClock_));
        plan->state_ = state;
        plan->error_ = value.getString("error");
        plan->operations_.clear();
        plan->events_.clear();
        auto operations = value.get("operations");
        std::vector<std::string> restoredOperationIds;
        for (size_t j = 0; j < operations.size(); ++j) {
            auto opValue = operations.at(j);
            if (!opValue.isObject() || opValue.getString("id").empty() ||
                opValue.getString("kind").empty() || !opValue.get("payload") ||
                !opValue.get("checked").isBool() || !opValue.get("valid").isBool()) {
                return failure(eve::DiagnosticCode::ParseError, "invalid operation in plan " + id);
            }
            Operation op;
            op.id = opValue.getString("id");
            const std::string operationIdentityText = opValue.getString("identity");
            if (!operationIdentityText.empty()) {
                const auto parsed = eve::OperationId::parse(operationIdentityText);
                if (!parsed || parsed->isNil() || op.id != parsed->format()) {
                    return failure(eve::DiagnosticCode::ParseError,
                                   "invalid canonical operation id in plan " + id);
                }
                op.identity = *parsed;
            } else if (const auto parsed = eve::OperationId::parse(op.id); parsed && !parsed->isNil()) {
                op.identity = *parsed;
            }
            if (std::find(restoredOperationIds.begin(), restoredOperationIds.end(), op.id) !=
                restoredOperationIds.end()) {
                return failure(eve::DiagnosticCode::Conflict,
                               "duplicate operation id in plan " + id);
            }
            restoredOperationIds.push_back(op.id);
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
                return failure(eve::DiagnosticCode::ParseError, "invalid event in plan " + id);
            }
            event.transactionId = id;
            event.operationId = eventValue.getString("operationId");
            event.type = eventValue.getString("type");
            event.detail = eventValue.getString("detail");
            event.transactionIdentity = identity;
            if (const auto parsed = eve::OperationId::parse(event.operationId))
                event.operationIdentity = *parsed;
            previousEvent = event.sequence;
            plan->events_.push_back(std::move(event));
        }
        if (!plan->events_.empty() && plan->events_.back().sequence >= nextEvent) {
            return failure(eve::DiagnosticCode::ParseError,
                           "invalid event allocator in plan " + id);
        }
        plan->nextOperation_ = nextOperation;
        plan->nextEvent_ = nextEvent;
        if (!canonicalIdentity) previousNumeric = numeric;
        restored.push_back(std::move(plan));
    }
    plans_ = std::move(restored);
    nextTransaction_ = restoredNext;
    return eve::Result<void>::success();
}

eve::Result<eve::SnapshotEnvelope> Ledger::snapshot(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("transaction.ledger", transactionSchema(), eve::SchemaVersion(1), instanceId_,
                                     revision_, tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> Ledger::restoreSnapshot(
    const eve::SnapshotEnvelope& source, const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "transaction.ledger" || source.schema != transactionSchema())
        return snapshotFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to transaction::Ledger");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "snapshot instanceId does not match transaction::Ledger");

    auto migrated = transactionMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    const auto& candidateEnvelope = migrated.value();
    auto metadata = eve::validateSnapshotPayloadMetadata(candidateEnvelope.payload,
                                                         candidateEnvelope.revision,
                                                         candidateEnvelope.tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto payload = candidateEnvelope.payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());

    Ledger candidate(instanceId_, transactionEntropy_, transactionClock_);
    auto restored = candidate.restore(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    candidate.instanceId_ = candidateEnvelope.instanceId;
    candidate.revision_   = candidateEnvelope.revision;
    candidate.tick_       = candidateEnvelope.tick;
    *this = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<std::string> Ledger::snapshotEnvelopeJson(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> Ledger::restoreSnapshotJson(
    std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

Ledger* Transaction::newLedger() {
    auto* module = Transaction::create();
    module->ledgers_.push_back(std::make_unique<Ledger>());
    return module->ledgers_.back().get();
}

Module_IMPL(Transaction, new Transaction());

void Transaction::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
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
    // Project the canonical Result API into the common Squirrel result-table
    // schema.  The script surface retains the short domain names, while the
    // failure status and diagnostics remain observable to the caller.
    plan.addFunc("markValid", [vm](Plan* p, const std::string& id) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->markValid(id));
    });
    plan.addFunc("markInvalid", [vm](Plan* p, const std::string& id, const std::string& error) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->markInvalid(id, error));
    });
    plan.addFunc("validate", [vm](Plan* p) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->validate());
    });
    plan.addFunc("commit", [vm](Plan* p) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->commit());
    });
    plan.addFunc("rollback", [vm](Plan* p, const std::string& reason) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->rollback(reason));
    });
    plan.addFunc("fail", [vm](Plan* p, const std::string& error) {
        if (!p)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction plan must not be null", "plan"));
        return eve::script::projectResult(vm, p->fail(error));
    });
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
    plan.addFunc("stage", [vm](Plan* value, const std::string& kind,
                                const std::string& target,
                                const std::string& payloadJson,
                                const std::string& operationIdentity) {
        if (!value)
            return eve::script::projectResult(
                vm, transactionBindingFailure<eve::OperationId>(
                        eve::DiagnosticCode::InvalidArgument, "transaction plan must not be null", "plan"),
                [](eve::OperationId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
        eve::OperationId identity;
        if (!operationIdentity.empty()) {
            const auto parsed = eve::OperationId::parse(operationIdentity);
            if (!parsed)
                return eve::script::projectResult(
                    vm, transactionBindingFailure<eve::OperationId>(
                            eve::DiagnosticCode::InvalidArgument,
                            "operation identity must be canonical UUID text", "operationIdentity"),
                    [](eve::OperationId id) {
                        return eve::Value(id.isNil() ? std::string{} : id.format());
                    });
            identity = *parsed;
        }
        return eve::script::projectResult(
            vm, value->stage(kind, target, payloadJson, identity),
            [](eve::OperationId id) { return eve::Value(id.isNil() ? std::string{} : id.format()); });
    });

    auto ledger = table.addClass<Ledger>("TransactionLedger", std::function<Ledger*()>([] { return nullptr; }), false);
    // Bind the compatibility spelling through a typed lambda so Ledger's
    // canonical TransactionId overload is never considered by deduction.
    ledger.addFunc("find", [](Ledger* ledger, const std::string& id) -> Plan* {
        return ledger ? ledger->find(id) : nullptr;
    });
    ledger.addFunc("count", &Ledger::count);
    ledger.addFunc("at", &Ledger::at);
    ledger.addFunc("snapshotJson", &Ledger::snapshotJson);
    ledger.addFunc("restore", [vm](Ledger* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, transactionBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                     "transaction ledger must not be null", "ledger"));
        return eve::script::projectResult(vm, value->restore(json));
    });
    ledger.addFunc("create", [vm](Ledger* value, const std::string& correlation,
                                   const std::string& causation,
                                   const std::string& transactionIdentity) {
        if (!value)
            return eve::script::projectResult(
                vm, transactionBindingFailure<Plan*>(eve::DiagnosticCode::InvalidArgument,
                                                      "transaction ledger must not be null", "ledger"),
                [](Plan* plan) { return planBindingValue(plan); });
        eve::TransactionId identity;
        if (!transactionIdentity.empty()) {
            const auto parsed = eve::TransactionId::parse(transactionIdentity);
            if (!parsed)
                return eve::script::projectResult(
                    vm, transactionBindingFailure<Plan*>(
                            eve::DiagnosticCode::InvalidArgument,
                            "transaction identity must be canonical UUID text", "transactionIdentity"),
                    [](Plan* plan) { return planBindingValue(plan); });
            identity = *parsed;
        }
        return eve::script::projectResult(
            vm, value->create(correlation, causation, identity),
            [](Plan* plan) { return planBindingValue(plan); });
    });

    auto cls = table.addClass(name, Transaction::create, false);
    expose(cls);
}

void Transaction::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Transaction::getName);
    cls.addFunc("newLedger", [](Transaction*) { return Transaction::newLedger(); });
}

}  // namespace eve::transaction
