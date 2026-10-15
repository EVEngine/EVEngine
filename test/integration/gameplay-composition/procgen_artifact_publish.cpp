#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Capability.h"
#include "graphics/ArtifactProvider.h"
#include "procgen/ArtifactPublish.h"
#include "transaction/Transaction.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace eve::procgen;

namespace {

ArtifactId id(const char* text) {
    const auto parsed = ArtifactId::parse(text);
    REQUIRE(parsed.has_value());
    return *parsed;
}

struct Counters {
    int prepared   = 0;
    int committed  = 0;
    int rolledBack = 0;
};

class InjectedTransactionParticipant final : public eve::transaction::ITransactionParticipant {
public:
    explicit InjectedTransactionParticipant(std::string name) : name_(std::move(name)) {}

    std::string_view name() const noexcept override { return name_; }

    eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        ++prepareCalls;
        if (failPrepare)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " prepare"));
        prepared = true;
        return eve::Result<void>::success();
    }

    eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        ++commitCalls;
        if (failCommit)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " commit"));
        prepared  = false;
        committed = true;
        return eve::Result<void>::success();
    }

    eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        ++rollbackCalls;
        if (failRollback)
            return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " rollback"));
        prepared = false;
        return eve::Result<void>::success();
    }

    eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        ++compensateCalls;
        if (failCompensate)
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, name_ + " compensate"));
        committed = false;
        return eve::Result<void>::success();
    }

    bool failPrepare     = false;
    bool failCommit      = false;
    bool failRollback    = false;
    bool failCompensate  = false;
    bool prepared        = false;
    bool committed       = false;
    int  prepareCalls    = 0;
    int  commitCalls     = 0;
    int  rollbackCalls   = 0;
    int  compensateCalls = 0;

private:
    std::string name_;
};

class Stage final : public PreparedArtifactPublish {
public:
    explicit Stage(Counters& counters) : counters_(counters) {}
    void commit() noexcept override { ++counters_.committed; }
    void rollback() noexcept override { ++counters_.rolledBack; }

private:
    Counters& counters_;
};

template <class Interface>
class Adapter final : public Interface {
public:
    explicit Adapter(Counters& counters, bool fail = false) : counters_(counters), fail_(fail) {}
    eve::Result<std::unique_ptr<PreparedArtifactPublish>> prepare(const eve::artifact::PublicationView&) override {
        ++counters_.prepared;
        if (fail_)
            return eve::Result<std::unique_ptr<PreparedArtifactPublish>>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Failed, "injected adapter failure"));
        return eve::Result<std::unique_ptr<PreparedArtifactPublish>>::success(std::make_unique<Stage>(counters_));
    }

private:
    Counters& counters_;
    bool      fail_;
};

Params params() {
    Params value;
    value.setSeed(42);
    value.setInt("width", 6);
    value.setInt("height", 5);
    value.setInt("rings", 1);
    return value;
}

}  // namespace

TEST_CASE("procgen.artifactPublish.missingCapabilityIsExplicitAndAtomic") {
    eve::cap::detail::clearAllRaw();
    auto generated = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd101"));
    REQUIRE(generated.ok());
    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              result = publisher.publish(std::move(generated).takeValue(), {.graphics = true});
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::Unsupported));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.capabilitiesCommitHexAndCastle") {
    eve::cap::detail::clearAllRaw();
    Counters                          sceneCounts, graphicsCounts, physicsCounts;
    Adapter<ISceneArtifactAdapter>    scene(sceneCounts);
    Adapter<IGraphicsArtifactAdapter> graphics(graphicsCounts);
    Adapter<IPhysicsArtifactAdapter>  physics(physicsCounts);
    eve::cap::provide<ISceneArtifactAdapter>(&scene);
    eve::cap::provide<IGraphicsArtifactAdapter>(&graphics);
    eve::cap::provide<IPhysicsArtifactAdapter>(&physics);

    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              hex = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd102"));
    REQUIRE(hex.ok());
    auto first = publisher.publish(std::move(hex).takeValue(), {true, true, true});
    REQUIRE(first.ok());
    std::move(first).takeValue();
    auto castle = generateCastleArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd103"));
    REQUIRE(castle.ok());
    auto second = publisher.publish(std::move(castle).takeValue(), {true, true, true});
    REQUIRE(second.ok());
    std::move(second).takeValue();
    CHECK_EQ(store.size(), std::size_t(2));
    CHECK_EQ(store.partCount(), std::size_t(8));
    CHECK_EQ(sceneCounts.committed, 2);
    CHECK_EQ(graphicsCounts.committed, 2);
    CHECK_EQ(physicsCounts.committed, 2);
}

TEST_CASE("procgen.artifactPublish.adapterFailureRollsBackWithoutStoreState") {
    eve::cap::detail::clearAllRaw();
    Counters                          sceneCounts, graphicsCounts;
    Adapter<ISceneArtifactAdapter>    scene(sceneCounts);
    Adapter<IGraphicsArtifactAdapter> graphics(graphicsCounts, true);
    eve::cap::provide<ISceneArtifactAdapter>(&scene);
    eve::cap::provide<IGraphicsArtifactAdapter>(&graphics);
    auto generated = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd104"));
    REQUIRE(generated.ok());
    ArtifactStore     store;
    ArtifactPublisher publisher(store);
    auto              result = publisher.publish(std::move(generated).takeValue(), {true, true, false});
    CHECK(!result.ok());
    CHECK_EQ(sceneCounts.rolledBack, 1);
    CHECK_EQ(sceneCounts.committed, 0);
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.badDependencyRejectsWholeBatch") {
    auto first  = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd105"));
    auto second = generateCastleArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd106"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    auto artifacts = std::vector<GeneratedArtifact>{};
    artifacts.emplace_back(std::move(first).takeValue());
    artifacts.emplace_back(std::move(second).takeValue());
    auto&         composite = std::get<CompositeArtifact>(artifacts.back().payload);
    ArtifactPart* collider  = const_cast<ArtifactPart*>(composite.find("collider"));
    REQUIRE(collider != nullptr);
    collider->dependencies = {id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd199")};
    ArtifactStore store;
    auto          result = store.publishBatch(std::move(artifacts));
    CHECK(!result.ok());
    CHECK_EQ(static_cast<int>(result.code()), static_cast<int>(eve::StatusCode::NotFound));
    CHECK_EQ(store.size(), std::size_t(0));
    CHECK_EQ(store.partCount(), std::size_t(0));
}

TEST_CASE("procgen.artifactPublish.buildKeyAndPayloadAreDeterministicAcrossIdentity") {
    auto first  = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd107"));
    auto second = generateHexTerrainArtifact(params(), id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd108"));
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    GeneratedArtifact a = std::move(first).takeValue();
    GeneratedArtifact b = std::move(second).takeValue();
    CHECK_EQ(a.buildKey, b.buildKey);
    const auto* aMesh = std::get<CompositeArtifact>(a.payload).find("mesh");
    const auto* bMesh = std::get<CompositeArtifact>(b.payload).find("mesh");
    REQUIRE(aMesh != nullptr);
    REQUIRE(bMesh != nullptr);
    CHECK_EQ(std::get<MeshData>(aMesh->payload).positions(), std::get<MeshData>(bMesh->payload).positions());
    CHECK_EQ(std::get<MeshData>(aMesh->payload).indices(), std::get<MeshData>(bMesh->payload).indices());
}

TEST_CASE("procgen.artifactPublish.coordinatorFailureInjectionCoversPrepareCommitAndRollback") {
    using eve::transaction::ITransactionParticipant;

    InjectedTransactionParticipant first("first");
    InjectedTransactionParticipant prepareFailure("prepare-failure");
    prepareFailure.failPrepare = true;
    std::array<ITransactionParticipant*, 2> prepareParticipants{&first, &prepareFailure};
    eve::transaction::Coordinator           coordinator;
    auto                                    prepareResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-prepare-failure"), prepareParticipants);
    CHECK(!prepareResult.ok());
    CHECK_EQ(first.rollbackCalls, 1);
    CHECK(!first.prepared);

    InjectedTransactionParticipant committed("committed");
    InjectedTransactionParticipant commitFailure("commit-failure");
    commitFailure.failCommit = true;
    std::array<ITransactionParticipant*, 2> commitParticipants{&committed, &commitFailure};
    auto                                    commitResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-commit-failure"), commitParticipants);
    CHECK(!commitResult.ok());
    CHECK_EQ(commitFailure.rollbackCalls, 1);
    CHECK_EQ(committed.compensateCalls, 1);
    CHECK(!committed.committed);

    InjectedTransactionParticipant rollbackFailure("rollback-failure");
    rollbackFailure.failRollback = true;
    InjectedTransactionParticipant trigger("trigger");
    trigger.failPrepare = true;
    std::array<ITransactionParticipant*, 2> rollbackParticipants{&rollbackFailure, &trigger};
    auto                                    rollbackResult =
        coordinator.execute(eve::transaction::TransactionContext("procgen-rollback-failure"), rollbackParticipants);
    CHECK(!rollbackResult.ok());
    CHECK_EQ(rollbackFailure.rollbackCalls, 1);
    CHECK(rollbackFailure.prepared);
    CHECK(rollbackResult.error() != nullptr);
}

TEST_CASE("procgen.artifactPublish.webGpuDescriptorParityUsesCommonMeshView") {
    const auto                       artifact = id("018f0b7e-6e50-7a10-8c22-2c8f8e3dd109");
    const std::vector<float>         positions{0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
    const std::vector<std::uint32_t> indices{0u, 1u, 2u};
    eve::artifact::PartView          part;
    part.id        = eve::PersistentId::fromUuid(artifact);
    part.role      = "mesh";
    part.kind      = eve::artifact::PartKind::MeshData;
    part.buildKey  = "procgen/v1/descriptor/0000000000000001";
    part.positions = positions;
    part.indices   = indices;
    std::array<eve::artifact::PartView, 1> parts{part};
    eve::artifact::PublicationView         publication;
    publication.id       = part.id;
    publication.buildKey = part.buildKey;
    publication.parts    = parts;

    const auto descriptor = eve::graphics::WebGpuArtifactDescriptorAdapter::describe(publication);
    REQUIRE(descriptor.has_value());
    CHECK_EQ(descriptor->id, part.id);
    CHECK_EQ(descriptor->vertexCount, std::size_t(3));
    CHECK_EQ(descriptor->indexCount, std::size_t(3));
    CHECK_NE(descriptor->checksum, std::uint64_t(0));
}
