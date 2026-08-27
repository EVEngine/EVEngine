#pragma once

/**
 * @file ArtifactPublication.h
 * @brief Low-level capability contracts for publishing backend-neutral artifacts.
 *
 * This header intentionally contains no procgen, scene, graphics, map or
 * physics include.  Procgen owns the producer-side conversion from its typed
 * GeneratedArtifact into this short-lived view; consumer modules own the
 * providers and their runtime registries.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "common/Value.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace eve::artifact {

/** @brief The leaf kinds understood by consumer-owned artifact providers. */
enum class PartKind : std::uint8_t {
    Grid,
    PointSet,
    MeshData,
    ImageData,
    Collider,
};

/** @brief Backend-neutral axis-aligned bounds copied from a generated part. */
struct Bounds {
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;
    bool valid = false;
};

/** @brief Minimal point representation used at the publication boundary. */
struct Point {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/**
 * @brief Borrowed immutable view of one generated leaf.
 *
 * The view is valid only during the provider's `prepare` call. Providers must
 * copy every field needed after that call and must not retain these spans or
 * string views.
 */
struct PartView {
    PersistentId         id;
    std::string_view     role;
    PartKind             kind = PartKind::Grid;
    std::uint64_t        schemaVersion = 1;
    std::string_view     buildKey;
    Bounds               bounds;
    std::span<const float> positions;
    std::span<const float> normals;
    std::span<const float> uvs;
    std::span<const std::uint32_t> indices;
    std::int32_t         width = 0;
    std::int32_t         height = 0;
    std::span<const std::uint32_t> cells;
    std::span<const Point> points;
    std::string_view     metadataJson;
};

/**
 * @brief Borrowed immutable view of one complete generated publication.
 *
 * This is the only data crossing from procgen into lower-level consumers.
 * Dense arrays stay typed and are never routed through JSON.
 */
struct PublicationView {
    PersistentId          id;
    std::uint64_t         schemaVersion = 1;
    std::string_view      buildKey;
    Bounds                bounds;
    std::span<const PartView> parts;
    std::string_view      metadataJson;
};

/**
 * @brief A prepared consumer mutation with a non-throwing visibility boundary.
 *
 * `commit` and `rollback` are called on the preparing thread. Implementations
 * preallocate/copy everything in prepare; an unexpected allocation or broken
 * invariant during commit is a process-fatal contract violation and must not
 * be reported as a recoverable partial success.
 */
class PreparedPublication {
public:
    virtual ~PreparedPublication() = default;
    /** @brief Make the prepared mutation observable; the operation cannot fail. */
    virtual void commit() noexcept = 0;
    /** @brief Discard the prepared mutation; idempotent and non-throwing. */
    virtual void rollback() noexcept = 0;
};

/** @brief Base contract for a consumer-owned artifact provider. */
class ProviderContract {
public:
    virtual ~ProviderContract() = default;

    /**
     * @brief Prepare a provider-local mutation from a borrowed publication view.
     * @param publication Immutable input valid for this call only.
     * @return A prepared stage or a structured validation/provider error.
     */
    [[nodiscard]] virtual eve::Result<std::unique_ptr<PreparedPublication>> prepare(
        const PublicationView& publication) = 0;

    /**
     * @brief Export backend-neutral observable provider state.
     * @return Owning state value suitable for a SnapshotEnvelope payload.
     */
    [[nodiscard]] virtual eve::Result<eve::Value> snapshotState() const;

    /**
     * @brief Transactionally restore backend-neutral observable provider state.
     * @param state State previously produced by snapshotState().
     * @return Applied or a structured parse/conflict/provider error.
     */
    [[nodiscard]] virtual eve::Result<void> restoreState(const eve::Value& state);

    /** @brief Return whether this provider has no committed observable state. */
    [[nodiscard]] virtual bool emptyState() const noexcept { return true; }

    /** @brief Clear provider state during aggregate-restore failure cleanup. */
    virtual void clearState() noexcept {}
};

/** @brief Scene graph publication capability, implemented by the scene module. */
class ISceneArtifactAdapter : public ProviderContract {
public:
    static constexpr const char* capabilityName = "eve.procgen.ISceneArtifactAdapter";
    ~ISceneArtifactAdapter() override = default;
};

/** @brief Tile/map publication capability, implemented by the map module. */
class IMapArtifactAdapter : public ProviderContract {
public:
    static constexpr const char* capabilityName = "eve.procgen.IMapArtifactAdapter";
    ~IMapArtifactAdapter() override = default;
};

/** @brief Graphics resource publication capability, implemented by graphics. */
class IGraphicsArtifactAdapter : public ProviderContract {
public:
    static constexpr const char* capabilityName = "eve.procgen.IGraphicsArtifactAdapter";
    ~IGraphicsArtifactAdapter() override = default;
};

/** @brief Collider publication capability, implemented by physics. */
class IPhysicsArtifactAdapter : public ProviderContract {
public:
    static constexpr const char* capabilityName = "eve.procgen.IPhysicsArtifactAdapter";
    ~IPhysicsArtifactAdapter() override = default;
};

/** @brief Required provider set for one artifact publication transaction. */
struct PublishOptions {
    bool scene = false;
    bool graphics = false;
    bool physics = false;
    /** @brief Map is kept last for aggregate-initializer compatibility. */
    bool map = false;
};

/** @brief Observable receipt from a successful provider transaction. */
struct PublishReceipt {
    PersistentId id;
    bool scenePublished = false;
    bool graphicsPublished = false;
    bool physicsPublished = false;
    bool mapPublished = false;
};

}  // namespace eve::artifact
