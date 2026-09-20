#include "graphics/material/editing/MaterialBatchTarget.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::editing;
using namespace eve::material_editing;

namespace {
SelectionSnapshot select(const MaterialBatchTarget& target, std::initializer_list<const char*> ids) {
    SelectionSnapshot result;
    for (const char* id : ids)
        result.items.push_back({SelectionDomain::Asset, target.targetId(), StableId(id), "graphics.material"});
    return result;
}

class BatchSink final : public IMaterialBatchRuntimeSink {
public:
    EditorResult<void> publish(std::span<const MaterialDocumentTarget> candidates) override {
        ++calls;
        observed = candidates.size();
        if (reject)
            return eve::editing::failed<void>(Status::Failed, RuleId("test.material-batch.reject"),
                                              "injected atomic batch rejection");
        return eve::editing::applied<void>();
    }
    bool        reject   = false;
    int         calls    = 0;
    std::size_t observed = 0;
};
}  // namespace

TEST_CASE("editor.material.batch_reports_mixed_and_commits_one_atomic_publication") {
    MaterialDocumentTarget oak("oak");
    MaterialDocumentTarget grass("grass");
    SelectionSnapshot      grassSelection;
    grassSelection.items.push_back({SelectionDomain::Asset, grass.targetId(), StableId("grass"), "graphics.material"});
    auto grassTint = grass.makeSet(grassSelection, PropertyPath("shading.tint"), Value::Array{0.2, 0.4, 0.6, 1.0},
                                   PropertySetMode::Absolute);
    REQUIRE(grassTint.ok());
    REQUIRE(grass.applyDomainOperation(grassTint.value()).ok());

    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {oak, grass}, &sink);
    const auto          both = select(batch, {"oak", "grass"});
    CHECK_EQ(static_cast<int>(batch.read(both, PropertyPath("shading.tint")).state),
             static_cast<int>(PropertyReadState::Mixed));
    const Value sharedTint = Value::Array{0.3, 0.5, 0.7, 1.0};
    auto        operation  = batch.makeSet(both, PropertyPath("shading.tint"), sharedTint, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    auto candidate = batch.cloneDomainState();
    REQUIRE(candidate->applyDomainOperation(operation.value()).ok());
    CHECK_EQ(sink.calls, 0);
    REQUIRE(batch.commitDomainState(std::move(candidate)).ok());
    CHECK_EQ(sink.calls, 1);
    CHECK_EQ(sink.observed, 2U);
    const auto read = batch.read(both, PropertyPath("shading.tint"));
    REQUIRE_EQ(static_cast<int>(read.state), static_cast<int>(PropertyReadState::Value));
    CHECK(read.value == sharedTint);
}

TEST_CASE("editor.material.batch_sink_failure_preserves_every_document") {
    BatchSink           sink;
    MaterialBatchTarget batch("vegetation-materials", {MaterialDocumentTarget("oak"), MaterialDocumentTarget("grass")},
                              &sink);
    const auto          both     = select(batch, {"oak", "grass"});
    const auto          before   = batch.snapshotValue();
    const auto          revision = batch.revision();
    auto                operation =
        batch.makeSet(both, PropertyPath("shading.tint"), Value::Array{0.6, 0.5, 0.4, 1.0}, PropertySetMode::Absolute);
    REQUIRE(operation.ok());
    sink.reject = true;
    CHECK(!batch.applyDomainOperation(operation.value()).ok());
    CHECK_EQ(batch.revision(), revision);
    CHECK(batch.snapshotValue() == before);
}
