#include "statepatch/StatePatch.h"

#include "common/Json.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::statepatch {
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

}  // namespace

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

PatchBatch* Store::newBatch() {
    batches_.push_back(std::make_unique<PatchBatch>());
    return batches_.back().get();
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

bool Store::restoreJson(const std::string& json) {
    lastError_.clear();
    auto       document         = eve::json::Document::parse(json, &lastError_);
    const auto root             = document.root();
    uint64_t   restoredRevision = 0;
    uint64_t   restoredSequence = 0;
    if (!document.valid() || !root.isObject() || root.getInt("version") != 1 ||
        !parseU64(root.get("revision"), restoredRevision) || !parseU64(root.get("nextSequence"), restoredSequence) ||
        restoredSequence == 0) {
        if (lastError_.empty()) lastError_ = "invalid state patch snapshot";
        return false;
    }
    const auto values = root.get("values");
    const auto dirty  = root.get("dirty");
    if (!values.isArray() || !dirty.isArray()) {
        lastError_ = "values and dirty must be arrays";
        return false;
    }
    Values                                        restoredValues;
    std::set<std::pair<std::string, std::string>> restoredDirty;
    for (size_t i = 0; i < values.size(); ++i) {
        const auto item          = values.at(i);
        uint64_t   valueRevision = 0;
        if (!item.isObject() || item.getString("subject").empty() || item.getString("key").empty() ||
            !parseU64(item.get("revision"), valueRevision) || valueRevision > restoredRevision || !item.get("value")) {
            lastError_ = "invalid value at index " + std::to_string(i);
            return false;
        }
        const auto subject = item.getString("subject");
        const auto key     = item.getString("key");
        if (restoredValues[subject].contains(key)) {
            lastError_ = "duplicate subject and key";
            return false;
        }
        restoredValues[subject][key] = {canonicalJson(item.get("value")), valueRevision};
    }
    for (size_t i = 0; i < dirty.size(); ++i) {
        const auto item = dirty.at(i);
        if (!item.isArray() || item.size() != 2 || !item.at(0).isString() || !item.at(1).isString() ||
            item.at(0).asString().empty() || item.at(1).asString().empty()) {
            lastError_ = "invalid dirty key at index " + std::to_string(i);
            return false;
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
    return true;
}

const std::string& Store::lastError() const { return lastError_; }

Store* StatePatch::newStore() {
    auto* module = StatePatch::create();
    module->stores_.push_back(std::make_unique<Store>());
    return module->stores_.back().get();
}

Module_IMPL(StatePatch, new StatePatch());

void StatePatch::expose(ssq::Table& table) {
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

    auto store = table.addClass<Store>("StateStore", std::function<Store*()>([] { return nullptr; }), false);
    store.addFunc("newBatch", &Store::newBatch);
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
    store.addFunc("restoreJson", &Store::restoreJson);
    store.addFunc("lastError", &Store::lastError);

    auto cls = table.addClass(name, StatePatch::create, false);
    expose(cls);
}

void StatePatch::expose(ssq::Class& cls) {
    cls.addFunc("getName", &StatePatch::getName);
    cls.addFunc("newStore", [](StatePatch*) { return StatePatch::newStore(); });
}

}  // namespace eve::statepatch
