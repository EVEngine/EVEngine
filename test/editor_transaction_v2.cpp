#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "editor/EditorAuthority.h"
#include "editor/EditorCommandService.h"
#include "editor/EditorSession.h"
#include "editor/EditorTransactionService.h"

#include <cstdint>
#include <memory>
#include <string>

using namespace eve::editor;

namespace {

class IntegerOperationTarget final : public IDomainOperationTarget, public IDomainOperationTargetStaging {
public:
    explicit IntegerOperationTarget(std::string id, int value = 0) : id_(std::move(id)), value_(value) {}

    const std::string& targetId() const override { return id_; }
    unsigned long long revision() const override { return revision_; }
    EditRegion         dirtyRegion() const override { return dirty_; }
    void               clearDirtyRegion() override { dirty_.clear(); }

    EditorResult<void> applyDomainOperation(const DomainOperation& operation) override {
        if (operation.type == "test.fail.v1")
            return EditorResult<void>::error(EditorStatus::Rejected, RuleId("test.operation.failure"),
                                             "Synthetic operation failure");
        if (operation.type != "test.integer.set.v1")
            return EditorResult<void>::error(EditorStatus::Unsupported, RuleId("test.operation.unsupported"),
                                             "Unsupported operation type");
        const auto* value = operation.payload.getIf<std::int64_t>();
        if (!value)
            return EditorResult<void>::error(EditorStatus::Rejected, RuleId("test.operation.payload"),
                                             "Integer payload required");
        value_ = static_cast<int>(*value);
        ++revision_;
        dirty_.include(0, 0);
        return EditorResult<void>::applied();
    }

    std::unique_ptr<IDomainOperationTarget> cloneDomainState() const override {
        return std::make_unique<IntegerOperationTarget>(*this);
    }

    EditorResult<void> commitDomainState(std::unique_ptr<IDomainOperationTarget> candidate) override {
        if (failCandidateCommit)
            return EditorResult<void>::error(EditorStatus::Failed, RuleId("test.candidate.publish"),
                                             "Injected candidate publish failure");
        auto* staged = dynamic_cast<IntegerOperationTarget*>(candidate.get());
        if (!staged || staged->id_ != id_)
            return EditorResult<void>::error(EditorStatus::Conflict, RuleId("test.candidate.identity"),
                                             "Candidate belongs to another target");
        value_    = staged->value_;
        revision_ = staged->revision_;
        dirty_    = staged->dirty_;
        return EditorResult<void>::applied();
    }

    int value() const { return value_; }
    bool failCandidateCommit = false;

private:
    std::string        id_;
    int                value_    = 0;
    unsigned long long revision_ = 0;
    EditRegion         dirty_;
};

DomainOperation setInteger(const IntegerOperationTarget& target, int before, int after, std::string inverseType = {}) {
    DomainOperation operation;
    operation.type       = "test.integer.set.v1";
    operation.inverseType = std::move(inverseType);
    operation.target     = TargetId(target.targetId());
    operation.payload    = EditorValue(after);
    operation.inverse    = EditorValue(before);
    operation.hasInverse = true;
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

TEST_CASE("editor.v2.authority_preflight_commit_and_compensate") {
    IntegerOperationTarget target("integer", 1);
    LocalWorldAuthority    authority(&target);
    DomainOperation        operation     = setInteger(target, 1, 5);
    TransactionSpec        specification = transaction(target, "transaction.one");

    auto plan = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK(plan.accepted());
    CHECK_EQ(target.value(), 1);
    auto committed = authority.commit(*plan.value);
    CHECK(committed.accepted());
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(committed.value->beforeRevision, static_cast<Revision>(0));
    CHECK_EQ(committed.value->afterRevision, static_cast<Revision>(1));

    target.failCandidateCommit                  = true;
    const auto valueBeforeFailedCompensation    = target.value();
    const auto revisionBeforeFailedCompensation = target.revision();
    auto       failedCompensation               = authority.compensate(*committed.value);
    CHECK(!failedCompensation.accepted());
    CHECK_EQ(target.value(), valueBeforeFailedCompensation);
    CHECK_EQ(target.revision(), revisionBeforeFailedCompensation);

    target.failCandidateCommit = false;
    auto compensated = authority.compensate(*committed.value);
    CHECK(compensated.accepted());
    CHECK_EQ(target.value(), 1);
    CHECK_EQ(compensated.value->afterRevision, static_cast<Revision>(2));

    auto conflict = authority.preflight(specification, std::span<const DomainOperation>(&operation, 1));
    CHECK_EQ(static_cast<int>(conflict.status), static_cast<int>(EditorStatus::Conflict));
}

TEST_CASE("editor.v2.authority_compensation_validates_full_inverse_on_candidate") {
    IntegerOperationTarget target("integer", 0);
    LocalWorldAuthority    authority(&target);
    const DomainOperation  operations[] = {
        setInteger(target, 0, 9, "test.fail.v1"),
        setInteger(target, 9, 5),
    };

    auto plan = authority.preflight(transaction(target, "transaction.candidate-failure"), operations);
    CHECK(plan.accepted());
    auto committed = authority.commit(*plan.value);
    CHECK(committed.accepted());
    CHECK_EQ(target.value(), 5);
    const auto revisionAfterCommit = target.revision();

    auto compensation = authority.compensate(*committed.value);
    CHECK(!compensation.accepted());
    CHECK_EQ(static_cast<int>(compensation.status), static_cast<int>(EditorStatus::Failed));
    CHECK_EQ(target.value(), 5);
    CHECK_EQ(target.revision(), revisionAfterCommit);
}

TEST_CASE("editor.v2.authority_rolls_back_partial_commit") {
    IntegerOperationTarget target("integer");
    LocalWorldAuthority    authority(&target);
    DomainOperation        first = setInteger(target, 0, 9);
    DomainOperation        failure;
    failure.type                       = "test.fail.v1";
    failure.target                     = TargetId(target.targetId());
    const DomainOperation operations[] = {first, failure};

    auto plan = authority.preflight(transaction(target, "transaction.rollback"), operations);
    CHECK(plan.accepted());
    auto result = authority.commit(*plan.value);
    CHECK(!result.accepted());
    CHECK_EQ(target.value(), 0);
    CHECK(result.value.has_value());
    CHECK_EQ(static_cast<int>(result.value->state), static_cast<int>(TransactionState::Rejected));
}

TEST_CASE("editor.v2.local_transaction_backend_undo_and_redo") {
    IntegerOperationTarget  target("integer");
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);

    CHECK(backend.begin(transaction(target, "transaction.history")).accepted());
    CHECK(backend.append(setInteger(target, 0, 7)).accepted());
    auto committed = backend.commit();
    CHECK(committed.accepted());
    CHECK_EQ(target.value(), 7);
    CHECK(backend.canUndo());

    auto undone = backend.undo();
    CHECK(undone.accepted());
    CHECK_EQ(target.value(), 0);
    CHECK(backend.canRedo());

    auto redone = backend.redo();
    CHECK(redone.accepted());
    CHECK_EQ(target.value(), 7);
}

TEST_CASE("editor.v2.planned_command_is_dry_run_and_revision_safe") {
    IntegerOperationTarget  target("integer", 2);
    LocalWorldAuthority     authority(&target);
    LocalTransactionBackend backend(&authority);
    EditorCommandService    commands;

    CommandDescriptor descriptor;
    descriptor.id          = CommandId("test.integer.set");
    descriptor.ownerModule = "test";
    CHECK(commands
              .registerPlannedCommand(
                  descriptor,
                  [&](const CommandRequest& request) {
                      const auto* after = request.payload.getIf<std::int64_t>();
                      if (!after)
                          return EditorResult<CommandPlan>::error(
                              EditorStatus::Rejected, RuleId("test.command.payload"), "Integer payload required");
                      CommandPlan plan;
                      plan.operations.push_back(setInteger(target, target.value(), static_cast<int>(*after)));
                      return EditorResult<CommandPlan>::applied(std::move(plan));
                  },
                  [&](const CommandRequest&, const CommandPlan& plan) {
                      TransactionSpec specification;
                      specification.id           = TransactionId(plan.id.value());
                      specification.label        = "Planned integer change";
                      specification.target       = plan.target;
                      specification.baseRevision = plan.baseRevision;
                      auto begun                 = backend.begin(std::move(specification));
                      if (!begun.accepted())
                          return EditorResult<TransactionReceipt>::error(begun.status, RuleId("test.command.begin"),
                                                                         "Could not begin transaction");
                      for (const DomainOperation& operation : plan.operations) {
                          auto appended = backend.append(operation);
                          if (!appended.accepted()) {
                              auto rolledBack = backend.rollback();
                              if (!rolledBack.accepted())
                                  return EditorResult<TransactionReceipt>::error(rolledBack.status,
                                                                                 RuleId("test.command.rollback"),
                                                                                 "Could not roll back transaction");
                              return EditorResult<TransactionReceipt>::error(
                                  appended.status, RuleId("test.command.append"), "Could not append operation");
                          }
                      }
                      return backend.commit();
                  })
              .accepted());

    EditorSession session;
    session.setSessionId(SessionId("test-session"));
    session.setCommandService(&commands);
    session.bindTarget(&target);

    auto plan = session.planCommand(descriptor.id, EditorValue(11), CommandSource::Script, target.revision());
    CHECK(plan.accepted());
    CHECK_EQ(target.value(), 2);
    auto executed = session.executePlan(*plan.value, EditorValue(11), CommandSource::Script);
    CHECK(executed.accepted());
    CHECK_EQ(target.value(), 11);
    CHECK_EQ(static_cast<int>(executed.value->state), static_cast<int>(TransactionState::Committed));

    auto stalePlan = session.planCommand(descriptor.id, EditorValue(15));
    CHECK(stalePlan.accepted());
    DomainOperation external = setInteger(target, 11, 12);
    CHECK(target.applyDomainOperation(external).accepted());
    auto stale = session.executePlan(*stalePlan.value, EditorValue(15));
    CHECK_EQ(static_cast<int>(stale.status), static_cast<int>(EditorStatus::Conflict));
    CHECK_EQ(target.value(), 12);
}

TEST_CASE("editor.v2.read_only_authority_rejects_mutation") {
    ReadOnlyAuthority authority;
    TransactionSpec   specification;
    specification.id     = TransactionId("read-only");
    specification.target = TargetId("target");
    auto result          = authority.preflight(specification, {});
    CHECK_EQ(static_cast<int>(result.status), static_cast<int>(EditorStatus::Rejected));
}
