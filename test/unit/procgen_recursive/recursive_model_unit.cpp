#include "procgen/recursive/RecursiveInstance.h"
#include "procgen/recursive/RecursiveModel.h"
#include "procgen/recursive/RecursivePersistence.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

using namespace eve::procgen::recursive;

namespace {
// Non-cryptographic test digest. Production hosts inject their chosen common snapshot provider.
eve::Result<eve::ContentId> testDigest(std::string_view bytes) {
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char byte : bytes) hash = (hash ^ byte) * 1099511628211ULL;
    std::array<std::uint8_t, 16> id{};
    for (unsigned i = 0; i < 8; ++i) id[i] = static_cast<std::uint8_t>(hash >> (8 * i));
    id[15] = 1;
    return eve::Result<eve::ContentId>::success(eve::ContentId(id));
}
eve::PersistentId modelId() {
    std::array<std::uint8_t, 16> id{};
    id[15] = 42;
    return eve::PersistentId(id);
}
}  // namespace

TEST_CASE("procgen.recursive.snapshotRoundTripPreservesEditsIdentityAndInvalidatesOldCandidates") {
    ModelDefinition definition;
    definition.capacity       = 137;
    definition.seed           = UINT64_MAX;
    definition.lengthExponent = 900;
    auto instance             = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 2> edits{{{136, 3, {10, 0, 0}, {2, 0, 0}}, {0, 0, {}, {}}}};
    auto                          update = instance.value().prepareEdits(edits);
    REQUIRE(update.ok());
    auto applied = instance.value().commit(std::move(update.value()));
    REQUIRE(applied.ok());
    auto oldCandidate = instance.value().prepareEdits(edits);
    REQUIRE(oldCandidate.ok());
    auto saved = saveRecursiveModel(instance.value(), modelId(), eve::SimulationTick{123}, testDigest);
    REQUIRE(saved.ok());
    auto json = eve::serializeSnapshotEnvelope(saved.value());
    REQUIRE(json.ok());
    auto parsed = eve::parseSnapshotEnvelope(json.value(), testDigest);
    REQUIRE(parsed.ok());
    auto restored = restoreRecursiveModel(parsed.value(), {}, testDigest);
    REQUIRE(restored.ok());
    CHECK(restored.value().instanceId == modelId());
    CHECK_EQ(restored.value().tick.value(), uint64_t{123});
    auto capture = restored.value().instance.capture();
    REQUIRE(capture.ok());
    CHECK_EQ(capture.value().definition.seed, UINT64_MAX);
    CHECK_EQ(capture.value().definition.lengthExponent, 900);
    CHECK_EQ(capture.value().revision.value(), uint64_t{2});
    CHECK_EQ(capture.value().edits.size(), std::size_t{2});
    auto before = instance.value().describe({1});
    auto after  = restored.value().instance.describe({1});
    REQUIRE(before.ok());
    REQUIRE(after.ok());
    CHECK_EQ(before.value().massFraction, after.value().massFraction);
    CHECK_EQ(before.value().centerOfMass[0], after.value().centerOfMass[0]);
    auto resealed = saveRecursiveModel(restored.value().instance, modelId(), eve::SimulationTick{123}, testDigest);
    REQUIRE(resealed.ok());
    auto reserialized = eve::serializeSnapshotEnvelope(resealed.value());
    REQUIRE(reserialized.ok());
    CHECK_EQ(json.value(), reserialized.value());
    instance.value() = std::move(restored.value().instance);
    auto stale       = instance.value().commit(std::move(oldCandidate.value()));
    CHECK(!stale.ok());
}

TEST_CASE("procgen.recursive.snapshotRejectsVersionUnknownFieldsTamperingAndProviderFailure") {
    auto instance = RecursiveInstance::create({}, {});
    REQUIRE(instance.ok());
    auto saved = saveRecursiveModel(instance.value(), modelId(), {}, testDigest);
    REQUIRE(saved.ok());
    auto wrongVersion          = saved.value();
    wrongVersion.schemaVersion = eve::SchemaVersion{2};
    auto version               = restoreRecursiveModel(wrongVersion, {}, testDigest);
    REQUIRE(!version.ok());
    auto unknown = saved.value();
    unknown.payload.set("future", eve::Value(int64_t{1}));
    auto unknownResult = restoreRecursiveModel(unknown, {}, testDigest);
    CHECK(!unknownResult.ok());
    auto tampered = saved.value();
    tampered.payload.find("definition")->set("seed", eve::Value("99"));
    auto hashMismatch = restoreRecursiveModel(tampered, {}, testDigest);
    REQUIRE(!hashMismatch.ok());
    CHECK(hashMismatch.status().primaryDiagnostic()->code() == eve::DiagnosticCode::HashMismatch);
    auto absent = saveRecursiveModel(instance.value(), modelId(), {}, {});
    CHECK(!absent.ok());
    auto failedProvider = saveRecursiveModel(instance.value(), modelId(), {}, [](std::string_view) {
        return eve::Result<eve::ContentId>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected hash failure"));
    });
    CHECK(!failedProvider.ok());
    auto intact = instance.value().capture();
    REQUIRE(intact.ok());
    CHECK_EQ(intact.value().revision.value(), uint64_t{1});
    CHECK(intact.value().edits.empty());
}

TEST_CASE("procgen.recursive.restoreValidatesAuthenticatedStateAndResourceAdmission") {
    ModelDefinition definition;
    definition.capacity = 16;
    auto instance       = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 2> edits{{{0, 2, {}, {}}, {15, 3, {}, {}}}};
    auto                          update = instance.value().prepareEdits(edits);
    REQUIRE(update.ok());
    auto committed = instance.value().commit(std::move(update.value()));
    REQUIRE(committed.ok());
    auto saved = saveRecursiveModel(instance.value(), modelId(), {}, testDigest);
    REQUIRE(saved.ok());
    auto absent = restoreRecursiveModel(saved.value(), {}, {});
    CHECK(!absent.ok());
    EditBudget limited;
    limited.maxEdits = 1;
    auto overBudget  = restoreRecursiveModel(saved.value(), limited, testDigest);
    CHECK(!overBudget.ok());
    limited.maxEdits     = 2;
    limited.maxSummaries = 1;
    auto summaryBudget   = restoreRecursiveModel(saved.value(), limited, testDigest);
    CHECK(!summaryBudget.ok());
    auto payload = saved.value().payload;
    payload.find("definition")->set("capacity", eve::Value("0"));
    const auto& s             = saved.value();
    auto authenticatedInvalid = eve::makeSnapshotEnvelope(s.type, s.schema, s.schemaVersion, s.instanceId, s.revision,
                                                          s.tick, std::move(payload), testDigest);
    REQUIRE(authenticatedInvalid.ok());
    auto invalid = restoreRecursiveModel(authenticatedInvalid.value(), {}, testDigest);
    CHECK(!invalid.ok());
    auto intact = instance.value().capture();
    REQUIRE(intact.ok());
    CHECK_EQ(intact.value().revision.value(), uint64_t{2});
    CHECK_EQ(intact.value().edits.size(), std::size_t{2});
}

TEST_CASE("procgen.recursive.centralMomentsPreserveVolumeAndParallelAxisConservation") {
    ModelDefinition definition;
    definition.capacity = 2;
    auto instance       = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    auto original = instance.value().describe({1});
    REQUIRE(original.ok());
    CHECK(std::abs(original.value().centralSecondMomentPerRootMass[0] - 1.0 / 3) < 1e-14);
    const std::array<LeafEdit, 2> edits{{{0, 1, {10, 10, 10}, {}}, {1, 3, {10, 12, 10}, {}}}};
    auto                          update = instance.value().prepareEdits(edits);
    REQUIRE(update.ok());
    auto committed = instance.value().commit(std::move(update.value()));
    REQUIRE(committed.ok());
    auto root = instance.value().describe({1});
    REQUIRE(root.ok());
    const auto& q = root.value().centralSecondMomentPerRootMass;
    CHECK(std::abs(q[0] - (1.0 / 6 + 0.375)) < 1e-14);
    CHECK(std::abs(q[1] - (2.0 / 3 + 1.5)) < 1e-14);
    CHECK(std::abs(q[2] - 2.0 / 3) < 1e-14);
    CHECK(std::abs(q[3] - 0.75) < 1e-14);
    CHECK_EQ(q[4], 0.0);
    CHECK_EQ(q[5], 0.0);
    auto saved = saveRecursiveModel(instance.value(), modelId(), {}, testDigest);
    REQUIRE(saved.ok());
    auto restored = restoreRecursiveModel(saved.value(), {}, testDigest);
    REQUIRE(restored.ok());
    auto rebuilt = restored.value().instance.describe({1});
    REQUIRE(rebuilt.ok());
    for (unsigned k = 0; k < 6; ++k) CHECK_EQ(rebuilt.value().centralSecondMomentPerRootMass[k], q[k]);
    const std::array<LeafEdit, 1> overflow{{{0, 1, {1e200, 0, 0}, {}}}};
    auto                          rejected = instance.value().prepareEdits(overflow);
    CHECK(!rejected.ok());
    auto intact = instance.value().describe({1});
    REQUIRE(intact.ok());
    CHECK_EQ(intact.value().centralSecondMomentPerRootMass[0], q[0]);
    const std::array<LeafEdit, 2> removed{{{0, 0, {}, {}}, {1, 0, {}, {}}}};
    auto                          deletion = instance.value().prepareEdits(removed);
    REQUIRE(deletion.ok());
    auto deleted = instance.value().commit(std::move(deletion.value()));
    REQUIRE(deleted.ok());
    auto empty = instance.value().describe({1});
    REQUIRE(empty.ok());
    for (double moment : empty.value().centralSecondMomentPerRootMass) CHECK_EQ(moment, 0.0);
}

TEST_CASE("procgen.recursive.editsPublishAtomicallyAndSurviveCoarseFrontiers") {
    ModelDefinition definition;
    definition.capacity = 137;
    auto instance       = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 2> edits{{{136, 3, {10, 0, 0}, {2, 0, 0}}, {0, 0, {}, {}}}};
    auto                          candidate = instance.value().prepareEdits(edits);
    REQUIRE(candidate.ok());
    auto unchanged = instance.value().capture();
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().edits.empty());
    auto committed = instance.value().commit(std::move(candidate.value()));
    REQUIRE(committed.ok());
    auto root = instance.value().describe({1});
    REQUIRE(root.ok());
    CHECK(std::abs(root.value().massFraction - 138.0 / 137.0) < 1e-14);
    CHECK(std::abs(root.value().momentumPerRootMass[0] - 6.0 / 137.0) < 1e-14);
    CHECK(root.value().boundsMax[0] > 9);
    const std::array<NodeKey, 2> keys{{{2}, {3}}};
    auto                         frontier = instance.value().prepareFrontier(keys, 2);
    REQUIRE(frontier.ok());
    double mass = 0, moment = 0, momentum = 0;
    for (const auto& source : frontier.value()) {
        mass += source.massFraction;
        moment += source.massFraction * source.centerOfMass[0];
        momentum += source.momentumPerRootMass[0];
    }
    CHECK(std::abs(mass - root.value().massFraction) < 1e-14);
    CHECK(std::abs(moment - root.value().massFraction * root.value().centerOfMass[0]) < 1e-14);
    CHECK_EQ(momentum, root.value().momentumPerRootMass[0]);
    auto capture = instance.value().capture();
    REQUIRE(capture.ok());
    CHECK_EQ(capture.value().edits[0].ordinal, uint64_t{0});
    CHECK_EQ(capture.value().edits[1].ordinal, uint64_t{136});
    CHECK_EQ(capture.value().revision.value(), uint64_t{2});
    auto reset = instance.value().prepareEdits({});
    REQUIRE(reset.ok());
    auto resetCommit = instance.value().commit(std::move(reset.value()));
    REQUIRE(resetCommit.ok());
    auto restoredBase = instance.value().describe({1});
    REQUIRE(restoredBase.ok());
    CHECK_EQ(restoredBase.value().massFraction, 1.0);
}

TEST_CASE("procgen.recursive.destroyedSubtreeHasExactZeroMass") {
    ModelDefinition definition;
    definition.capacity = 137;
    auto instance       = RecursiveInstance::create(definition, {});
    REQUIRE(instance.ok());
    std::vector<LeafEdit> edits;
    for (uint64_t i = 0; i < definition.capacity; ++i) edits.push_back({i, 0, {}, {}});
    auto candidate = instance.value().prepareEdits(edits);
    REQUIRE(candidate.ok());
    auto committed = instance.value().commit(std::move(candidate.value()));
    REQUIRE(committed.ok());
    auto root = instance.value().describe({1});
    REQUIRE(root.ok());
    CHECK_EQ(root.value().massFraction, 0.0);
    CHECK_EQ(root.value().mass.mantissa, 0.0);
    edits.back().massScale       = 1e-20;
    edits.back().displacement[0] = 100;
    auto tiny                    = instance.value().prepareEdits(edits);
    REQUIRE(tiny.ok());
    auto tinyCommit = instance.value().commit(std::move(tiny.value()));
    REQUIRE(tinyCommit.ok());
    auto tinyRoot = instance.value().describe({1});
    REQUIRE(tinyRoot.ok());
    auto base = RecursiveModel::build(definition);
    REQUIRE(base.ok());
    auto leaf = base.value().leaf(136);
    REQUIRE(leaf.ok());
    CHECK(std::abs(tinyRoot.value().centerOfMass[0] - leaf.value().center[0] - 100) < 1e-12);
}

TEST_CASE("procgen.recursive.foreignAndSupersededEditsNeverPublish") {
    auto first  = RecursiveInstance::create({}, {});
    auto second = RecursiveInstance::create({}, {});
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    const std::array<LeafEdit, 1> edits{{{0, 2, {}, {}}}};
    auto                          foreign = first.value().prepareEdits(edits);
    REQUIRE(foreign.ok());
    auto rejected = second.value().commit(std::move(foreign.value()));
    REQUIRE(!rejected.ok());
    auto a = first.value().prepareEdits(edits);
    auto b = first.value().prepareEdits(edits);
    REQUIRE(a.ok());
    REQUIRE(b.ok());
    auto applied = first.value().commit(std::move(a.value()));
    REQUIRE(applied.ok());
    auto stale = first.value().commit(std::move(b.value()));
    CHECK(!stale.ok());
    auto orphan = [&]() {
        auto owner = RecursiveInstance::create({}, {});
        auto live  = std::move(owner).expect("Create owner for destruction test");
        return live.prepareEdits(edits);
    }();
    REQUIRE(orphan.ok());
    auto expired = second.value().commit(std::move(orphan.value()));
    CHECK(!expired.ok());
}

TEST_CASE("procgen.recursive.cancelMoveAndRevisionExhaustionAreExplicit") {
    auto instance = RecursiveInstance::create({}, {});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 1> edits{{{0, 2, {}, {}}}};
    {
        auto cancelled = instance.value().prepareEdits(edits);
        REQUIRE(cancelled.ok());
    }
    auto unchanged = instance.value().describe({1});
    REQUIRE(unchanged.ok());
    CHECK_EQ(unchanged.value().massFraction, 1.0);
    auto candidate = instance.value().prepareEdits(edits);
    REQUIRE(candidate.ok());
    auto moved     = std::move(instance.value());
    auto committed = moved.commit(std::move(candidate.value()));
    REQUIRE(committed.ok());
    ModelDefinition definition;
    definition.revision = eve::Revision{std::numeric_limits<uint64_t>::max()};
    auto exhausted      = RecursiveInstance::create(definition, {});
    REQUIRE(exhausted.ok());
    auto rejected = exhausted.value().prepareEdits(edits);
    CHECK(!rejected.ok());
}

TEST_CASE("procgen.recursive.invalidOrOverBudgetEditsPreservePublishedState") {
    ModelDefinition definition;
    definition.capacity = 8;
    auto instance       = RecursiveInstance::create(definition, {2, 1});
    REQUIRE(instance.ok());
    const std::array<LeafEdit, 1> edits{{{0, 2, {}, {}}}};
    auto                          overBudget = instance.value().prepareEdits(edits);
    REQUIRE(!overBudget.ok());
    auto unchanged = instance.value().capture();
    REQUIRE(unchanged.ok());
    CHECK(unchanged.value().edits.empty());
    CHECK_EQ(unchanged.value().revision.value(), uint64_t{1});
    auto normal = RecursiveInstance::create(definition, {});
    REQUIRE(normal.ok());
    const std::array<LeafEdit, 2> duplicates{{edits[0], edits[0]}};
    auto                          duplicate = normal.value().prepareEdits(duplicates);
    CHECK(!duplicate.ok());
    const std::array<LeafEdit, 1> tiny{{{0, std::numeric_limits<double>::denorm_min(), {}, {}}}};
    auto                          underflow = normal.value().prepareEdits(tiny);
    CHECK(!underflow.ok());
}

TEST_CASE("procgen.recursive.refinementConservesMassAndFirstMoment") {
    ModelDefinition definition;
    definition.capacity       = 137;
    definition.baseHalfExtent = {8, 3, 2};
    auto model                = RecursiveModel::build(definition);
    REQUIRE(model.ok());
    std::vector<NodeKey> frontier{{1}};
    double               greatestError = 0;
    while (frontier.size() < definition.capacity) {
        bool refined = false;
        for (std::size_t i = 0; i < frontier.size(); ++i) {
            auto parent = model.value().describe(frontier[i]);
            REQUIRE(parent.ok());
            if (parent.value().leafCount == 1) continue;
            auto children = model.value().split(frontier[i]);
            REQUIRE(children.ok());
            const auto& a = children.value()[0];
            const auto& b = children.value()[1];
            CHECK_EQ(a.leafCount + b.leafCount, parent.value().leafCount);
            CHECK(std::abs(a.rootMassFraction + b.rootMassFraction - parent.value().rootMassFraction) < 1e-15);
            for (unsigned axis = 0; axis < 3; ++axis) {
                const auto error = std::abs(a.rootMassFraction * a.center[axis] + b.rootMassFraction * b.center[axis] -
                                            parent.value().rootMassFraction * parent.value().center[axis]);
                greatestError    = std::max(greatestError, error);
            }
            frontier[i] = a.key;
            frontier.push_back(b.key);
            refined = true;
            break;
        }
        REQUIRE(refined);
    }
    auto published = model.value().prepareFrontier(frontier, 137);
    REQUIRE(published.ok());
    CHECK_EQ(published.value().size(), std::size_t{137});
    CHECK(greatestError < 1e-14);
    double                totalMass = 0;
    std::array<double, 3> moment{};
    for (const auto& node : published.value()) {
        totalMass += node.rootMassFraction;
        for (unsigned axis = 0; axis < 3; ++axis) moment[axis] += node.rootMassFraction * node.center[axis];
    }
    CHECK(std::abs(totalMass - 1) < 1e-14);
    CHECK(std::hypot(moment[0], moment[1], moment[2]) < 1e-14);
}

TEST_CASE("procgen.recursive.capacityAndPhysicalScaleAreIndependent") {
    ModelDefinition definition;
    auto            ordinary = RecursiveModel::build(definition);
    REQUIRE(ordinary.ok());
    auto a = ordinary.value().describe({1});
    REQUIRE(a.ok());
    definition.lengthExponent = 900;
    auto immense              = RecursiveModel::build(definition);
    REQUIRE(immense.ok());
    auto b = immense.value().describe({1});
    REQUIRE(b.ok());
    CHECK_EQ(a.value().halfExtent[0], b.value().halfExtent[0]);
    CHECK_EQ(a.value().leafCount, b.value().leafCount);
    CHECK_EQ(b.value().mass.exponent - a.value().mass.exponent, 2700);
    definition.lengthExponent = 0;
    definition.capacity       = 8;
    definition.capacityMode   = CapacityMode::FixedMeanLeafVolume;
    auto settlement           = RecursiveModel::build(definition);
    REQUIRE(settlement.ok());
    auto town = settlement.value().describe({1});
    REQUIRE(town.ok());
    CHECK(std::abs(town.value().halfExtent[0] - 2) < 1e-14);
    CHECK_EQ(town.value().leafCount, uint64_t{8});
    CHECK_EQ(town.value().mass.exponent - a.value().mass.exponent, 3);
}

TEST_CASE("procgen.recursive.lazySummaryDoesNotEnumeratePotentialCapacity") {
    ModelDefinition definition;
    definition.capacity = uint64_t{1} << 52;
    auto model          = RecursiveModel::build(definition);
    REQUIRE(model.ok());
    auto root = model.value().describe({1});
    REQUIRE(root.ok());
    CHECK_EQ(root.value().leafCount, definition.capacity);
    auto leaf = model.value().describe({uint64_t{1} << 52});
    REQUIRE(leaf.ok());
    CHECK_EQ(leaf.value().leafCount, uint64_t{1});
    CHECK_EQ(leaf.value().rootMassFraction, 0x1p-52);
    auto terminal = model.value().split(leaf.value().key);
    CHECK(!terminal.ok());
}

TEST_CASE("procgen.recursive.frontierRejectsDuplicateMassAndMissingResidual") {
    ModelDefinition definition;
    definition.capacity = 16;
    auto model          = RecursiveModel::build(definition);
    REQUIRE(model.ok());
    const std::array<NodeKey, 3> complete{{{7}, {2}, {6}}};
    auto                         accepted = model.value().prepareFrontier(complete, 3);
    REQUIRE(accepted.ok());
    CHECK_EQ(accepted.value()[0].key.path, uint64_t{2});
    const std::array<NodeKey, 2> duplicate{{{1}, {2}}};
    auto                         overlap = model.value().prepareFrontier(duplicate, 2);
    CHECK(!overlap.ok());
    const std::array<NodeKey, 1> missing{{{2}}};
    auto                         hole = model.value().prepareFrontier(missing, 1);
    CHECK(!hole.ok());
    auto budget = model.value().prepareFrontier(complete, 2);
    CHECK(!budget.ok());
}

TEST_CASE("procgen.recursive.randomStreamsAreIndependentOfTraversalOrder") {
    ModelDefinition definition;
    definition.capacity = 100;
    definition.seed     = 12345;
    auto model          = RecursiveModel::build(definition);
    REQUIRE(model.ok());
    auto first = model.value().randomWord({3}, RandomStream::Geometry, 17);
    REQUIRE(first.ok());
    CHECK_EQ(first.value(), uint64_t{6882116831085870244ULL});
    auto other = model.value().randomWord({2}, RandomStream::Geometry, 17);
    REQUIRE(other.ok());
    auto same = model.value().randomWord({3}, RandomStream::Geometry, 17);
    REQUIRE(same.ok());
    CHECK_EQ(first.value(), same.value());
    CHECK(first.value() != other.value());
    auto material = model.value().randomWord({3}, RandomStream::Material, 17);
    REQUIRE(material.ok());
    CHECK(material.value() != first.value());
    definition.ruleVersion = 2;
    auto unknown           = RecursiveModel::build(definition);
    CHECK(!unknown.ok());
    definition.ruleVersion = 1;
    definition.density     = std::numeric_limits<double>::infinity();
    auto nonfinite         = RecursiveModel::build(definition);
    CHECK(!nonfinite.ok());
}
