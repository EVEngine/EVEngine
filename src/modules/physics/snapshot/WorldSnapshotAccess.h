#include "physics/box3d/Box3DBody.h"
#pragma once
// Internal, narrowly scoped access for detached topology construction.
#include "physics/World.h"

#include "physics/Body.h"
#include "physics/Body3D.h"
#include "physics/Fixture.h"
#include "physics/Joint3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"
#include "physics/backend/SimulationBackend.h"

#include <Box2D/Box2D.h>
#include <box3d/box3d.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace eve::physics {
struct WorldSnapshotAccess {
    /** @brief Capture native shape contact state and wrapper-owned collision settings. */
    static Value shapeContact(const Shape3D& shape);
    /** @brief Restore validated candidate contact state; return density for deferred mass preparation. */
    [[nodiscard]] static Result<float> restoreShapeContact(Shape3D& shape, const Value::Object& source);
    /** @brief Preflight candidate mass/inertia arithmetic before applying backend mass updates. */
    [[nodiscard]] static Result<void> finishShapeMass(World3D& world);
    /** @brief Capture transmissions with stable source ids and current unwrapped phase. */
    [[nodiscard]] static Result<Value> captureCouplings3D(const World3D& world);
    /** @brief Rebuild transmissions after candidate handles and native joints are ready. */
    [[nodiscard]] static Result<void> restoreCouplings3D(World3D& world, const Value::Array& values);
    /** @brief Capture owning joint definitions; rejects unregistered backend joints. */
    [[nodiscard]] static Result<Value> captureJoints2D(const World& world);
    /** @brief Rebuild validated joint definitions inside a detached candidate, allocating fresh runtime handles. */
    [[nodiscard]] static Result<void> restoreJoints2D(World& candidate, const Value::Array& joints,
                                                      std::uint32_t firstHandleIndex);

    /** @brief Candidate owns newly reconstructed wrappers so rollback releases all allocations. */
    static void ownPreparedBody(World& world, std::unique_ptr<Body> body) {
        world.restoredBodies_.push_back(std::move(body));
    }
    /** @brief Candidate owns a fixture wrapper after registering it with its backend. */
    static void ownPreparedFixture(World& world, std::unique_ptr<Fixture> fixture) {
        world.restoredFixtures_.push_back(std::move(fixture));
    }
    /** @brief Detached 3D candidates own all reconstructed wrappers through commit or rollback. */
    static void ownPreparedBody(World3D& world, std::unique_ptr<Body3D> body) {
        world.restoredBodies_.push_back(std::move(body));
    }
    /** @brief Transfer one reconstructed shape wrapper to its candidate world. */
    static void ownPreparedShape(World3D& world, std::unique_ptr<Shape3D> shape) {
        world.restoredShapes_.push_back(std::move(shape));
    }
    /** @brief Transfer one reconstructed joint wrapper to its candidate world. */
    static void ownPreparedJoint(World3D& world, std::unique_ptr<Joint3D> joint) {
        world.restoredJoints_.push_back(std::move(joint));
    }
    static eve::Value shapeSource(const Shape3D& shape) {
        eve::Value::Object object;
        object.emplace("bodyId", eve::Value(std::to_string(shape.body_ ? shape.body_->getId() : -1)));
        object.emplace("id", eve::Value(std::to_string(shape.id_)));
        object.emplace("kind", eve::Value(shape.getKind()));
        object.emplace("contact", shapeContact(shape));
        object.emplace("localTransform",
                       Value(Value::Array{Value(shape.getLocalX()), Value(shape.getLocalY()), Value(shape.getLocalZ()),
                                          Value(shape.getLocalRotX()), Value(shape.getLocalRotY()),
                                          Value(shape.getLocalRotZ()), Value(shape.getLocalRotW())}));
        object.emplace("a", eve::Value(shape.geometry_.a));
        object.emplace("b", eve::Value(shape.geometry_.b));
        object.emplace("c", eve::Value(shape.geometry_.c));
        auto floats = [](const std::vector<float>& source) {
            eve::Value::Array values;
            values.reserve(source.size());
            for (float value : source) values.emplace_back(value);
            return eve::Value(std::move(values));
        };
        auto integers = [](const auto& source) {
            eve::Value::Array values;
            values.reserve(source.size());
            for (auto value : source) values.emplace_back(std::to_string(static_cast<std::int64_t>(value)));
            return eve::Value(std::move(values));
        };
        object.emplace("hullVertices", floats(shape.geometry_.hullVertices));
        object.emplace("hullMaxVertices", eve::Value(std::to_string(shape.geometry_.hullMaxVertices)));
        object.emplace("meshVertices", floats(shape.geometry_.meshVertices));
        object.emplace("meshIndices", integers(shape.geometry_.meshIndices));
        object.emplace("meshWeldVertices", eve::Value(shape.geometry_.meshWeldVertices));
        object.emplace("meshWeldTolerance", eve::Value(shape.geometry_.meshWeldTolerance));
        object.emplace("meshIdentifyEdges", eve::Value(shape.geometry_.meshIdentifyEdges));
        object.emplace("meshUseMedianSplit", eve::Value(shape.geometry_.meshUseMedianSplit));
        object.emplace("meshMaterialIndices", integers(shape.geometry_.meshMaterialIndices));
        object.emplace("heightValues", floats(shape.geometry_.heightValues));
        object.emplace("heightCountX", eve::Value(std::to_string(shape.geometry_.heightCountX)));
        object.emplace("heightCountZ", eve::Value(std::to_string(shape.geometry_.heightCountZ)));
        object.emplace("heightCellSizeX", eve::Value(shape.geometry_.heightCellSizeX));
        object.emplace("heightCellSizeZ", eve::Value(shape.geometry_.heightCellSizeZ));
        object.emplace("heightGlobalMin", eve::Value(shape.geometry_.heightGlobalMin));
        object.emplace("heightGlobalMax", eve::Value(shape.geometry_.heightGlobalMax));
        object.emplace("heightClockwise", eve::Value(shape.geometry_.heightClockwise));
        return eve::Value(std::move(object));
    }

    static void                     setBodyId(Body& body, int id) { body.id_ = id; }
    static void                     setBodyId(Body3D& body, int id) { body.id_ = id; }
    static void                     setShapeId(Shape3D& shape, int id) { shape.id_ = id; }
    static void                     setJointId(Joint3D& joint, int id) { joint.id_ = id; }
    static std::unique_ptr<World3D> makeDetachedWorld3D(float gx, float gy, float gz, eve::PersistentId instanceId,
                                                        bool allowSleep) {
        return std::unique_ptr<World3D>(new World3D(gx, gy, gz, allowSleep, instanceId, false, RigidBackend3D::Box3D));
    }
    static eve::Result<void> finishPrepared(World& world, int nextId, const SimulationObservation& observation) {
        world.nextId_ = nextId;
        auto restored = world.simulation_->restoreObservation(observation);
        if (!restored) return restored;
        world.simulationTick_ = observation.lastTick;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    static eve::Result<void> finishPrepared(World3D& world, int nextBodyId, int nextShapeId, int nextJointId,
                                            const SimulationObservation& observation) {
        world.nextId_      = nextBodyId;
        world.nextShapeId_ = nextShapeId;
        world.nextJointId_ = nextJointId;
        auto restored      = world.simulation_->restoreObservation(observation);
        if (!restored) return restored;
        world.simulationTick_ = observation.lastTick;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }

    /** @brief Allocate fresh 3D links entirely in the detached candidate before publishing topology. */
    [[nodiscard]] static Result<void> prepareRuntimeLinks(const World3D& live, World3D& prepared) {
        try {
            prepared.nextBodyHandleIndex_  = live.nextBodyHandleIndex_;
            prepared.nextShapeHandleIndex_ = live.nextShapeHandleIndex_;
            prepared.nextJointHandleIndex_ = live.nextJointHandleIndex_;
            prepared.refreshRuntimeHandlesAfterRestore();
            return Result<void>::success();
        } catch (const std::exception& error) {
            return Result<void>::failure(Diagnostic::error(
                DiagnosticCode::Failed, std::string("Cannot prepare restored 3D links: ") + error.what(),
                "physics.world3d.restore.links"));
        }
    }

    static void adopt(World& live, World& prepared) { live.adoptPreparedTopology(prepared); }
    static void adopt(World3D& live, World3D& prepared) { live.adoptPreparedTopology(prepared); }
};
}  // namespace eve::physics
