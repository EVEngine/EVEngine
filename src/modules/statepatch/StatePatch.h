#pragma once

#include "common/Module.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace eve::statepatch {

/** @brief One validation error produced while applying a patch batch. */
struct PatchError {
    int         operationIndex = -1;
    std::string subject;
    std::string key;
    std::string code;
    std::string message;
};

/** @brief Summary of the most recent batch commit attempt. */
struct PatchResult {
    bool                    success        = false;
    int                     changedCount   = 0;
    uint64_t                revisionBefore = 0;
    uint64_t                revisionAfter  = 0;
    std::vector<PatchError> errors;
};

/** @brief Immutable description of one committed value change. */
struct ChangeEvent {
    uint64_t    sequence = 0;
    uint64_t    revision = 0;
    std::string subject;
    std::string key;
    std::string oldJson;
    std::string newJson;
    bool        removed = false;
};

/** @brief Ordered collection of set and remove operations committed atomically. */
class PatchBatch {
public:
    /** @brief Appends an unconditional set operation. */
    bool set(const std::string& subject, const std::string& key, const std::string& jsonValue);
    /** @brief Appends a set operation guarded by an expected current JSON value. */
    bool setExpected(const std::string& subject, const std::string& key, const std::string& jsonValue,
                     const std::string& expectedJson);
    /** @brief Appends an unconditional remove operation. */
    bool remove(const std::string& subject, const std::string& key);
    /** @brief Appends a remove operation guarded by an expected current JSON value. */
    bool removeExpected(const std::string& subject, const std::string& key, const std::string& expectedJson);
    /** @brief Removes every operation and resets the latest result. */
    void clear();
    /** @brief Returns the number of queued operations. */
    int size() const;
    /** @brief Returns the result of the most recent commit attempt. */
    const PatchResult& result() const;

private:
    friend class Store;
    struct Operation {
        bool                       remove = false;
        std::string                subject;
        std::string                key;
        std::string                value;
        std::optional<std::string> expected;
        std::string                inputError;
    };
    std::vector<Operation> operations_;
    PatchResult            result_;
};

/** @brief Deterministic subject-and-key JSON value store with atomic patching. */
class Store {
public:
    /** @brief Allocates a store-owned empty patch batch. */
    PatchBatch* newBatch();
    /** @brief Validates and atomically commits a batch. */
    bool commit(PatchBatch* batch);
    /** @brief Returns whether a subject and key currently exist. */
    bool has(const std::string& subject, const std::string& key) const;
    /** @brief Returns canonical JSON for a value, or an empty string when absent. */
    std::string get(const std::string& subject, const std::string& key) const;
    /** @brief Returns the revision at which a value last changed, or zero when absent. */
    uint64_t valueRevision(const std::string& subject, const std::string& key) const;
    /** @brief Returns the current global store revision. */
    uint64_t revision() const;
    /** @brief Queries subjects in lexical order. */
    int querySubjects();
    /** @brief Queries keys for a subject in lexical order. */
    int queryKeys(const std::string& subject);
    /** @brief Returns an item from the latest subject or key query. */
    std::string queryAt(int index) const;
    /** @brief Queries dirty subject-key pairs in lexical order. */
    int queryDirty();
    /** @brief Returns the subject for a dirty query item. */
    std::string dirtySubjectAt(int index) const;
    /** @brief Returns the key for a dirty query item. */
    std::string dirtyKeyAt(int index) const;
    /** @brief Clears all dirty-key markers without changing values. */
    void clearDirty();
    /** @brief Returns the number of retained change events. */
    int eventCount() const;
    /** @brief Returns a retained change event, or nullptr. */
    const ChangeEvent* eventAt(int index) const;
    /** @brief Removes retained change events without resetting sequence allocation. */
    void clearEvents();
    /** @brief Exports all persistent store state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores a snapshot produced by snapshotJson(). */
    bool restoreJson(const std::string& json);
    /** @brief Returns the latest snapshot restore error. */
    const std::string& lastError() const;

private:
    struct Value {
        std::string json;
        uint64_t    revision = 0;
    };
    using Values = std::map<std::string, std::map<std::string, Value>>;

    Values                                           values_;
    uint64_t                                         revision_     = 0;
    uint64_t                                         nextSequence_ = 1;
    std::set<std::pair<std::string, std::string>>    dirty_;
    std::vector<ChangeEvent>                         events_;
    std::vector<std::string>                         query_;
    std::vector<std::pair<std::string, std::string>> dirtyQuery_;
    std::vector<std::unique_ptr<PatchBatch>>         batches_;
    std::string                                      lastError_;
};

/** @brief Script module factory for generic state patch stores. */
class StatePatch : public Module {
public:
    Module_REG(StatePatch);
    StatePatch()           = default;
    ~StatePatch() override = default;

    /** @brief Allocates a module-owned state store. */
    static Store* newStore();

private:
    std::vector<std::unique_ptr<Store>> stores_;
};

}  // namespace eve::statepatch
