#include "procgen/ArtifactPublish.h"

#include "common/Capability.h"
#include "transaction/Transaction.h"

#include <exception>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve::procgen {
namespace {

struct PublicationBuffer {
    std::vector<eve::artifact::PartView>           parts;
    std::vector<std::vector<eve::artifact::Point>> pointBuffers;
    eve::artifact::PublicationView                 view;
};

eve::artifact::PartKind partKind(ArtifactType type) {
    switch (type) {
        case ArtifactType::Grid: return eve::artifact::PartKind::Grid;
        case ArtifactType::PointSet: return eve::artifact::PartKind::PointSet;
        case ArtifactType::MeshData: return eve::artifact::PartKind::MeshData;
        case ArtifactType::ImageData: return eve::artifact::PartKind::ImageData;
        case ArtifactType::Collider: return eve::artifact::PartKind::Collider;
        case ArtifactType::Composite: break;
    }
    return eve::artifact::PartKind::Grid;
}

eve::artifact::Bounds boundsView(const Bounds& bounds) {
    return {bounds.minX, bounds.minY, bounds.minZ, bounds.maxX, bounds.maxY, bounds.maxZ, bounds.valid};
}

void appendPartView(const ArtifactPart& source, PublicationBuffer& buffer) {
    eve::artifact::PartView view;
    view.id            = eve::PersistentId::fromUuid(source.id);
    view.role          = source.role;
    view.kind          = partKind(source.type);
    view.schemaVersion = source.schemaVersion.value();
    view.buildKey      = source.buildKey.format();
    view.bounds        = boundsView(source.bounds);
    std::visit(
        [&view, &buffer](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, MeshData>) {
                view.positions = payload.positions();
                view.normals   = payload.normals();
                view.uvs       = payload.uvs();
                view.indices   = payload.indices();
            } else if constexpr (std::is_same_v<T, Collider>) {
                view.positions = payload.vertices;
                view.indices   = payload.indices;
            } else if constexpr (std::is_same_v<T, Grid2D>) {
                view.width  = payload.getWidth();
                view.height = payload.getHeight();
                view.cells  = payload.cells();
            } else if constexpr (std::is_same_v<T, PointSet>) {
                buffer.pointBuffers.emplace_back();
                auto& points = buffer.pointBuffers.back();
                points.reserve(payload.points().size());
                for (const auto& point : payload.points()) points.push_back({point.x, point.y, point.z});
                view.points = points;
            }
        },
        source.payload);
    buffer.parts.emplace_back(view);
}

void appendLeafView(const GeneratedArtifact& source, PublicationBuffer& buffer) {
    eve::artifact::PartView view;
    view.id            = eve::PersistentId::fromUuid(source.id);
    view.role          = artifactTypeName(source.type);
    view.kind          = partKind(source.type);
    view.schemaVersion = source.schemaVersion.value();
    view.buildKey      = source.buildKey.format();
    view.bounds        = boundsView(source.bounds);
    std::visit(
        [&view, &buffer](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, MeshData>) {
                view.positions = payload.positions();
                view.normals   = payload.normals();
                view.uvs       = payload.uvs();
                view.indices   = payload.indices();
            } else if constexpr (std::is_same_v<T, Collider>) {
                view.positions = payload.vertices;
                view.indices   = payload.indices;
            } else if constexpr (std::is_same_v<T, Grid2D>) {
                view.width  = payload.getWidth();
                view.height = payload.getHeight();
                view.cells  = payload.cells();
            } else if constexpr (std::is_same_v<T, PointSet>) {
                buffer.pointBuffers.emplace_back();
                auto& points = buffer.pointBuffers.back();
                points.reserve(payload.points().size());
                for (const auto& point : payload.points()) points.push_back({point.x, point.y, point.z});
                view.points = points;
            }
        },
        source.payload);
    buffer.parts.emplace_back(view);
}

PublicationBuffer makePublicationBuffer(const GeneratedArtifact& artifact) {
    PublicationBuffer buffer;
    if (artifact.type == ArtifactType::Composite) {
        const auto& composite = std::get<CompositeArtifact>(artifact.payload);
        buffer.parts.reserve(composite.children.size());
        buffer.pointBuffers.reserve(composite.children.size());
        for (const auto& part : composite.children) appendPartView(part, buffer);
    } else {
        buffer.parts.reserve(1);
        buffer.pointBuffers.reserve(1);
        appendLeafView(artifact, buffer);
    }
    buffer.view.id            = eve::PersistentId::fromUuid(artifact.id);
    buffer.view.schemaVersion = artifact.schemaVersion.value();
    buffer.view.buildKey      = artifact.buildKey.format();
    buffer.view.bounds        = boundsView(artifact.bounds);
    buffer.view.parts         = std::span<const eve::artifact::PartView>(buffer.parts.data(), buffer.parts.size());
    return buffer;
}

eve::Result<void> participantFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

/** @brief Adapts one PreparedPublication to the common transaction protocol. */
class ProviderParticipant final : public eve::transaction::ITransactionParticipant {
public:
    ProviderParticipant(std::string name, eve::artifact::ProviderContract& provider,
                        const eve::artifact::PublicationView& view)
        : name_(std::move(name)), provider_(&provider), view_(view) {}

    ~ProviderParticipant() override {
        if (stage_) stage_->rollback();
    }

    std::string_view name() const noexcept override { return name_; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact provider participant is not idle", name_ + ".prepare");
        // Do not initialize a placeholder Result and then move-assign over it:
        // in assertion builds move assignment intentionally rejects overwriting
        // an unobserved Result.  Returning from this lambda constructs the one
        // authoritative Result directly while still converting provider throws
        // into the publication diagnostic.
        auto prepared = [&]() -> eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> {
            try {
                return provider_->prepare(view_);
            } catch (const std::exception& error) {
                return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, std::string("artifact provider prepare threw: ") + error.what(),
                    name_ + ".prepare"));
            } catch (...) {
                return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::Failed, "artifact provider prepare threw", name_ + ".prepare"));
            }
        }();
        if (!prepared.ok()) return eve::Result<void>::failure(prepared.status());
        stage_ = std::move(prepared).takeValue();
        if (!stage_)
            return participantFailure(eve::DiagnosticCode::InvariantViolation,
                                      "artifact provider returned a null prepared stage", name_ + ".prepare");
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared || !stage_)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact provider participant is not prepared", name_ + ".commit");
        stage_->commit();
        stage_.reset();
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact provider participant is not prepared", name_ + ".rollback");
        stage_->rollback();
        stage_.reset();
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact provider participant is not committed", name_ + ".compensate");
        return participantFailure(eve::DiagnosticCode::Unsupported,
                                  "artifact provider publication has no inverse compensation", name_ + ".compensate");
    }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack };

    std::string                                         name_;
    eve::artifact::ProviderContract*                    provider_ = nullptr;
    const eve::artifact::PublicationView&               view_;
    std::unique_ptr<eve::artifact::PreparedPublication> stage_;
    Phase                                               phase_ = Phase::Idle;
};

/** @brief Adapts the store's PreparedPublication to the common coordinator. */
class StoreParticipant final : public eve::transaction::ITransactionParticipant {
public:
    StoreParticipant(ArtifactStore& store, GeneratedArtifact artifact)
        : store_(&store), artifact_(std::move(artifact)) {}

    ~StoreParticipant() override {
        if (stage_) stage_->rollback();
    }

    std::string_view name() const noexcept override { return "procgen.artifact-store"; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store participant is not idle", "procgen.artifact-store.prepare");
        auto staged = store_->stagePublish(std::move(artifact_));
        if (!staged.ok()) return eve::Result<void>::failure(staged.status());
        stage_ = std::move(staged).takeValue();
        if (!stage_)
            return participantFailure(eve::DiagnosticCode::InvariantViolation,
                                      "artifact store returned a null prepared stage",
                                      "procgen.artifact-store.prepare");
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared || !stage_)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store participant is not prepared", "procgen.artifact-store.commit");
        stage_->commit();
        stage_.reset();
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store participant is not prepared", "procgen.artifact-store.rollback");
        stage_->rollback();
        stage_.reset();
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store participant is not committed",
                                      "procgen.artifact-store.compensate");
        return participantFailure(eve::DiagnosticCode::Unsupported,
                                  "artifact store publication has no inverse compensation",
                                  "procgen.artifact-store.compensate");
    }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack };

    ArtifactStore*                                      store_ = nullptr;
    GeneratedArtifact                                   artifact_;
    std::unique_ptr<eve::artifact::PreparedPublication> stage_;
    Phase                                               phase_ = Phase::Idle;
};

/** @brief Restores one provider only during the coordinator commit phase. */
class ProviderRestoreParticipant final : public eve::transaction::ITransactionParticipant {
public:
    ProviderRestoreParticipant(std::string name, eve::artifact::ProviderContract& provider, const eve::Value& state)
        : name_(std::move(name)), provider_(&provider), state_(&state) {}

    std::string_view name() const noexcept override { return name_; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact restore participant is not idle", name_ + ".prepare");
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact restore participant is not prepared", name_ + ".commit");
        auto restored = provider_->restoreState(*state_);
        if (!restored.ok()) return eve::Result<void>::failure(restored.status());
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact restore participant is not prepared", name_ + ".rollback");
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact restore participant is not committed", name_ + ".compensate");
        provider_->clearState();
        phase_ = Phase::Compensated;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack, Compensated };

    std::string                      name_;
    eve::artifact::ProviderContract* provider_ = nullptr;
    const eve::Value*                state_    = nullptr;
    Phase                            phase_    = Phase::Idle;
};

/** @brief Restores the store through the same common coordinator protocol. */
class StoreRestoreParticipant final : public eve::transaction::ITransactionParticipant {
public:
    StoreRestoreParticipant(ArtifactStore& store, const eve::Value& state) : store_(&store), state_(&state) {}

    std::string_view name() const noexcept override { return "procgen.artifact-store.restore"; }

    [[nodiscard]] eve::Result<void> prepare(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Idle)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store restore participant is not idle",
                                      "procgen.artifact-store.restore.prepare");
        phase_ = Phase::Prepared;
        return eve::Result<void>::success();
    }

    [[nodiscard]] eve::Result<void> commit(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store restore participant is not prepared",
                                      "procgen.artifact-store.restore.commit");
        auto restored = store_->restoreState(*state_);
        if (!restored.ok()) return eve::Result<void>::failure(restored.status());
        phase_ = Phase::Committed;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> rollback(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Prepared)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store restore participant is not prepared",
                                      "procgen.artifact-store.restore.rollback");
        phase_ = Phase::RolledBack;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    [[nodiscard]] eve::Result<void> compensate(const eve::transaction::TransactionContext&) override {
        if (phase_ != Phase::Committed)
            return participantFailure(eve::DiagnosticCode::PreconditionViolation,
                                      "artifact store restore participant is not committed",
                                      "procgen.artifact-store.restore.compensate");
        store_->clear();
        phase_ = Phase::Compensated;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

private:
    enum class Phase { Idle, Prepared, Committed, RolledBack, Compensated };

    ArtifactStore*    store_ = nullptr;
    const eve::Value* state_ = nullptr;
    Phase             phase_ = Phase::Idle;
};

template <class Adapter>
eve::Result<void> addProviderParticipant(
    bool required, const char* name, const eve::artifact::PublicationView& publication,
    std::vector<std::unique_ptr<eve::transaction::ITransactionParticipant>>& owners,
    std::vector<eve::transaction::ITransactionParticipant*>&                 participants) {
    if (!required) return eve::Result<void>::success(eve::Status::success(eve::StatusCode::NoOp));
    Adapter* adapter = eve::cap::query<Adapter>();
    if (!adapter)
        return participantFailure(eve::DiagnosticCode::Unsupported,
                                  std::string("required artifact adapter is unavailable: ") + name,
                                  std::string("artifact.adapters.") + name);
    try {
        owners.emplace_back(std::make_unique<ProviderParticipant>(name, *adapter, publication));
        participants.push_back(owners.back().get());
    } catch (...) {
        return participantFailure(eve::DiagnosticCode::Failed,
                                  std::string("failed to allocate artifact participant: ") + name,
                                  std::string("artifact.adapters.") + name);
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

const eve::Value* objectMember(const eve::Value::Object& object, const char* name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

eve::transaction::TransactionContext publicationContext(const eve::PersistentId& id, std::string_view operation) {
    if (!id.isNil()) return eve::transaction::TransactionContext(eve::TransactionId::fromUuid(id));
    return eve::transaction::TransactionContext(std::string(operation) + ".invalid-identity");
}

}  // namespace

eve::Result<ArtifactPublishReceipt> ArtifactPublisher::publish(GeneratedArtifact      artifact,
                                                               ArtifactPublishOptions options) {
    PublicationBuffer publication;
    try {
        publication = makePublicationBuffer(artifact);
    } catch (const std::exception& error) {
        return eve::Result<ArtifactPublishReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                   std::string("failed to materialize artifact publication view: ") + error.what()));
    } catch (...) {
        return eve::Result<ArtifactPublishReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "failed to materialize artifact publication view"));
    }

    std::vector<std::unique_ptr<eve::transaction::ITransactionParticipant>> owners;
    std::vector<eve::transaction::ITransactionParticipant*>                 participants;
    owners.reserve(5);
    participants.reserve(5);
    auto scene =
        addProviderParticipant<ISceneArtifactAdapter>(options.scene, "scene", publication.view, owners, participants);
    if (!scene.ok()) return eve::Result<ArtifactPublishReceipt>::failure(scene.status());
    auto map = addProviderParticipant<IMapArtifactAdapter>(options.map, "map", publication.view, owners, participants);
    if (!map.ok()) return eve::Result<ArtifactPublishReceipt>::failure(map.status());
    auto graphics = addProviderParticipant<IGraphicsArtifactAdapter>(options.graphics, "graphics", publication.view,
                                                                     owners, participants);
    if (!graphics.ok()) return eve::Result<ArtifactPublishReceipt>::failure(graphics.status());
    auto physics = addProviderParticipant<IPhysicsArtifactAdapter>(options.physics, "physics", publication.view, owners,
                                                                   participants);
    if (!physics.ok()) return eve::Result<ArtifactPublishReceipt>::failure(physics.status());

    try {
        owners.emplace_back(std::make_unique<StoreParticipant>(store_, std::move(artifact)));
        participants.push_back(owners.back().get());
    } catch (...) {
        return eve::Result<ArtifactPublishReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "failed to allocate artifact store participant"));
    }

    const auto                    context = publicationContext(publication.view.id, "artifact.publish");
    eve::transaction::Coordinator coordinator;
    auto                          coordinated = coordinator.execute(context, participants);
    if (!coordinated.ok()) return eve::Result<ArtifactPublishReceipt>::failure(coordinated.status());
    auto transactionReceipt = std::move(coordinated).takeValue();
    if (transactionReceipt.state != eve::transaction::CoordinatorState::Committed)
        return eve::Result<ArtifactPublishReceipt>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation,
                                   "artifact publication coordinator returned a non-committed receipt"));

    ArtifactPublishReceipt receipt;
    receipt.id                = ArtifactId::fromUuid(publication.view.id);
    receipt.scenePublished    = options.scene;
    receipt.graphicsPublished = options.graphics;
    receipt.physicsPublished  = options.physics;
    receipt.mapPublished      = options.map;
    return eve::Result<ArtifactPublishReceipt>::success(std::move(receipt));
}

eve::Result<eve::SnapshotEnvelope> ArtifactPublisher::snapshot(const ArtifactSnapshotContext& context) const {
    if (!context.hashProvider)
        return eve::Result<eve::SnapshotEnvelope>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "artifact snapshot requires an injected hash provider"));
    const auto schema = eve::LogicalId::fromParts("eve", "artifact-publication");
    if (!schema)
        return eve::Result<eve::SnapshotEnvelope>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvariantViolation, "artifact snapshot schema id is invalid"));

    auto store = store_.snapshotState();
    if (!store.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(store.status());
    eve::Value::Object providers;
    const auto         appendProvider = [&providers](const char*                      name,
                                             eve::artifact::ProviderContract* provider) -> eve::Result<void> {
        if (!provider) return eve::Result<void>::success();
        auto state = provider->snapshotState();
        if (!state.ok()) return eve::Result<void>::failure(state.status());
        providers.emplace(name, std::move(state).takeValue());
        return eve::Result<void>::success();
    };
    auto scene = appendProvider("scene", eve::cap::query<eve::artifact::ISceneArtifactAdapter>());
    if (!scene.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(scene.status());
    auto map = appendProvider("map", eve::cap::query<eve::artifact::IMapArtifactAdapter>());
    if (!map.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(map.status());
    auto graphics = appendProvider("graphics", eve::cap::query<eve::artifact::IGraphicsArtifactAdapter>());
    if (!graphics.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(graphics.status());
    auto physics = appendProvider("physics", eve::cap::query<eve::artifact::IPhysicsArtifactAdapter>());
    if (!physics.ok()) return eve::Result<eve::SnapshotEnvelope>::failure(physics.status());

    eve::Value::Object payload;
    payload.emplace("store", std::move(store).takeValue());
    payload.emplace("providers", eve::Value(std::move(providers)));
    return eve::makeSnapshotEnvelope("eve.artifact.publication", *schema, eve::SchemaVersion(1), context.instanceId,
                                     context.revision, context.tick, eve::Value(std::move(payload)),
                                     context.hashProvider);
}

eve::Result<void> ArtifactPublisher::restore(const eve::SnapshotEnvelope&     snapshot,
                                             const eve::SnapshotHashProvider& hashProvider) {
    auto verified = eve::verifySnapshotEnvelope(snapshot, hashProvider);
    if (!verified.ok()) return verified;
    const auto schema = eve::LogicalId::fromParts("eve", "artifact-publication");
    if (!schema || snapshot.type != "eve.artifact.publication" || snapshot.schema != *schema ||
        snapshot.schemaVersion.value() != 1)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::UnknownVersion, "unsupported artifact publication snapshot"));
    if (store_.size() != 0)
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "artifact snapshot restore requires an empty store"));

    const auto* payload            = snapshot.payload.getIf<eve::Value::Object>();
    const auto* storeState         = payload ? objectMember(*payload, "store") : nullptr;
    const auto* providerStateValue = payload ? objectMember(*payload, "providers") : nullptr;
    const auto* providerStates     = providerStateValue ? providerStateValue->getIf<eve::Value::Object>() : nullptr;
    if (!payload || !storeState || !providerStates)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::ParseError, "artifact publication snapshot payload is incomplete"));

    eve::artifact::ISceneArtifactAdapter*    scene    = eve::cap::query<eve::artifact::ISceneArtifactAdapter>();
    eve::artifact::IMapArtifactAdapter*      map      = eve::cap::query<eve::artifact::IMapArtifactAdapter>();
    eve::artifact::IGraphicsArtifactAdapter* graphics = eve::cap::query<eve::artifact::IGraphicsArtifactAdapter>();
    eve::artifact::IPhysicsArtifactAdapter*  physics  = eve::cap::query<eve::artifact::IPhysicsArtifactAdapter>();
    if ((scene && !scene->emptyState()) || (map && !map->emptyState()) || (graphics && !graphics->emptyState()) ||
        (physics && !physics->emptyState()))
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict,
                                                                 "artifact snapshot restore requires empty providers"));

    const auto lookupProvider = [providerStates](const char* name) -> const eve::Value* {
        const auto found = providerStates->find(name);
        return found == providerStates->end() ? nullptr : &found->second;
    };
    for (const auto& entry : *providerStates) {
        if (entry.first != "scene" && entry.first != "map" && entry.first != "graphics" && entry.first != "physics")
            return eve::Result<void>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::ParseError, "unknown artifact publication provider state"));
    }
    if ((lookupProvider("scene") && !scene) || (lookupProvider("map") && !map) ||
        (lookupProvider("graphics") && !graphics) || (lookupProvider("physics") && !physics))
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported, "snapshot contains state for an unavailable provider"));

    std::vector<std::unique_ptr<eve::transaction::ITransactionParticipant>> owners;
    std::vector<eve::transaction::ITransactionParticipant*>                 participants;
    owners.reserve(5);
    participants.reserve(5);
    try {
        owners.emplace_back(std::make_unique<StoreRestoreParticipant>(store_, *storeState));
        participants.push_back(owners.back().get());
        const auto addRestore = [&owners, &participants](const char* name, eve::artifact::ProviderContract* provider,
                                                         const eve::Value* state) {
            if (!provider || !state) return;
            owners.emplace_back(std::make_unique<ProviderRestoreParticipant>(name, *provider, *state));
            participants.push_back(owners.back().get());
        };
        addRestore("scene", scene, lookupProvider("scene"));
        addRestore("map", map, lookupProvider("map"));
        addRestore("graphics", graphics, lookupProvider("graphics"));
        addRestore("physics", physics, lookupProvider("physics"));
    } catch (...) {
        return eve::Result<void>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "failed to allocate artifact restore participants"));
    }

    const auto                    context = publicationContext(snapshot.instanceId, "artifact.restore");
    eve::transaction::Coordinator coordinator;
    auto                          coordinated = coordinator.execute(context, participants);
    if (!coordinated.ok()) return eve::Result<void>::failure(coordinated.status());
    auto transactionReceipt = std::move(coordinated).takeValue();
    if (transactionReceipt.state != eve::transaction::CoordinatorState::Committed)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "artifact restore coordinator returned a non-committed receipt"));
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

}  // namespace eve::procgen
