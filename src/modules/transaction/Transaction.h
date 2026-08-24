#pragma once

#include "common/Module.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace eve::transaction {

/** @brief Lifecycle state of a generic transaction plan. */
enum class State { Open, Validated, Committed, RolledBack, Failed };

/** @brief One inert, script-interpreted operation in a transaction plan. */
struct Operation {
    std::string id;
    std::string kind;
    std::string target;
    std::string payload = "null";
    bool        valid   = false;
    bool        checked = false;
    std::string error;
};

/** @brief Deterministically ordered transaction lifecycle event. */
struct Event {
    uint64_t    sequence = 0;
    std::string transactionId;
    std::string operationId;
    std::string type;
    std::string detail;
};

/**
 * @brief Immutable-after-validation operation plan for script-coordinated work.
 *
 * The plan records intent and validation results only. It deliberately does
 * not execute operations against other engine modules.
 */
class Plan {
public:
    /** @brief Returns the stable ledger-local transaction identifier. */
    const std::string& id() const;
    /** @brief Returns the current lifecycle state. */
    State state() const;
    /** @brief Returns the correlation identifier supplied at creation. */
    const std::string& correlation() const;
    /** @brief Returns the causation identifier supplied at creation. */
    const std::string& causation() const;
    /** @brief Adds an inert operation and returns its stable identifier. */
    std::string stage(const std::string& kind, const std::string& target, const std::string& payloadJson = "null");
    /** @brief Records a successful validation result for an operation. */
    bool markValid(const std::string& operationId);
    /** @brief Records a failed validation result and its diagnostic. */
    bool markInvalid(const std::string& operationId, const std::string& error);
    /** @brief Enters validated state only when every operation was checked and valid. */
    bool validate();
    /** @brief Commits a validated plan without executing its inert operations. */
    bool commit();
    /** @brief Rolls back an open or validated plan with an optional reason. */
    bool rollback(const std::string& reason = {});
    /** @brief Marks a non-terminal plan failed with a diagnostic. */
    bool fail(const std::string& error);
    /** @brief Returns the latest plan-level error or terminal reason. */
    const std::string& error() const;
    /** @brief Returns the number of staged operations. */
    int operationCount() const;
    /** @brief Returns an operation in staging order, or nullptr. */
    const Operation* operationAt(int index) const;
    /** @brief Returns an operation by stable identifier, or nullptr. */
    const Operation* findOperation(const std::string& operationId) const;
    /** @brief Returns the number of deterministic events. */
    int eventCount() const;
    /** @brief Returns an event in sequence order, or nullptr. */
    const Event* eventAt(int index) const;
    /** @brief Exports this plan as deterministic compact JSON. */
    std::string snapshotJson() const;

private:
    friend class Ledger;
    Plan(std::string id, std::string correlation, std::string causation);
    void emit(const std::string& type, const std::string& operationId = {}, const std::string& detail = {});

    std::string           id_;
    State                 state_ = State::Open;
    std::string           correlation_;
    std::string           causation_;
    std::string           error_;
    uint64_t              nextOperation_ = 1;
    uint64_t              nextEvent_     = 1;
    std::deque<Operation> operations_;
    std::deque<Event>     events_;
};

/** @brief Deterministic owner and identifier allocator for transaction plans. */
class Ledger {
public:
    /** @brief Creates an open plan with stable correlation metadata. */
    Plan* create(const std::string& correlation = {}, const std::string& causation = {});
    /** @brief Returns a retained plan by identifier, or nullptr. */
    Plan* find(const std::string& transactionId);
    /** @brief Returns the number of retained plans. */
    int count() const;
    /** @brief Returns a plan in creation order, or nullptr. */
    Plan* at(int index);
    /** @brief Exports all plans and allocator state as deterministic compact JSON. */
    std::string snapshotJson() const;
    /** @brief Transactionally restores a snapshot produced by snapshotJson(). */
    bool restoreJson(const std::string& json);
    /** @brief Returns the latest restore error. */
    const std::string& lastError() const;

private:
    uint64_t                           nextTransaction_ = 1;
    std::vector<std::unique_ptr<Plan>> plans_;
    std::string                        lastError_;
};

/** @brief Returns the stable lowercase name of a transaction state. */
std::string stateName(State state);

/** @brief Script module factory for generic transaction ledgers. */
class Transaction : public Module {
public:
    Module_REG(Transaction);
    Transaction()           = default;
    ~Transaction() override = default;

    /** @brief Allocates a module-owned transaction ledger. */
    static Ledger* newLedger();

private:
    std::vector<std::unique_ptr<Ledger>> ledgers_;
};

}  // namespace eve::transaction
