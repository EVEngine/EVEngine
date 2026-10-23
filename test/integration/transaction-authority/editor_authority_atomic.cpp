#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editing/EditingAuthority.h"
using EditorStatus = eve::editing::Status;
using EditorValue  = eve::editing::Value;

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

using namespace eve::editing;

namespace {

class IntegerOperationTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit IntegerOperationTarget(std::string id, int value = 0) : id_(std::move(id)), value_(value) {}

    TargetId      targetId() const override { return TargetId(id_); }
    std::uint64_t revision() const override { return revision_; }
    EditRegion    dirtyRegion() const override { return dirty_; }
    void          clearDirtyRegion() override { dirty_.clear(); }

    Result<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type == "test.throw.v1") throw std::runtime_error("injected");
        if (operation.type == "test.fail.v1")
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.failure"),
                                              "Synthetic operation failure");
        if (operation.type != "test.integer.set.v1")
            return eve::editing::failed<void>(EditorStatus::Unsupported, RuleId("test.operation.unsupported"),
                                              "Unsupported operation type");
        const auto* value = operation.payload.getIf<std::int64_t>();
        if (!value)
            return eve::editing::failed<void>(EditorStatus::Rejected, RuleId("test.operation.payload"),
                                              "Integer payload required");
        value_ = static_cast<int>(*value);
        ++revision_;
        dirty_.include(0, 0);
        return eve::editing::applied<void>();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<IntegerOperationTarget>(*this);
    }

    Result<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        if (failCandidateCommit)
            return eve::editing::failed<void>(EditorStatus::Failed, RuleId("test.candidate.publish"),
                                              "Injected candidate publish failure");
        auto* staged = dynamic_cast<IntegerOperationTarget*>(candidate.get());
        if (!staged || staged->id_ != id_)
            return eve::editing::failed<void>(EditorStatus::Conflict, RuleId("test.candidate.identity"),
                                              "Candidate belongs to another target");
        value_    = staged->value_;
        revision_ = staged->revision_ + publicationRevisionStep;
        dirty_    = staged->dirty_;
        return eve::editing::applied<void>();
    }

    int  value() const { return value_; }
    bool failCandidateCommit = false;
    unsigned publicationRevisionStep = 0;

private:
    std::string        id_;
    int                value_    = 0;
    unsigned long long revision_ = 0;
    EditRegion         dirty_;
};

DomainOperation setInteger(const IntegerOperationTarget& target, int before, int after, std::string inverseType = {}) {
    DomainOperation operation;
    operation.type        = "test.integer.set.v1";
    operation.inverseType = std::move(inverseType);
    operation.target      = TargetId(target.targetId());
    operation.payload     = EditorValue(after);
    operation.inverse     = EditorValue(before);
    operation.hasInverse  = true;
    operation.affectedProperties.push_back("value");
    return operation;
}

TransactionSpec transaction(const IntegerOperationTarget& target, const char* id) {
    TransactionSpec specification;
    specification.id           = TransactionId(id);
    specification.label        = "Set integer";
    specification.target       = TargetId(target.targetId());
    specification.baseRevision = target.revision();
    return specification;
}

}  // namespace


TEST_CASE("editor.atomic.candidate_failure_preserves_revision_and_history") {
    IntegerOperationTarget target("atomic");
    LocalWorldAuthority    authority(&target);
    auto                   first = setInteger(target, 0, 9);
    auto                   bad   = first;
    bad.type                     = "test.throw.v1";
    DomainOperation operations[] = {first, bad};
    auto            plan         = authority.preflight(transaction(target, "atomic-failure"), operations);
    REQUIRE(plan.ok());
    auto failed = authority.commit(plan.value());
    CHECK(!failed.ok());
    CHECK_EQ(target.value(), 0);
    CHECK_EQ(target.revision(), 0u);
    operations[1] = setInteger(target, 9, 11);
    auto retry    = authority.preflight(transaction(target, "atomic-failure"), operations);
    REQUIRE(retry.ok());
    auto committed = authority.commit(retry.value());
    REQUIRE(committed.ok());
    CHECK_EQ(target.value(), 11);
    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    CHECK_EQ(target.value(), 0);
}

TEST_CASE("editor.atomic.publish_failure_has_no_effect_and_can_retry") {
    IntegerOperationTarget target("atomic");
    target.publicationRevisionStep = 3;
    LocalWorldAuthority    authority(&target);
    auto                   operation = setInteger(target, 0, 7);
    auto                   plan      = authority.preflight(transaction(target, "publish"), {&operation, 1});
    REQUIRE(plan.ok());
    target.failCandidateCommit = true;
    auto failed                = authority.commit(plan.value());
    CHECK(!failed.ok());
    CHECK_EQ(target.value(), 0);
    CHECK_EQ(target.revision(), 0u);
    target.failCandidateCommit = false;
    auto committed             = authority.commit(plan.value());
    REQUIRE(committed.ok());
    CHECK_EQ(target.value(), 7);
    CHECK_EQ(committed.value().afterRevision, target.revision());
    auto undone = authority.compensate(committed.value());
    REQUIRE(undone.ok());
    CHECK_EQ(target.value(), 0);
}

TEST_CASE("editor.atomic.two_agents_same_revision_only_one_commits") {
    IntegerOperationTarget target("atomic");
    LocalWorldAuthority    authority(&target);
    auto                   a = setInteger(target, 0, 1), b = setInteger(target, 0, 2);
    auto                   pa = authority.preflight(transaction(target, "agent-a"), {&a, 1});
    auto                   pb = authority.preflight(transaction(target, "agent-b"), {&b, 1});
    REQUIRE(pa.ok());
    REQUIRE(pb.ok());
    auto ca = authority.commit(pa.value());
    REQUIRE(ca.ok());
    auto cb = authority.commit(pb.value());
    CHECK(!cb.ok());
    CHECK_EQ(static_cast<int>(cb.code()), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(target.value(), 1);
    CHECK_EQ(target.revision(), 1u);
}

TEST_CASE("editor.atomic.forged_plan_target_rejected_before_mutation") {
    IntegerOperationTarget target("atomic");
    LocalWorldAuthority    authority(&target);
    auto                   operation = setInteger(target, 0, 7);
    auto                   plan      = authority.preflight(transaction(target, "forged"), {&operation, 1});
    REQUIRE(plan.ok());
    plan.value().operations[0].target = TargetId("other");
    auto result                       = authority.commit(plan.value());
    CHECK(!result.ok());
    CHECK_EQ(target.value(), 0);
    CHECK_EQ(target.revision(), 0u);
}

TEST_CASE("editor.atomic.legacy_rollback_exception_is_reported_and_cleanup_continues") {
    class LegacyTarget final : public IDomainOperationTarget {
    public:
        TargetId      targetId() const override { return TargetId("legacy"); }
        std::uint64_t revision() const override { return 0; }
        EditRegion    dirtyRegion() const override { return {}; }
        void          clearDirtyRegion() override {}
        Result<void>  applyDomainOperation(const DomainOperation& op) override {
            if (op.type == "throw") throw std::runtime_error("injected apply exception");
            if (op.type == "undo-fail") throw std::runtime_error("injected rollback exception");
            if (op.type == "undo-ok") ++reverted;
            return eve::editing::applied<void>();
        }
        int reverted = 0;
    } target;
    LocalWorldAuthority authority(&target);
    TransactionSpec     spec;
    spec.id     = TransactionId("legacy-failure");
    spec.target = target.targetId();
    DomainOperation a;
    a.target              = target.targetId();
    a.type                = "apply";
    a.hasInverse          = true;
    a.inverseType         = "undo-ok";
    auto b                = a;
    b.inverseType         = "undo-fail";
    auto c                = a;
    c.type                = "throw";
    DomainOperation ops[] = {a, b, c};
    auto            plan  = authority.preflight(spec, ops);
    REQUIRE(plan.ok());
    auto result = authority.commit(plan.value());
    CHECK(!result.ok());
    CHECK_EQ(target.reverted, 1);
    CHECK_EQ(result.diagnostics().size(), 2u);
}
