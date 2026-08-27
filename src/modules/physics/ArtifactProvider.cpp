#include "physics/ArtifactProvider.h"

#include "common/Capability.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace eve::physics {

/**
 * @brief Owning runtime state for one published collider.
 *
 * Declaration order is intentional: destruction runs Shape3D, Body3D, then
 * World3D. The wrappers are owned here rather than by the public descriptor;
 * the descriptor remains a backend-neutral borrowed query description.
 */
struct PhysicsArtifactProvider::State {
    struct RuntimeCollider {
        PhysicsArtifactCollider  descriptor;
        std::unique_ptr<World3D> world;
        std::unique_ptr<Body3D>  body;
        std::unique_ptr<Shape3D> shape;
    };

    std::vector<RuntimeCollider> records;
    std::size_t                  pendingStages = 0;
};

namespace {

const eve::Value* member(const eve::Value::Object& object, const char* name) noexcept {
    const auto found = object.find(name);
    return found == object.end() ? nullptr : &found->second;
}

bool readString(const eve::Value::Object& object, const char* name, std::string& output) {
    const auto* value = member(object, name);
    const auto* text  = value ? value->getIf<std::string>() : nullptr;
    if (!text || text->empty()) return false;
    output = *text;
    return true;
}

bool readU64(const eve::Value::Object& object, const char* name, std::uint64_t& output) {
    std::string text;
    if (!readString(object, name, text)) return false;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), output);
    return error == std::errc{} && end == text.data() + text.size();
}

bool readFloat(const eve::Value::Object& object, const char* name, float& output) {
    const auto* value = member(object, name);
    if (!value) return false;
    if (const auto* number = value->getIf<double>()) {
        if (!std::isfinite(*number)) return false;
        output = static_cast<float>(*number);
        return std::isfinite(output);
    }
    if (const auto* integer = value->getIf<std::int64_t>()) {
        output = static_cast<float>(*integer);
        return std::isfinite(output);
    }
    return false;
}

bool hasSupportedVersion(const eve::Value::Object& object) noexcept {
    const auto* encoded = member(object, "version");
    const auto* version = encoded ? encoded->getIf<std::int64_t>() : nullptr;
    return version && *version == 1;
}

bool validBounds(const eve::artifact::Bounds& bounds) noexcept {
    return bounds.valid && std::isfinite(bounds.minX) && std::isfinite(bounds.minY) && std::isfinite(bounds.minZ) &&
           std::isfinite(bounds.maxX) && std::isfinite(bounds.maxY) && std::isfinite(bounds.maxZ) &&
           bounds.minX <= bounds.maxX && bounds.minY <= bounds.maxY && bounds.minZ <= bounds.maxZ;
}

bool readBounds(const eve::Value::Object& object, eve::artifact::Bounds& output) {
    const auto* encoded = member(object, "bounds");
    const auto* bounds  = encoded ? encoded->getIf<eve::Value::Object>() : nullptr;
    if (!bounds || !readFloat(*bounds, "minX", output.minX) || !readFloat(*bounds, "minY", output.minY) ||
        !readFloat(*bounds, "minZ", output.minZ) || !readFloat(*bounds, "maxX", output.maxX) ||
        !readFloat(*bounds, "maxY", output.maxY) || !readFloat(*bounds, "maxZ", output.maxZ))
        return false;
    output.valid = true;
    return validBounds(output);
}

template <class T>
eve::Value arrayValue(const std::vector<T>& values) {
    eve::Value::Array encoded;
    encoded.reserve(values.size());
    for (const auto value : values) {
        if constexpr (std::is_same_v<T, float>)
            encoded.emplace_back(value);
        else
            encoded.emplace_back(std::int64_t(value));
    }
    return eve::Value(std::move(encoded));
}

template <class T>
bool readArray(const eve::Value* encoded, std::vector<T>& output) {
    const auto* array = encoded ? encoded->getIf<eve::Value::Array>() : nullptr;
    if (!array) return false;
    output.clear();
    output.reserve(array->size());
    for (const eve::Value& value : *array) {
        if constexpr (std::is_same_v<T, float>) {
            if (const auto* number = value.getIf<double>()) {
                if (!std::isfinite(*number)) return false;
                const float converted = static_cast<float>(*number);
                if (!std::isfinite(converted)) return false;
                output.push_back(converted);
            } else if (const auto* integer = value.getIf<std::int64_t>()) {
                const float converted = static_cast<float>(*integer);
                if (!std::isfinite(converted)) return false;
                output.push_back(converted);
            } else {
                return false;
            }
        } else {
            const auto* integer = value.getIf<std::int64_t>();
            if (!integer || *integer < 0 || static_cast<std::uint64_t>(*integer) > std::numeric_limits<T>::max())
                return false;
            output.push_back(static_cast<T>(*integer));
        }
    }
    return true;
}

eve::Value boundsValue(const eve::artifact::Bounds& bounds) {
    eve::Value::Object value;
    value.emplace("minX", eve::Value(bounds.minX));
    value.emplace("minY", eve::Value(bounds.minY));
    value.emplace("minZ", eve::Value(bounds.minZ));
    value.emplace("maxX", eve::Value(bounds.maxX));
    value.emplace("maxY", eve::Value(bounds.maxY));
    value.emplace("maxZ", eve::Value(bounds.maxZ));
    return eve::Value(std::move(value));
}

bool validGeometry(const std::vector<float>& vertices, const std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || vertices.size() % 3u != 0u || indices.empty() || indices.size() % 3u != 0u ||
        vertices.size() / 3u > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()))
        return false;
    for (const float value : vertices)
        if (!std::isfinite(value)) return false;
    for (const auto index : indices)
        if (index >= vertices.size() / 3u ||
            index > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()))
            return false;
    return true;
}

std::vector<std::int32_t> signedIndices(const std::vector<std::uint32_t>& indices) {
    std::vector<std::int32_t> result;
    result.reserve(indices.size());
    for (const auto index : indices) result.push_back(static_cast<std::int32_t>(index));
    return result;
}

eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> prepareFailure(eve::DiagnosticCode code,
                                                                                std::string         message) {
    return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::failure(
        eve::Diagnostic::error(code, std::move(message)));
}

eve::Result<void> restoreFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<void>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

/** @brief Staged, owning Box3D construction for one provider mutation. */
class PhysicsArtifactStage final : public eve::artifact::PreparedPublication {
public:
    PhysicsArtifactStage(PhysicsArtifactProvider& owner, PhysicsArtifactCollider collider,
                         std::unique_ptr<World3D> world, std::unique_ptr<Body3D> body, std::unique_ptr<Shape3D> shape)
        : owner_(&owner),
          collider_(std::move(collider)),
          world_(std::move(world)),
          body_(std::move(body)),
          shape_(std::move(shape)) {}

    ~PhysicsArtifactStage() override { rollback(); }

    void commit() noexcept override {
        if (!owner_) return;
        owner_->commit(std::move(collider_), std::move(world_), std::move(body_), std::move(shape_));
        owner_ = nullptr;
    }

    void rollback() noexcept override {
        if (!owner_) return;
        owner_->release(collider_, world_, body_, shape_);
        owner_    = nullptr;
        collider_ = {};
    }

private:
    PhysicsArtifactProvider* owner_ = nullptr;
    PhysicsArtifactCollider  collider_;
    std::unique_ptr<World3D> world_;
    std::unique_ptr<Body3D>  body_;
    std::unique_ptr<Shape3D> shape_;
};

PhysicsArtifactProvider::PhysicsArtifactProvider() : state_(std::make_unique<State>()) {}

PhysicsArtifactProvider::~PhysicsArtifactProvider() { clear(); }

eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>> PhysicsArtifactProvider::prepare(
    const eve::artifact::PublicationView& publication) {
    if (failPrepare_)
        return prepareFailure(eve::DiagnosticCode::Failed, "physics artifact prepare failure was injected");
    if (publication.schemaVersion != 1 || publication.id.isNil() || publication.buildKey.empty())
        return prepareFailure(
            publication.schemaVersion != 1 ? eve::DiagnosticCode::UnknownVersion : eve::DiagnosticCode::InvalidArgument,
            "physics publication requires schema version 1, identity and build key");
    if (find(publication.id) != nullptr)
        return prepareFailure(eve::DiagnosticCode::Conflict, "physics artifact identity is already published");

    const eve::artifact::PartView* colliderPart = nullptr;
    for (const auto& part : publication.parts) {
        if (part.kind == eve::artifact::PartKind::Collider && (part.role == "collider" || colliderPart == nullptr))
            colliderPart = &part;
    }
    if (!colliderPart)
        return prepareFailure(eve::DiagnosticCode::PreconditionViolation,
                              "physics publication requires a collider part");
    if (colliderPart->schemaVersion != 1)
        return prepareFailure(eve::DiagnosticCode::UnknownVersion, "physics collider schema version is unsupported");

    PhysicsArtifactCollider collider;
    collider.id       = publication.id;
    collider.buildKey = std::string(publication.buildKey);
    collider.shape    = "triangle_mesh";
    collider.backend  = "box3d";
    collider.bounds   = colliderPart->bounds;
    try {
        collider.vertices.assign(colliderPart->positions.begin(), colliderPart->positions.end());
        collider.indices.assign(colliderPart->indices.begin(), colliderPart->indices.end());
        if (!validBounds(collider.bounds) || !validGeometry(collider.vertices, collider.indices))
            return prepareFailure(eve::DiagnosticCode::PreconditionViolation,
                                  "physics publication requires finite valid triangle geometry");
        if (nextIndex_ == PhysicsArtifactHandle::invalidIndex)
            return prepareFailure(eve::DiagnosticCode::Failed, "physics artifact handle index exhausted");

        // Allocate the artifact handle during prepare. A rolled-back stage
        // burns this slot intentionally; reusing it would make a stale handle
        // resolve to a different collider.
        collider.handle = PhysicsArtifactHandle(nextIndex_, 1u);
        ++nextIndex_;
        state_->records.reserve(state_->records.size() + state_->pendingStages + 1u);

        auto world = std::make_unique<World3D>(0.f, 0.f, 0.f, true);
        auto body  = std::unique_ptr<Body3D>(world->newBody("static", 0.f, 0.f, 0.f));
        if (!body)
            return prepareFailure(eve::DiagnosticCode::Failed, "Box3D did not create the generated collider body");
        auto mesh  = signedIndices(collider.indices);
        auto shape = std::unique_ptr<Shape3D>(body->newTriangleMeshShape(collider.vertices, mesh));
        if (!body || !shape)
            return prepareFailure(eve::DiagnosticCode::Failed, "Box3D did not create the generated collider");
        auto stage = std::make_unique<PhysicsArtifactStage>(*this, std::move(collider), std::move(world),
                                                            std::move(body), std::move(shape));
        ++state_->pendingStages;
        return eve::Result<std::unique_ptr<eve::artifact::PreparedPublication>>::success(std::move(stage));
    } catch (const std::exception& error) {
        return prepareFailure(eve::DiagnosticCode::Failed,
                              std::string("Box3D collider construction failed: ") + error.what());
    } catch (...) {
        return prepareFailure(eve::DiagnosticCode::Failed, "Box3D collider construction failed");
    }
}

void PhysicsArtifactProvider::commit(PhysicsArtifactCollider collider, std::unique_ptr<World3D> world,
                                     std::unique_ptr<Body3D> body, std::unique_ptr<Shape3D> shape) noexcept {
    State::RuntimeCollider runtime;
    if (state_->pendingStages > 0) --state_->pendingStages;
    runtime.descriptor = std::move(collider);
    runtime.world      = std::move(world);
    runtime.body       = std::move(body);
    runtime.shape      = std::move(shape);
    state_->records.push_back(std::move(runtime));
}

void PhysicsArtifactProvider::release(PhysicsArtifactCollider& collider, std::unique_ptr<World3D>& world,
                                      std::unique_ptr<Body3D>& body, std::unique_ptr<Shape3D>& shape) noexcept {
    // Explicitly tear down the backend before wrapper ownership is released.
    // This also makes rollback behavior independent of Box3D's wrapper order.
    if (state_->pendingStages > 0) --state_->pendingStages;
    if (shape) shape->destroy();
    shape.reset();
    if (body) body->destroy();
    body.reset();
    if (world) world->destroy();
    world.reset();
    collider = {};
}

const PhysicsArtifactCollider* PhysicsArtifactProvider::find(eve::PersistentId id) const noexcept {
    const auto found = std::find_if(state_->records.begin(), state_->records.end(),
                                    [id](const auto& record) { return record.descriptor.id == id; });
    return found == state_->records.end() ? nullptr : &found->descriptor;
}

const PhysicsArtifactCollider* PhysicsArtifactProvider::find(PhysicsArtifactHandle handle) const noexcept {
    if (handle.isInvalid()) return nullptr;
    const auto found = std::find_if(state_->records.begin(), state_->records.end(),
                                    [handle](const auto& record) { return record.descriptor.handle == handle; });
    return found == state_->records.end() ? nullptr : &found->descriptor;
}

bool PhysicsArtifactProvider::isHandleLive(PhysicsArtifactHandle handle) const noexcept {
    return find(handle) != nullptr;
}

PhysicsArtifactRayHit PhysicsArtifactProvider::rayCast(eve::PersistentId id, float ox, float oy, float oz, float dx,
                                                       float dy, float dz) const noexcept {
    PhysicsArtifactRayHit result;
    const auto            found = std::find_if(state_->records.begin(), state_->records.end(),
                                               [id](const auto& record) { return record.descriptor.id == id; });
    if (found == state_->records.end()) return result;
    const auto& runtime = *found;
    if (!runtime.world || !runtime.body || !runtime.shape || !runtime.world->isValid() || !runtime.body->isValid() ||
        !runtime.shape->isValid())
        return result;
    if (!std::isfinite(ox) || !std::isfinite(oy) || !std::isfinite(oz) || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(dz))
        return result;
    const float ex = ox + dx;
    const float ey = oy + dy;
    const float ez = oz + dz;
    if (!std::isfinite(ex) || !std::isfinite(ey) || !std::isfinite(ez)) return result;
    try {
        const int bodyId = runtime.world->rayCast(ox, oy, oz, ex, ey, ez);
        if (bodyId < 0 || runtime.world->getRayResultCount() <= 0) return result;
        result.hit      = true;
        result.fraction = runtime.world->getRayResultFraction(0);
        result.x        = runtime.world->getRayResultX(0);
        result.y        = runtime.world->getRayResultY(0);
        result.z        = runtime.world->getRayResultZ(0);
        result.normalX  = runtime.world->getRayResultNormalX(0);
        result.normalY  = runtime.world->getRayResultNormalY(0);
        result.normalZ  = runtime.world->getRayResultNormalZ(0);
    } catch (...) {
        // The public query is a noexcept probe. Invalid input or a backend
        // query failure is represented as a miss; the collider remains intact.
        return PhysicsArtifactRayHit{};
    }
    return result;
}

std::size_t PhysicsArtifactProvider::size() const noexcept { return state_->records.size(); }

std::string PhysicsArtifactProvider::backendName(eve::PersistentId id) const {
    const auto* collider = find(id);
    return collider ? collider->backend : std::string{};
}

bool PhysicsArtifactProvider::isBox3DBacked(eve::PersistentId id) const noexcept {
    const auto found = std::find_if(state_->records.begin(), state_->records.end(),
                                    [id](const auto& record) { return record.descriptor.id == id; });
    if (found == state_->records.end()) return false;
    const auto& runtime = *found;
    return runtime.world && runtime.body && runtime.shape && runtime.world->isValid() && runtime.body->isValid() &&
           runtime.shape->isValid() && runtime.shape->getKind() == "triangleMesh";
}

bool PhysicsArtifactProvider::emptyState() const noexcept { return state_->records.empty(); }

void PhysicsArtifactProvider::clear() noexcept {
    state_->records.clear();
    // Do not reset nextIndex_: artifact handles are process-local and must not
    // become valid again after destruction. Exhausted slots are retired.
    failPrepare_ = false;
}

eve::Result<eve::Value> PhysicsArtifactProvider::snapshotState() const {
    try {
        eve::Value::Array colliders;
        colliders.reserve(state_->records.size());
        for (const auto& runtime : state_->records) {
            const auto&        collider = runtime.descriptor;
            eve::Value::Object item;
            item.emplace("artifactId", eve::Value(collider.id.format()));
            item.emplace("buildKey", eve::Value(collider.buildKey));
            // Kept for diagnostics and compatibility. restoreState deliberately
            // regenerates this process-local handle rather than reviving it.
            item.emplace("handle", eve::Value(std::to_string(collider.handle.packed())));
            item.emplace("shape", eve::Value(collider.shape));
            item.emplace("backend", eve::Value(collider.backend));
            item.emplace("bounds", boundsValue(collider.bounds));
            item.emplace("vertices", arrayValue(collider.vertices));
            item.emplace("indices", arrayValue(collider.indices));
            colliders.emplace_back(std::move(item));
        }
        eve::Value::Object state;
        state.emplace("provider", eve::Value("physics.box3d-collider"));
        state.emplace("version", eve::Value(std::int64_t(1)));
        state.emplace("colliders", eve::Value(std::move(colliders)));
        return eve::Result<eve::Value>::success(eve::Value(std::move(state)));
    } catch (...) {
        return eve::Result<eve::Value>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "physics artifact snapshot allocation failed"));
    }
}

eve::Result<void> PhysicsArtifactProvider::restoreState(const eve::Value& state) {
    if (state_->pendingStages != 0)
        return restoreFailure(eve::DiagnosticCode::Conflict,
                              "cannot restore physics artifacts while a publication is staged");
    const auto* object           = state.getIf<eve::Value::Object>();
    const auto* provider         = object ? member(*object, "provider") : nullptr;
    const auto* providerName     = provider ? provider->getIf<std::string>() : nullptr;
    const auto* encodedColliders = object ? member(*object, "colliders") : nullptr;
    const auto* colliders        = encodedColliders ? encodedColliders->getIf<eve::Value::Array>() : nullptr;
    if (!object || !providerName || *providerName != "physics.box3d-collider" || !hasSupportedVersion(*object) ||
        !colliders)
        return restoreFailure(eve::DiagnosticCode::ParseError, "invalid physics artifact provider state");

    std::vector<State::RuntimeCollider>   candidate;
    std::unordered_set<eve::PersistentId> identities;
    std::uint32_t                         next = nextIndex_;
    try {
        candidate.reserve(colliders->size());
        for (const eve::Value& encoded : *colliders) {
            const auto*             item = encoded.getIf<eve::Value::Object>();
            std::string             idText;
            std::string             buildKey;
            std::string             shape;
            std::string             backend;
            std::uint64_t           packed = 0;
            PhysicsArtifactCollider collider;
            if (!item || !readString(*item, "artifactId", idText) || !readString(*item, "buildKey", buildKey) ||
                !readString(*item, "shape", shape) || !readString(*item, "backend", backend) ||
                !readU64(*item, "handle", packed) || !readBounds(*item, collider.bounds) ||
                !readArray(member(*item, "vertices"), collider.vertices) ||
                !readArray(member(*item, "indices"), collider.indices))
                return restoreFailure(eve::DiagnosticCode::ParseError, "invalid physics artifact collider state");
            const auto parsedId        = eve::PersistentId::parse(idText);
            const auto persistedHandle = PhysicsArtifactHandle::fromPacked(packed);
            if (!parsedId || parsedId->isNil() || persistedHandle.isInvalid() || backend != "box3d" ||
                shape != "triangle_mesh" || !identities.emplace(*parsedId).second ||
                !validGeometry(collider.vertices, collider.indices))
                return restoreFailure(eve::DiagnosticCode::Unsupported,
                                      "physics artifact state has unsupported backend or geometry");

            if (next == PhysicsArtifactHandle::invalidIndex)
                return restoreFailure(eve::DiagnosticCode::Failed,
                                      "physics artifact handle index exhausted during restore");
            collider.id       = *parsedId;
            collider.buildKey = std::move(buildKey);
            collider.shape    = std::move(shape);
            collider.backend  = std::move(backend);
            // Runtime handles are deliberately regenerated. This preserves
            // stale detection across clear/restore and keeps process-local
            // coordinates out of the persistent contract.
            collider.handle = PhysicsArtifactHandle(next++, 1u);

            auto world = std::make_unique<World3D>(0.f, 0.f, 0.f, true);
            auto body  = std::unique_ptr<Body3D>(world->newBody("static", 0.f, 0.f, 0.f));
            if (!body)
                return restoreFailure(eve::DiagnosticCode::Failed, "Box3D did not recreate the restored collider body");
            const auto mesh        = signedIndices(collider.indices);
            auto       shapeObject = std::unique_ptr<Shape3D>(body->newTriangleMeshShape(collider.vertices, mesh));
            if (!body || !shapeObject)
                return restoreFailure(eve::DiagnosticCode::Failed, "Box3D did not recreate a restored collider");
            State::RuntimeCollider runtime;
            runtime.descriptor = std::move(collider);
            runtime.world      = std::move(world);
            runtime.body       = std::move(body);
            runtime.shape      = std::move(shapeObject);
            candidate.push_back(std::move(runtime));
        }
    } catch (const std::exception& error) {
        return restoreFailure(eve::DiagnosticCode::Failed, std::string("Box3D restore failed: ") + error.what());
    } catch (...) {
        return restoreFailure(eve::DiagnosticCode::Failed, "Box3D restore failed");
    }

    state_->records.swap(candidate);
    nextIndex_ = next;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

PhysicsArtifactProvider& physicsArtifactProvider() noexcept {
    static PhysicsArtifactProvider provider;
    return provider;
}

void registerPhysicsArtifactProvider() {
    eve::cap::provide<eve::artifact::IPhysicsArtifactAdapter>(&physicsArtifactProvider());
}

}  // namespace eve::physics
