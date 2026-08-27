#include "statepatch/StatePatch.h"

#include "common/Json.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <exception>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::statepatch {
namespace {

/** @brief Script-owned handle proxy; the state store remains module-owned. */
struct ScriptStateStore {
    explicit ScriptStateStore(StateStoreHandleRef value) : reference(value) {}
    ~ScriptStateStore() noexcept {
        StatePatch::release(reference).ignore("script state-patch store proxy destruction");
    }
    StateStoreHandleRef reference;
};

/** @brief Script-owned batch proxy; the Store remains the authoritative owner. */
struct ScriptStateBatch {
    explicit ScriptStateBatch(StateBatchHandleRef value) : reference(value) {}
    ~ScriptStateBatch() noexcept {
        StatePatch::releaseBatch(reference).ignore("script state-patch batch proxy destruction");
    }
    StateBatchHandleRef reference;
};

template <class T>
eve::Result<T> statePatchBindingFailure(eve::DiagnosticCode code, std::string message,
                                        std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(
        code, std::move(message), std::move(path), {}, "statepatch.squirrel"));
}

template <class T>
ssq::Table staleStateResult(HSQUIRRELVM vm, const char* objectName) {
    return eve::script::projectResult(
        vm, statePatchBindingFailure<T>(eve::DiagnosticCode::StaleHandle,
                                        std::string("owned state-patch ") + objectName +
                                            " handle is stale", objectName),
        [](T value) { return eve::Value(value); });
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference)
        return eve::script::projectStatusResult(vm, reference.status(), false, false);
    const Ref ref = std::move(reference).takeValue();
    auto object = eve::script::makeOwnedSquirrelInstance<Proxy>(
        vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned state-patch proxy");
        std::invoke(std::forward<Release>(release), ref).ignore(
            "rollback failed owned state-patch allocation");
        return eve::script::projectStatusResult(vm, status, false, false);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto result = eve::script::projectStatusResult(
        vm, eve::Status::success(eve::StatusCode::Applied), true, false);
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

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

bool normalize(const std::string& input, std::string& output) {
    auto document = eve::json::Document::parse(input);
    if (!document.valid()) return false;
    output = canonicalJson(document.root());
    return true;
}

bool parseU64(const eve::json::Value& value, uint64_t& output) {
    if (!value.isString()) return false;
    try {
        size_t used = 0;
        output      = std::stoull(value.asString(), &used);
        return used == value.asString().size();
    } catch (...) {
        return false;
    }
}

eve::LogicalId statePatchSchema() {
    const auto schema = eve::LogicalId::parse("statepatch:store");
    if (!schema) std::terminate();
    return *schema;
}

const eve::SnapshotMigrationChain& statePatchMigrations() {
    static const eve::SnapshotMigrationChain chain = [] {
        eve::SnapshotMigrationChain result;
        const auto registration = result.add(
            statePatchSchema(), eve::SchemaVersion(0), eve::SchemaVersion(1),
            [](const eve::Value& payload) -> eve::Result<eve::Value> {
                const auto* object = payload.getIf<eve::Value::Object>();
                if (!object)
                    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::ParseError, "state patch payload must be an object"));
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

eve::Result<void> restoreFailure(std::string message, std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::ParseError, std::move(message), std::move(path), {},
        "statepatch.store.restoreJson"));
}

}  // namespace

Store::Store(eve::PersistentId instanceId) : instanceId_(instanceId) {}

bool PatchBatch::set(const std::string& subject, const std::string& key, const std::string& jsonValue) {
    std::string value;
    const bool  valid = normalize(jsonValue, value);
    operations_.push_back({false, subject, key, value, std::nullopt, valid ? "" : "value must be valid JSON"});
    return valid;
}

bool PatchBatch::setExpected(const std::string& subject, const std::string& key, const std::string& jsonValue,
                             const std::string& expectedJson) {
    std::string value;
    std::string expected;
    const bool  valueValid    = normalize(jsonValue, value);
    const bool  expectedValid = normalize(expectedJson, expected);
    std::string error;
    if (!valueValid) error = "value must be valid JSON";
    if (!expectedValid)
        error = error.empty() ? "expected value must be valid JSON" : error + "; expected value must be valid JSON";
    operations_.push_back({false, subject, key, value, expected, error});
    return valueValid && expectedValid;
}

bool PatchBatch::remove(const std::string& subject, const std::string& key) {
    operations_.push_back({true, subject, key, {}, std::nullopt, {}});
    return true;
}

bool PatchBatch::removeExpected(const std::string& subject, const std::string& key, const std::string& expectedJson) {
    std::string expected;
    const bool  valid = normalize(expectedJson, expected);
    operations_.push_back({true, subject, key, {}, expected, valid ? "" : "expected value must be valid JSON"});
    return valid;
}

void PatchBatch::clear() {
    operations_.clear();
    result_ = {};
}

int PatchBatch::size() const { return static_cast<int>(operations_.size()); }

const PatchResult& PatchBatch::result() const { return result_; }

eve::Result<PatchBatchHandleRef> Store::newBatch() {
    return batches_.emplace(std::make_unique<PatchBatch>());
}

eve::script::Borrowed<PatchBatch> Store::resolveBatch(
    PatchBatchHandleRef reference) noexcept {
    return batches_.resolve(reference);
}

eve::Result<void> Store::releaseBatch(PatchBatchHandleRef reference) {
    return batches_.erase(reference);
}

bool Store::isBatchStale(PatchBatchHandleRef reference) const noexcept {
    if (!reference.isValid()) return false;
    return batches_.isStale(reference);
}

bool Store::commit(PatchBatch* batch) {
    if (!batch) return false;
    batch->result_                = {};
    batch->result_.revisionBefore = revision_;
    batch->result_.revisionAfter  = revision_;
    Values staged                 = values_;
    for (size_t i = 0; i < batch->operations_.size(); ++i) {
        const auto& operation = batch->operations_[i];
        auto        fail      = [&](const std::string& code, const std::string& message) {
            batch->result_.errors.push_back({static_cast<int>(i), operation.subject, operation.key, code, message});
        };
        if (operation.subject.empty()) fail("invalid_subject", "subject must not be empty");
        if (operation.key.empty()) fail("invalid_key", "key must not be empty");
        if (!operation.inputError.empty()) fail("invalid_json", operation.inputError);
        if (!batch->result_.errors.empty() && batch->result_.errors.back().operationIndex == static_cast<int>(i))
            continue;
        const auto subjectIt = staged.find(operation.subject);
        const auto valueIt   = subjectIt == staged.end() ? std::map<std::string, Value>::const_iterator{}
                                                         : subjectIt->second.find(operation.key);
        const bool exists    = subjectIt != staged.end() && valueIt != subjectIt->second.end();
        if (operation.expected && (!exists || valueIt->second.json != *operation.expected)) {
            fail("conflict", "current value does not match expected JSON");
            continue;
        }
        if (operation.remove) {
            if (exists) {
                staged[operation.subject].erase(operation.key);
                if (staged[operation.subject].empty()) staged.erase(operation.subject);
            }
        } else {
            staged[operation.subject][operation.key].json = operation.value;
        }
    }
    if (!batch->result_.errors.empty()) return false;

    struct Pending {
        std::string subject;
        std::string key;
        std::string oldJson;
        std::string newJson;
        bool        removed;
    };
    std::vector<Pending>                          changes;
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto& [subject, entries] : values_)
        for (const auto& [key, value] : entries) keys.emplace(subject, key);
    for (const auto& [subject, entries] : staged)
        for (const auto& [key, value] : entries) keys.emplace(subject, key);
    for (const auto& [subject, key] : keys) {
        const auto oldSubject = values_.find(subject);
        const auto newSubject = staged.find(subject);
        const auto oldValue =
            oldSubject == values_.end() ? std::map<std::string, Value>::const_iterator{} : oldSubject->second.find(key);
        const auto newValue =
            newSubject == staged.end() ? std::map<std::string, Value>::const_iterator{} : newSubject->second.find(key);
        const bool had = oldSubject != values_.end() && oldValue != oldSubject->second.end();
        const bool has = newSubject != staged.end() && newValue != newSubject->second.end();
        if (had != has || (had && oldValue->second.json != newValue->second.json))
            changes.push_back({subject, key, had ? oldValue->second.json : "", has ? newValue->second.json : "", !has});
    }
    if (!changes.empty()) {
        if (revision_ == std::numeric_limits<uint64_t>::max() ||
            nextSequence_ > std::numeric_limits<uint64_t>::max() - changes.size()) {
            batch->result_.errors.push_back({-1, {}, {}, "revision_exhausted", "revision or event sequence exhausted"});
            return false;
        }
        ++revision_;
        for (const auto& change : changes) {
            if (!change.removed) staged[change.subject][change.key].revision = revision_;
            dirty_.emplace(change.subject, change.key);
            events_.push_back({nextSequence_++, revision_, change.subject, change.key, change.oldJson, change.newJson,
                               change.removed});
        }
        values_ = std::move(staged);
    }
    batch->result_.success       = true;
    batch->result_.changedCount  = static_cast<int>(changes.size());
    batch->result_.revisionAfter = revision_;
    return true;
}

bool Store::has(const std::string& subject, const std::string& key) const {
    const auto found = values_.find(subject);
    return found != values_.end() && found->second.find(key) != found->second.end();
}

std::string Store::get(const std::string& subject, const std::string& key) const {
    const auto found = values_.find(subject);
    if (found == values_.end()) return {};
    const auto value = found->second.find(key);
    return value == found->second.end() ? std::string{} : value->second.json;
}

uint64_t Store::valueRevision(const std::string& subject, const std::string& key) const {
    const auto found = values_.find(subject);
    if (found == values_.end()) return 0;
    const auto value = found->second.find(key);
    return value == found->second.end() ? 0 : value->second.revision;
}

uint64_t Store::revision() const { return revision_; }

int Store::querySubjects() {
    query_.clear();
    for (const auto& [subject, entries] : values_) query_.push_back(subject);
    return static_cast<int>(query_.size());
}

int Store::queryKeys(const std::string& subject) {
    query_.clear();
    const auto found = values_.find(subject);
    if (found != values_.end())
        for (const auto& [key, value] : found->second) query_.push_back(key);
    return static_cast<int>(query_.size());
}

std::string Store::queryAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < query_.size() ? query_[static_cast<size_t>(index)]
                                                                    : std::string{};
}

int Store::queryDirty() {
    dirtyQuery_.assign(dirty_.begin(), dirty_.end());
    return static_cast<int>(dirtyQuery_.size());
}

std::string Store::dirtySubjectAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < dirtyQuery_.size() ? dirtyQuery_[static_cast<size_t>(index)].first
                                                                         : std::string{};
}

std::string Store::dirtyKeyAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < dirtyQuery_.size()
               ? dirtyQuery_[static_cast<size_t>(index)].second
               : std::string{};
}

void Store::clearDirty() {
    dirty_.clear();
    dirtyQuery_.clear();
}

int Store::eventCount() const { return static_cast<int>(events_.size()); }

const ChangeEvent* Store::eventAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < events_.size() ? &events_[static_cast<size_t>(index)] : nullptr;
}

void Store::clearEvents() { events_.clear(); }

std::string Store::snapshotJson() const {
    std::ostringstream out;
    out << "{\"version\":1,\"revision\":" << quote(std::to_string(revision_))
        << ",\"nextSequence\":" << quote(std::to_string(nextSequence_)) << ",\"values\":[";
    bool first = true;
    for (const auto& [subject, entries] : values_)
        for (const auto& [key, value] : entries) {
            if (!first) out << ',';
            first = false;
            out << "{\"subject\":" << quote(subject) << ",\"key\":" << quote(key)
                << ",\"revision\":" << quote(std::to_string(value.revision)) << ",\"value\":" << value.json << '}';
        }
    out << "],\"dirty\":[";
    first = true;
    for (const auto& [subject, key] : dirty_) {
        if (!first) out << ',';
        first = false;
        out << "[" << quote(subject) << ',' << quote(key) << ']';
    }
    return out.str() + "]}";
}

eve::Result<void> Store::restoreJson(const std::string& json) {
    std::string parseError;
    auto        document = eve::json::Document::parse(json, &parseError);
    if (!document.valid())
        return restoreFailure(parseError.empty() ? "invalid state patch snapshot" : std::move(parseError), "$");

    const auto root = document.root();
    if (!root.isObject()) return restoreFailure("snapshot root must be an object", "$");

    uint64_t restoredRevision = 0;
    uint64_t restoredSequence = 0;
    if (root.getInt("version") != 1)
        return restoreFailure("unsupported state patch snapshot version", "$.version");
    if (!parseU64(root.get("revision"), restoredRevision))
        return restoreFailure("snapshot revision must be a decimal string", "$.revision");
    if (!parseU64(root.get("nextSequence"), restoredSequence) || restoredSequence == 0)
        return restoreFailure("snapshot nextSequence must be a non-zero decimal string", "$.nextSequence");

    const auto values = root.get("values");
    const auto dirty  = root.get("dirty");
    if (!values.isArray() || !dirty.isArray()) {
        return restoreFailure("values and dirty must be arrays", "$");
    }
    Values                                        restoredValues;
    std::set<std::pair<std::string, std::string>> restoredDirty;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto item          = values.at(i);
        uint64_t   valueRevision = 0;
        if (!item.isObject() || item.getString("subject").empty() || item.getString("key").empty() ||
            !parseU64(item.get("revision"), valueRevision) || valueRevision > restoredRevision || !item.get("value")) {
            return restoreFailure("invalid value at index " + std::to_string(i),
                                  "$.values[" + std::to_string(i) + "]");
        }
        const auto subject = item.getString("subject");
        const auto key     = item.getString("key");
        if (restoredValues[subject].contains(key)) {
            return restoreFailure("duplicate subject and key", "$.values[" + std::to_string(i) + "]");
        }
        restoredValues[subject][key] = {canonicalJson(item.get("value")), valueRevision};
    }
    for (size_t i = 0; i < dirty.size(); ++i) {
        const auto item = dirty.at(i);
        if (!item.isArray() || item.size() != 2 || !item.at(0).isString() || !item.at(1).isString() ||
            item.at(0).asString().empty() || item.at(1).asString().empty()) {
            return restoreFailure("invalid dirty key at index " + std::to_string(i),
                                  "$.dirty[" + std::to_string(i) + "]");
        }
        restoredDirty.emplace(item.at(0).asString(), item.at(1).asString());
    }
    values_       = std::move(restoredValues);
    dirty_        = std::move(restoredDirty);
    revision_     = restoredRevision;
    nextSequence_ = restoredSequence;
    events_.clear();
    query_.clear();
    dirtyQuery_.clear();
    batches_.clear();
    return eve::Result<void>::success();
}

eve::Result<eve::SnapshotEnvelope> Store::snapshot(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto payload = eve::Value::fromJson(snapshotJson());
    if (!payload.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(payload.status());
    return eve::makeSnapshotEnvelope("statepatch.store", statePatchSchema(), eve::SchemaVersion(1), instanceId_,
                                     eve::Revision(revision_), tick_, std::move(payload).takeValue(), hashProvider);
}

eve::Result<void> Store::restoreSnapshot(
    const eve::SnapshotEnvelope& source, const eve::SnapshotHashProvider& hashProvider) {
    if (source.type != "statepatch.store" || source.schema != statePatchSchema())
        return snapshotFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "snapshot does not belong to statepatch::Store");
    if (!instanceId_.isNil() && source.instanceId != instanceId_)
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "snapshot instanceId does not match statepatch::Store");
    auto migrated = statePatchMigrations().migrate(source, eve::SchemaVersion(1), hashProvider);
    if (!migrated.ok()) return eve::Result<void>::failure(migrated.status());
    const auto& candidateEnvelope = migrated.value();
    auto metadata = eve::validateSnapshotPayloadMetadata(candidateEnvelope.payload,
                                                         candidateEnvelope.revision,
                                                         candidateEnvelope.tick);
    if (!metadata.ok()) return eve::Result<void>::failure(metadata.status());
    auto payload = candidateEnvelope.payload.toJson();
    if (!payload.ok()) return eve::Result<void>::failure(payload.status());

    Store candidate(instanceId_);
    auto restored = candidate.restoreJson(std::move(payload).takeValue());
    if (!restored.ok()) return eve::Result<void>::failure(restored.status());
    if (candidate.revision_ != candidateEnvelope.revision.value())
        return snapshotFailure<void>(eve::DiagnosticCode::Conflict,
                                     "state patch payload revision disagrees with snapshot envelope");
    candidate.instanceId_ = candidateEnvelope.instanceId;
    candidate.tick_       = candidateEnvelope.tick;
    *this = std::move(candidate);
    return eve::Result<void>::success();
}

eve::Result<std::string> Store::snapshotEnvelopeJson(
    const eve::SnapshotHashProvider& hashProvider) const {
    auto value = snapshot(hashProvider);
    if (!value.ok()) return eve::Result<std::string>::failure(value.status());
    return std::move(value).andThen(
        [](eve::SnapshotEnvelope&& envelope) { return eve::serializeSnapshotEnvelope(envelope); });
}

eve::Result<void> Store::restoreSnapshotJson(
    std::string_view json, const eve::SnapshotHashProvider& hashProvider) {
    auto source = eve::parseSnapshotEnvelope(json, hashProvider);
    if (!source.ok()) return eve::Result<void>::failure(source.status());
    return restoreSnapshot(std::move(source).takeValue(), hashProvider);
}

void Store::copyTransactionStateFrom(const Store& source) {
    values_       = source.values_;
    revision_     = source.revision_;
    nextSequence_ = source.nextSequence_;
    dirty_        = source.dirty_;
    events_       = source.events_;
    query_.clear();
    dirtyQuery_.clear();
    batches_.clear();
}

void Store::swapTransactionState(Store& other) noexcept {
    values_.swap(other.values_);
    std::swap(revision_, other.revision_);
    std::swap(nextSequence_, other.nextSequence_);
    dirty_.swap(other.dirty_);
    events_.swap(other.events_);
    query_.clear();
    dirtyQuery_.clear();
    other.query_.clear();
    other.dirtyQuery_.clear();
}

bool Store::transactionStateEquals(const Store& other) const {
    if (snapshotJson() != other.snapshotJson() || events_.size() != other.events_.size()) return false;
    for (size_t i = 0; i < events_.size(); ++i) {
        const auto& left  = events_[i];
        const auto& right = other.events_[i];
        if (left.sequence != right.sequence || left.revision != right.revision || left.subject != right.subject ||
            left.key != right.key || left.oldJson != right.oldJson || left.newJson != right.newJson ||
            left.removed != right.removed)
            return false;
    }
    return true;
}

namespace {

eve::Result<void> patchParticipantFailure(const PatchResult& result) {
    eve::StatusCode statusCode = eve::StatusCode::Failed;
    eve::DiagnosticCode diagnosticCode = eve::DiagnosticCode::Failed;
    std::vector<eve::Diagnostic> diagnostics;
    for (const auto& error : result.errors) {
        if (error.code == "conflict") {
            statusCode    = eve::StatusCode::Conflict;
            diagnosticCode = eve::DiagnosticCode::Conflict;
        } else if (statusCode == eve::StatusCode::Failed && error.code.starts_with("invalid_")) {
            statusCode    = eve::StatusCode::Rejected;
            diagnosticCode = eve::DiagnosticCode::InvalidArgument;
        }
        diagnostics.push_back(eve::Diagnostic::error(
            diagnosticCode, error.message.empty() ? "state patch batch was rejected" : error.message,
            "statepatch[" + std::to_string(error.operationIndex) + "]"));
    }
    if (diagnostics.empty())
        diagnostics.push_back(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "state patch batch could not be prepared", "statepatch"));
    return eve::Result<void>::failure(eve::Status(statusCode, std::move(diagnostics)));
}

}  // namespace

bool StoreTransactionParticipant::contextMatches(const transaction::TransactionContext& context) const noexcept {
    return context.transactionId() == transactionId_ && context.correlationId() == correlationId_ &&
           context.causationId() == causationId_;
}

eve::Result<void> StoreTransactionParticipant::lifecycleFailure(std::string_view operation) const {
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::PreconditionViolation,
        "statepatch participant cannot " + std::string(operation) + " in its current lifecycle phase",
        "statepatch.lifecycle"));
}

eve::Result<void> StoreTransactionParticipant::contextFailure(
    const transaction::TransactionContext& context) const {
    (void)context;
    return eve::Result<void>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::Conflict, "transaction context changed between participant lifecycle calls",
        "statepatch.context"));
}

eve::Result<void> StoreTransactionParticipant::prepare(const transaction::TransactionContext& context) {
    if (phase_ != Phase::Idle) return lifecycleFailure("prepare");
    if (context.transactionId().empty())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "transaction id must not be empty", "transactionId"));

    transactionId_ = context.transactionId();
    correlationId_ = context.correlationId();
    causationId_   = context.causationId();
    before_        = std::make_unique<Store>();
    before_->copyTransactionStateFrom(store_);
    prepared_ = std::make_unique<Store>();
    prepared_->copyTransactionStateFrom(*before_);

    if (!prepared_->commit(&batch_)) {
        auto result = patchParticipantFailure(batch_.result());
        before_.reset();
        prepared_.reset();
        phase_ = Phase::Failed;
        return result;
    }

    expectedAfter_ = std::make_unique<Store>();
    expectedAfter_->copyTransactionStateFrom(*prepared_);
    phase_ = Phase::Prepared;
    return eve::Result<void>::success();
}

eve::Result<void> StoreTransactionParticipant::commit(const transaction::TransactionContext& context) {
    if (phase_ != Phase::Prepared) return lifecycleFailure("commit");
    if (!contextMatches(context)) return contextFailure(context);
    if (!before_ || !prepared_ || !expectedAfter_ || !store_.transactionStateEquals(*before_))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "state patch store changed after prepare", "statepatch.store"));

    store_.swapTransactionState(*prepared_);
    phase_ = Phase::Committed;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> StoreTransactionParticipant::rollback(const transaction::TransactionContext& context) {
    if (phase_ != Phase::Prepared) return lifecycleFailure("rollback");
    if (!contextMatches(context)) return contextFailure(context);
    before_.reset();
    prepared_.reset();
    expectedAfter_.reset();
    phase_ = Phase::RolledBack;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> StoreTransactionParticipant::compensate(const transaction::TransactionContext& context) {
    if (phase_ != Phase::Committed) return lifecycleFailure("compensate");
    if (!contextMatches(context)) return contextFailure(context);
    if (!prepared_ || !expectedAfter_ || !store_.transactionStateEquals(*expectedAfter_))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "state patch store changed before compensation", "statepatch.store"));

    store_.swapTransactionState(*prepared_);
    expectedAfter_.reset();
    phase_ = Phase::Compensated;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<StateStoreHandleRef> StatePatch::newStore() {
    StatePatch* module = StatePatch::create();
    return module->stores_.emplace(std::make_unique<Store>());
}

eve::script::Borrowed<PatchBatch> StatePatch::resolveBatch(
    StateBatchHandleRef reference) noexcept {
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    if (!module) return {};
    auto store = module->stores_.resolve(reference.store);
    if (!store.isBound()) return {};
    return store->resolveBatch(reference.batch);
}

eve::Result<void> StatePatch::releaseBatch(StateBatchHandleRef reference) {
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    if (!module)
        return statePatchBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                              "StatePatch module is no longer loaded", "batch");
    auto store = module->stores_.resolve(reference.store);
    if (!store.isBound())
        return statePatchBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                              "owning StatePatch store is stale", "store");
    return store->releaseBatch(reference.batch);
}

bool StatePatch::isBatchStale(StateBatchHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    if (!module) return true;
    auto store = module->stores_.resolve(reference.store);
    return !store.isBound() || store->isBatchStale(reference.batch);
}

eve::script::Borrowed<Store> StatePatch::resolve(
    StateStoreHandleRef reference) noexcept {
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    if (!module) return {};
    return module->stores_.resolve(reference);
}

eve::Result<void> StatePatch::release(StateStoreHandleRef reference) {
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    if (!module)
        return statePatchBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                              "StatePatch module is no longer loaded", "store");
    return module->stores_.erase(reference);
}

bool StatePatch::isStale(StateStoreHandleRef reference) noexcept {
    if (!reference.isValid()) return false;
    StatePatch* module = ModuleManager::getInstance<StatePatch>("StatePatch");
    return !module || module->stores_.isStale(reference);
}

Module_IMPL(StatePatch, new StatePatch());

void StatePatch::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    auto error = table.addClass<PatchError>("PatchError", std::function<PatchError*()>([] { return nullptr; }), false);
    error.addFunc("getOperationIndex", [](PatchError* e) { return e ? e->operationIndex : -1; });
    error.addFunc("getSubject", [](PatchError* e) { return e ? e->subject : std::string{}; });
    error.addFunc("getKey", [](PatchError* e) { return e ? e->key : std::string{}; });
    error.addFunc("getCode", [](PatchError* e) { return e ? e->code : std::string{}; });
    error.addFunc("getMessage", [](PatchError* e) { return e ? e->message : std::string{}; });

    auto result =
        table.addClass<PatchResult>("PatchResult", std::function<PatchResult*()>([] { return nullptr; }), false);
    result.addFunc("isSuccess", [](PatchResult* r) { return r && r->success; });
    result.addFunc("getChangedCount", [](PatchResult* r) { return r ? r->changedCount : 0; });
    result.addFunc("getRevisionBefore", [](PatchResult* r) { return r ? static_cast<int64_t>(r->revisionBefore) : 0; });
    result.addFunc("getRevisionAfter", [](PatchResult* r) { return r ? static_cast<int64_t>(r->revisionAfter) : 0; });
    result.addFunc("errorCount", [](PatchResult* r) { return r ? static_cast<int>(r->errors.size()) : 0; });
    result.addFunc("errorAt", [](PatchResult* r, int index) -> PatchError* {
        return r && index >= 0 && static_cast<size_t>(index) < r->errors.size() ? &r->errors[static_cast<size_t>(index)]
                                                                                : nullptr;
    });

    auto event =
        table.addClass<ChangeEvent>("StateChangeEvent", std::function<ChangeEvent*()>([] { return nullptr; }), false);
    event.addFunc("getSequence", [](ChangeEvent* e) { return e ? static_cast<int64_t>(e->sequence) : 0; });
    event.addFunc("getRevision", [](ChangeEvent* e) { return e ? static_cast<int64_t>(e->revision) : 0; });
    event.addFunc("getSubject", [](ChangeEvent* e) { return e ? e->subject : std::string{}; });
    event.addFunc("getKey", [](ChangeEvent* e) { return e ? e->key : std::string{}; });
    event.addFunc("getOldJson", [](ChangeEvent* e) { return e ? e->oldJson : std::string{}; });
    event.addFunc("getNewJson", [](ChangeEvent* e) { return e ? e->newJson : std::string{}; });
    event.addFunc("isRemoved", [](ChangeEvent* e) { return e && e->removed; });

    auto batch = table.addClass<PatchBatch>("PatchBatch", std::function<PatchBatch*()>([] { return nullptr; }), false);
    batch.addFunc("set", &PatchBatch::set);
    batch.addFunc("setExpected", &PatchBatch::setExpected);
    batch.addFunc("remove", &PatchBatch::remove);
    batch.addFunc("removeExpected", &PatchBatch::removeExpected);
    batch.addFunc("clear", &PatchBatch::clear);
    batch.addFunc("size", &PatchBatch::size);
    batch.addFunc("result",
                  [](PatchBatch* b) -> PatchResult* { return b ? const_cast<PatchResult*>(&b->result()) : nullptr; });

    auto ownedBatch = table.addClass<ScriptStateBatch>(
        "StateBatchProxy", std::function<ScriptStateBatch*()>([] { return nullptr; }), true);
    ownedBatch.addFunc("ownership", [](ScriptStateBatch*) { return std::string("owned"); });
    ownedBatch.addFunc("ownerEpoch", [](ScriptStateBatch* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedBatch.addFunc("handle", [](ScriptStateBatch* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedBatch.addFunc("isStale", [](ScriptStateBatch* value) {
        return !value || StatePatch::isBatchStale(value->reference);
    });
    ownedBatch.addFunc("release", [vm](ScriptStateBatch* value) {
        if (!value)
            return eve::script::projectResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "owned state batch proxy must not be null", "batch"));
        return eve::script::projectResult(vm, StatePatch::releaseBatch(value->reference));
    });
    ownedBatch.addFunc("set", [vm](ScriptStateBatch* value, const std::string& subject,
                                    const std::string& key, const std::string& jsonValue) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        if (!view.isBound())
            return staleStateResult<bool>(vm, "batch");
        return eve::script::projectResult(
            vm, eve::Result<bool>::success(view->set(subject, key, jsonValue)),
            [](bool value) { return eve::Value(value); });
    });
    ownedBatch.addFunc("remove", [vm](ScriptStateBatch* value, const std::string& subject,
                                       const std::string& key) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        if (!view.isBound())
            return staleStateResult<bool>(vm, "batch");
        return eve::script::projectResult(
            vm, eve::Result<bool>::success(view->remove(subject, key)),
            [](bool value) { return eve::Value(value); });
    });
    ownedBatch.addFunc("setExpected", [vm](ScriptStateBatch* value, const std::string& subject,
                                            const std::string& key, const std::string& jsonValue,
                                            const std::string& expectedJson) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        if (!view.isBound())
            return staleStateResult<bool>(vm, "batch");
        return eve::script::projectResult(
            vm, eve::Result<bool>::success(view->setExpected(subject, key, jsonValue, expectedJson)),
            [](bool result) { return eve::Value(result); });
    });
    ownedBatch.addFunc("removeExpected", [vm](ScriptStateBatch* value, const std::string& subject,
                                               const std::string& key, const std::string& expectedJson) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        if (!view.isBound())
            return staleStateResult<bool>(vm, "batch");
        return eve::script::projectResult(
            vm, eve::Result<bool>::success(view->removeExpected(subject, key, expectedJson)),
            [](bool result) { return eve::Value(result); });
    });
    ownedBatch.addFunc("clear", [](ScriptStateBatch* value) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        if (view.isBound()) view->clear();
    });
    ownedBatch.addFunc("size", [](ScriptStateBatch* value) {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        return view.isBound() ? view->size() : 0;
    });
    ownedBatch.addFunc("result", [](ScriptStateBatch* value) -> PatchResult* {
        auto view = value ? StatePatch::resolveBatch(value->reference)
                          : eve::script::Borrowed<PatchBatch>();
        return view.isBound() ? const_cast<PatchResult*>(&view->result()) : nullptr;
    });

    auto store = table.addClass<Store>("StateStore", std::function<Store*()>([] { return nullptr; }), false);
    store.addFunc("commit", &Store::commit);
    store.addFunc("has", &Store::has);
    store.addFunc("get", &Store::get);
    store.addFunc("valueRevision", [](Store* s, const std::string& subject, const std::string& key) {
        return s ? static_cast<int64_t>(s->valueRevision(subject, key)) : 0;
    });
    store.addFunc("revision", [](Store* s) { return s ? static_cast<int64_t>(s->revision()) : 0; });
    store.addFunc("querySubjects", &Store::querySubjects);
    store.addFunc("queryKeys", &Store::queryKeys);
    store.addFunc("queryAt", &Store::queryAt);
    store.addFunc("queryDirty", &Store::queryDirty);
    store.addFunc("dirtySubjectAt", &Store::dirtySubjectAt);
    store.addFunc("dirtyKeyAt", &Store::dirtyKeyAt);
    store.addFunc("clearDirty", &Store::clearDirty);
    store.addFunc("eventCount", &Store::eventCount);
    store.addFunc("eventAt", [](Store* s, int index) -> ChangeEvent* {
        return s ? const_cast<ChangeEvent*>(s->eventAt(index)) : nullptr;
    });
    store.addFunc("clearEvents", &Store::clearEvents);
    store.addFunc("snapshotJson", &Store::snapshotJson);
    store.addFunc("restoreJson", [vm](Store* value, const std::string& json) {
        if (!value)
            return eve::script::projectResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "state store must not be null", "store"));
        return eve::script::projectResult(vm, value->restoreJson(json));
    });

    auto ownedStore = table.addClass<ScriptStateStore>(
        "StateStoreProxy", std::function<ScriptStateStore*()>([] { return nullptr; }), true);
    ownedStore.addFunc("ownership", [](ScriptStateStore*) { return std::string("owned"); });
    ownedStore.addFunc("ownerEpoch", [](ScriptStateStore* value) {
        return value ? static_cast<int64_t>(value->reference.ownerEpoch) : int64_t{0};
    });
    ownedStore.addFunc("handle", [](ScriptStateStore* value) {
        return value ? static_cast<int64_t>(value->reference.packed()) : int64_t{0};
    });
    ownedStore.addFunc("isStale", [](ScriptStateStore* value) {
        return !value || StatePatch::isStale(value->reference);
    });
    ownedStore.addFunc("release", [vm](ScriptStateStore* value) {
        if (!value)
            return eve::script::projectResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "owned state store proxy must not be null", "store"));
        return eve::script::projectResult(vm, StatePatch::release(value->reference));
    });
    ownedStore.addFunc("commit", [](ScriptStateStore* value, ScriptStateBatch* batch) {
        if (!value || !batch || !(batch->reference.store == value->reference)) return false;
        auto store = StatePatch::resolve(value->reference);
        auto staged = StatePatch::resolveBatch(batch->reference);
        return store.isBound() && staged.isBound() && store->commit(staged.get());
    });
    ownedStore.addFunc("revision", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? static_cast<int64_t>(view->revision()) : int64_t{0};
    });
    ownedStore.addFunc("has", [](ScriptStateStore* value, const std::string& subject,
                                  const std::string& key) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() && view->has(subject, key);
    });
    ownedStore.addFunc("get", [](ScriptStateStore* value, const std::string& subject,
                                  const std::string& key) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->get(subject, key) : std::string{};
    });
    ownedStore.addFunc("valueRevision", [](ScriptStateStore* value, const std::string& subject,
                                             const std::string& key) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? static_cast<int64_t>(view->valueRevision(subject, key)) : int64_t{0};
    });
    ownedStore.addFunc("querySubjects", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->querySubjects() : 0;
    });
    ownedStore.addFunc("queryKeys", [](ScriptStateStore* value, const std::string& subject) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryKeys(subject) : 0;
    });
    ownedStore.addFunc("queryAt", [](ScriptStateStore* value, int index) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryAt(index) : std::string{};
    });
    ownedStore.addFunc("queryDirty", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->queryDirty() : 0;
    });
    ownedStore.addFunc("dirtySubjectAt", [](ScriptStateStore* value, int index) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->dirtySubjectAt(index) : std::string{};
    });
    ownedStore.addFunc("dirtyKeyAt", [](ScriptStateStore* value, int index) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->dirtyKeyAt(index) : std::string{};
    });
    ownedStore.addFunc("clearDirty", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (view.isBound()) view->clearDirty();
    });
    ownedStore.addFunc("eventCount", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->eventCount() : 0;
    });
    ownedStore.addFunc("eventAt", [](ScriptStateStore* value, int index) -> ChangeEvent* {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? const_cast<ChangeEvent*>(view->eventAt(index)) : nullptr;
    });
    ownedStore.addFunc("clearEvents", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (view.isBound()) view->clearEvents();
    });
    ownedStore.addFunc("snapshotJson", [](ScriptStateStore* value) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        return view.isBound() ? view->snapshotJson() : std::string{};
    });
    ownedStore.addFunc("restoreJson", [vm](ScriptStateStore* value, const std::string& json) {
        auto view = value ? StatePatch::resolve(value->reference) : eve::script::Borrowed<Store>();
        if (!view.isBound())
            return eve::script::projectResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                    "owned state-patch store handle is stale", "store"));
        return eve::script::projectResult(vm, view->restoreJson(json));
    });
    ownedStore.addFunc("newBatch", [vm](ScriptStateStore* value) -> ssq::Table {
        if (!value)
            return eve::script::projectStatusResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                                    "owned state store proxy must not be null", "store")
                         .status(),
                false, false);
        auto store = StatePatch::resolve(value->reference);
        if (!store.isBound())
            return eve::script::projectStatusResult(
                vm, statePatchBindingFailure<void>(eve::DiagnosticCode::StaleHandle,
                                                    "owned state-patch store handle is stale", "store")
                         .status(),
                false, false);
        auto batch = store->newBatch();
        if (!batch) return eve::script::projectStatusResult(vm, batch.status(), false, false);
        const auto batchRef = std::move(batch).takeValue();
        StateBatchHandleRef reference{value->reference, batchRef, batchRef.ownerEpoch};
        return makeOwnedProxy<StateBatchHandleRef, ScriptStateBatch>(
            vm, eve::Result<StateBatchHandleRef>::success(reference),
            [](StateBatchHandleRef ref) { return StatePatch::releaseBatch(ref); });
    });

    auto cls = table.addClass(name, StatePatch::create, false);
    expose(cls);
}

void StatePatch::expose(ssq::Class& cls) {
    cls.addFunc("getName", &StatePatch::getName);
    cls.addFunc("newStore", [vm = cls.getHandle()](StatePatch*) -> ssq::Table {
        return makeOwnedProxy<StateStoreHandleRef, ScriptStateStore>(
            vm, StatePatch::newStore(),
            [](StateStoreHandleRef ref) { return StatePatch::release(ref); });
    });
}

}  // namespace eve::statepatch
