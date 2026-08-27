#pragma once

/**
 * @file GeneratedArtifact.h
 * @brief Owning, backend-neutral products published by procedural generation.
 *
 * Generated artifacts are the boundary between deterministic CPU generation
 * and optional map, scene, graphics or physics adapters.  The payload keeps
 * dense data in typed values; metadata is deliberately small and dynamic.
 */

#include "common/Identity.h"
#include "common/ArtifactPublication.h"
#include "common/Result.h"
#include "common/Snapshot.h"
#include "common/Value.h"
#include "common/Generation.h"
#include "common/SchemaVersion.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/PointSet.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <utility>
#include <vector>

namespace eve::procgen {

/**
 * @brief Common strong UUID identity for one published procedural artifact.
 * @remarks The procgen namespace is a source-compatible alias only. UUID
 *          parsing, formatting, hashing, ordering and child derivation are
 *          owned by `eve::StrongUuid` in the common layer.
 */
using ArtifactId = eve::ArtifactId;

/** @brief Stable key for deterministic-equivalent generation inputs. */
class BuildKey {
public:
    /** @brief Construct an empty invalid build key. */
    BuildKey() = default;

    /**
     * @brief Construct a build key from canonical text.
     * @param canonical Non-empty deterministic text; control characters are rejected.
     * @return The key, or empty for invalid input.
     */
    [[nodiscard]] static std::optional<BuildKey> fromCanonical(std::string_view canonical);

    /**
     * @brief Derive a compact deterministic key for a recipe and parameters.
     * @param recipeId Stable recipe identifier.
     * @param params Generation parameters.
     * @return A key containing a versioned hash of canonical recipe inputs.
     */
    [[nodiscard]] static std::optional<BuildKey> forRecipe(std::string_view recipeId,
                                                             const Params& params);

    /** @brief Return whether this key is non-empty. */
    [[nodiscard]] bool empty() const noexcept { return value_.empty(); }
    /** @brief Return canonical key text. */
    [[nodiscard]] const std::string& format() const noexcept { return value_; }
    friend bool operator==(const BuildKey&, const BuildKey&) noexcept = default;

private:
    explicit BuildKey(std::string value) : value_(std::move(value)) {}
    std::string value_;
};

}  // namespace eve::procgen

namespace std {
template <> struct hash<eve::procgen::BuildKey> {
    size_t operator()(const eve::procgen::BuildKey& key) const noexcept {
        return std::hash<std::string>{}(key.format());
    }
};
}  // namespace std

namespace eve::procgen {

/** @brief Axis-aligned bounds in the artifact's declared world coordinate space. */
struct Bounds {
    float minX = 0.f;
    float minY = 0.f;
    float minZ = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
    float maxZ = 0.f;
    bool  valid = false;

    /** @brief Expand this box to include one point. */
    void include(float x, float y, float z) noexcept;
    /** @brief Return whether the box encloses at least one point. */
    [[nodiscard]] bool isValid() const noexcept { return valid; }
};

/** @brief Runtime payload kind; it is checked against the variant on publish. */
enum class ArtifactType : std::uint8_t {
    Grid,
    PointSet,
    MeshData,
    ImageData,
    Collider,
    Composite,
};

/** @brief Small owning image product; raw pixels stay out of JSON metadata. */
struct ImageData {
    int                 width = 0;
    int                 height = 0;
    int                 channels = 0;
    std::string         format;
    std::vector<std::uint8_t> pixels;

    /** @brief Return whether dimensions and pixel storage describe an image. */
    [[nodiscard]] bool isValid() const noexcept;
};

/** @brief Backend-neutral collider product, normally a triangle mesh or hull. */
struct Collider {
    std::vector<float>    vertices;
    std::vector<std::uint32_t> indices;
    Bounds                bounds;
    std::string           shape = "triangle_mesh";

    /** @brief Return whether the collider has complete triangle data. */
    [[nodiscard]] bool isValid() const noexcept;
};

/** @brief Existing dense CPU mesh type under the artifact vocabulary. */
using MeshData = MeshBuild;

/** @brief Leaf payloads used by a CompositeArtifact. */
using ArtifactLeafPayload = std::variant<Grid2D, PointSet, MeshData, ImageData, Collider>;

/** @brief One typed, role-named child product inside a composite artifact. */
struct ArtifactPart {
    ArtifactId          id;
    ArtifactType        type = ArtifactType::Grid;
    eve::SchemaVersion  schemaVersion{1};
    BuildKey            buildKey;
    Bounds              bounds;
    std::vector<ArtifactId> dependencies;
    eve::Value::Object  metadata;
    ArtifactLeafPayload payload;
    std::string         role;
};

/** @brief A coherent set of products published from one deterministic build. */
struct CompositeArtifact {
    std::vector<ArtifactPart> children;

    /**
     * @brief Find a child by its stable role.
     * @return A nullable borrowed pointer into this composite, or `nullptr` if
     *         no child has that role.
     * @ownership Borrowed; `CompositeArtifact::children` owns the child and the
     *            caller must not delete or retain the pointer after mutation.
     * @nullable Yes, when the role is absent.
     * @lifetime Valid until this composite is mutated, moved or destroyed.
     * @thread Access is confined to the owner of this immutable published value.
     * @reentrancy The function is side-effect free and invokes no callbacks.
     */
    [[nodiscard]] const ArtifactPart* find(std::string_view role) const noexcept;
    /** @brief Return whether a role exists. */
    [[nodiscard]] bool has(std::string_view role) const noexcept { return find(role) != nullptr; }
};

/** @brief Complete immutable-once-published artifact record. */
struct GeneratedArtifact {
    ArtifactId             id;
    ArtifactType           type = ArtifactType::Grid;
    eve::SchemaVersion     schemaVersion{1};
    BuildKey               buildKey;
    Bounds                 bounds;
    std::vector<ArtifactId> dependencies;
    eve::Value::Object     metadata;
    using Payload = std::variant<Grid2D, PointSet, MeshData, ImageData, Collider,
                                 CompositeArtifact>;
    Payload payload;
};

/** @brief Return the variant kind for a payload. */
[[nodiscard]] ArtifactType artifactType(const GeneratedArtifact::Payload& payload) noexcept;
/**
 * @brief Return the stable textual spelling of an artifact kind.
 * @return A non-null borrowed pointer to immutable static text.
 * @ownership Borrowed from program-static storage; the caller must not free it.
 * @nullable No.
 * @lifetime Static for the process lifetime.
 * @thread Thread-safe and side-effect free.
 * @reentrancy Does not access artifact state or invoke callbacks.
 */
[[nodiscard]] const char* artifactTypeName(ArtifactType type) noexcept;

/**
 * @brief Validate and construct one artifact record.
 * @return A complete owning artifact or a structured rejection diagnostic.
 */
[[nodiscard]] eve::Result<GeneratedArtifact> makeArtifact(
    ArtifactId id, ArtifactType type, eve::SchemaVersion schemaVersion, BuildKey buildKey,
    Bounds bounds, std::vector<ArtifactId> dependencies, eve::Value::Object metadata,
    GeneratedArtifact::Payload payload);

/** @brief Validate and construct a leaf part for a composite artifact. */
[[nodiscard]] eve::Result<ArtifactPart> makeArtifactPart(
    std::string role, ArtifactId id, ArtifactType type, eve::SchemaVersion schemaVersion,
    BuildKey buildKey, Bounds bounds, std::vector<ArtifactId> dependencies,
    eve::Value::Object metadata, ArtifactLeafPayload payload);

/**
 * @brief In-memory owner and publisher for generated artifacts.
 *
 * The store owns records after publish. Pointers returned by find are borrowed
 * and are invalidated by publish, remove or clear. Publishing never replaces
 * an existing identity; build keys are lookup metadata and do not become
 * identities.
 */
class ArtifactStore {
public:
    /**
     * @brief Validate and atomically publish one artifact and all composite parts.
     * @param artifact Owning artifact to publish.
     * @return Its instance identity, or a structured validation/dependency error.
     * @remarks A failed Result leaves both top-level and part registries unchanged.
     */
    [[nodiscard]] eve::Result<ArtifactId> publish(GeneratedArtifact artifact);

    /**
     * @brief Validate and atomically publish a dependency-connected batch.
     * @param artifacts Owning top-level artifacts; dependencies may reference
     *                  existing records or any top-level/part record in this batch.
     * @return Published top-level identities in input order.
     * @remarks A failed Result leaves the store unchanged.
     */
    [[nodiscard]] eve::Result<std::vector<ArtifactId>> publishBatch(
        std::vector<GeneratedArtifact> artifacts);

    /**
     * @brief Prepare one store mutation for a larger provider transaction.
     * @param artifact Owning CPU artifact to stage.
     * @return A hidden store stage; it becomes visible only on commit.
     * @remarks The returned stage shares the store's main-thread affinity.
     */
    [[nodiscard]] eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> stagePublish(
        GeneratedArtifact artifact);

    /**
     * @brief Export identity, build-key and complete typed dense payload state.
     * @return An owning value suitable for a SnapshotEnvelope.
     */
    [[nodiscard]] eve::Result<eve::Value> snapshotState() const;

    /**
     * @brief Restore complete identity/build-key and typed payload state transactionally.
     * @param state State previously emitted by snapshotState().
     * @return Applied or a structured parse/conflict error.
     * @remarks The candidate graph is parsed and validated before either store
     *          index is changed. After success, find() and findPart() return
     *          the restored dense records immediately.
     */
    [[nodiscard]] eve::Result<void> restoreState(const eve::Value& state);

    /**
     * @brief Look up a published artifact.
     * @return A nullable borrowed pointer, or `nullptr` when the identity is absent.
     * @ownership Borrowed from this store; `ArtifactStore` owns the artifact and
     *            the caller must not delete or transfer the pointer.
     * @nullable Yes, for an unknown or removed identity.
     * @lifetime Valid until the next publish, remove, clear, restore or store
     *           destruction; copy the artifact or use its identity for later lookup.
     * @thread Calls and returned observations share the store's owner-thread affinity.
     * @reentrancy Side-effect free; do not mutate the store while using the pointer.
     */
    [[nodiscard]] const GeneratedArtifact* find(ArtifactId id) const noexcept;
    /**
     * @brief Look up a composite child part.
     * @return A nullable borrowed pointer, or `nullptr` when the part is absent.
     * @ownership Borrowed from this store; its owning top-level artifact remains
     *            the sole owner and the caller must not delete the pointer.
     * @nullable Yes, for an unknown, removed or non-composite identity.
     * @lifetime Valid until publish, remove, clear, restore or store destruction;
     *           use the part identity to resolve it again after a mutation.
     * @thread Calls and returned observations share the store's owner-thread affinity.
     * @reentrancy Side-effect free and does not invoke provider callbacks.
     */
    [[nodiscard]] const ArtifactPart* findPart(ArtifactId id) const noexcept;
    /** @brief Return the kind of a live identity or composite part. */
    [[nodiscard]] std::optional<ArtifactType> typeOf(ArtifactId id) const noexcept;
    /** @brief Find all identities with a matching deterministic build key. */
    [[nodiscard]] std::vector<ArtifactId> findByBuildKey(const BuildKey& key) const;
    /** @brief Remove one artifact; NotFound is returned when absent. */
    [[nodiscard]] eve::Result<void> remove(ArtifactId id);
    /** @brief Remove all published artifacts. */
    void clear() noexcept;
    /** @brief Return the number of published top-level artifacts. */
    [[nodiscard]] std::size_t size() const noexcept { return artifacts_.size(); }
    /** @brief Return the number of indexed composite child products. */
    [[nodiscard]] std::size_t partCount() const noexcept;

private:
    friend class ArtifactStoreStage;
    void commitStaged(std::vector<GeneratedArtifact> artifacts) noexcept;
    void rollbackStaged() noexcept;

    std::unordered_map<ArtifactId, GeneratedArtifact> artifacts_;
    std::unordered_map<ArtifactId, ArtifactId>        partOwners_;
    bool stageActive_ = false;
};

/**
 * @brief Generate a hex-terrain composite containing mesh, collider,
 *        topology and world anchors.
 * @param params Deterministic hex-terrain parameters.
 * @param id Non-nil identity for the top-level artifact instance.
 * @return An owning composite artifact, or a structured generation failure.
 */
[[nodiscard]] eve::Result<GeneratedArtifact> generateHexTerrainArtifact(
    const Params& params, ArtifactId id);

/**
 * @brief Generate a castle composite containing mesh, collider, ring topology
 *        and gate/keep anchors.
 * @param params Deterministic castle parameters.
 * @param id Non-nil identity for the top-level artifact instance.
 * @return An owning composite artifact, or a structured generation failure.
 */
[[nodiscard]] eve::Result<GeneratedArtifact> generateCastleArtifact(
    const Params& params, ArtifactId id);

/**
 * @brief Generate any registered mesh recipe as a CPU artifact.
 * @param recipeId Registered mesh recipe id.
 * @param params Deterministic recipe parameters.
 * @param id Non-nil artifact instance identity.
 * @return Hex terrain and castle become composites; other recipes become MeshData artifacts.
 */
[[nodiscard]] eve::Result<GeneratedArtifact> generateMeshArtifact(
    std::string_view recipeId, const Params& params, ArtifactId id);

}  // namespace eve::procgen
