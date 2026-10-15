#pragma once
// Internal snapshot codec, never exposed as a gameplay header.
#include <initializer_list>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "common/Snapshot.h"
#include "physics/backend/SimulationBackend.h"
class b2Fixture;
namespace eve::physics {
class World;
class World3D;
class Body;
class Body3D;
class Shape3D;
class Joint3D;
class Fixture;
namespace snapshot_detail {


constexpr std::string_view kWorld2DType       = "physics.world2d";
constexpr std::string_view kWorld2DSchema     = "physics:world2d";
constexpr std::string_view kWorld3DType       = "physics.world3d";
constexpr std::string_view kWorld3DSchema     = "physics:world3d";
constexpr std::uint64_t    kSnapshotVersion   = 3;
constexpr std::uint64_t    kSnapshot2DVersion = 3;

struct ObservationState {
    SimulationObservation value;
};
struct Body2DState {
    int         id = 0;
    std::string type;
    float       x = 0.f, y = 0.f, angle = 0.f;
    float       vx = 0.f, vy = 0.f, angularVelocity = 0.f;
    bool        active = false, bullet = false, awake = false, fixedRotation = false;
    eve::Value  fixtures;
    float       linearDamping = 0, angularDamping = 0, gravityScale = 1;
    bool        allowSleep = true;
};
struct Body3DState {
    int         id = 0;
    std::string type;
    float       x = 0.f, y = 0.f, z = 0.f;
    float       qx = 0.f, qy = 0.f, qz = 0.f, qw = 1.f;
    float       vx = 0.f, vy = 0.f, vz = 0.f;
    float       wx = 0.f, wy = 0.f, wz = 0.f;
    bool        active = false, bullet = false, awake = false, fixedRotation = false;
    float       linearDamping = 0, angularDamping = 0, gravityScale = 1, sleepThreshold = 0;
    bool        allowSleep = true;
};
template <typename BodyState>
eve::Result<void> matchBodyIds(const std::vector<BodyState>& states, std::set<int>& ids) {
    for (const BodyState& state : states) {
        if (!ids.insert(state.id).second)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "physics snapshot contains a duplicate body id", "payload.bodies.id"));
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}


/** @brief Read a finite float array, rejecting missing or malformed elements. */
[[nodiscard]] Result<std::vector<float>> readFloatArray(const Value::Object& object, std::string_view name);
/** @brief Internal exact-field schema predicate. */
bool hasExactFields(const Value::Object& object, std::initializer_list<std::string_view> expected);
/** @brief Borrow an optional field only during the current synchronous codec call.
 * @ownership Object owns the result; nullable on absence, invalid after object mutation/destruction.
 * @thread Calling thread; no callbacks or retained references.
 */
const Value*                           field(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<std::uint64_t>    readUint64(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<std::int64_t>     readInt64(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<float>            readFloat(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<bool>             readBool(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<std::string>      readString(const Value::Object& object, std::string_view name);
[[nodiscard]] Result<LogicalId>        snapshotSchema(std::string_view text);
[[nodiscard]] Result<void>             checkEnvelope(const SnapshotEnvelope& snapshot, std::string_view type,
                                                     std::string_view schema);
[[nodiscard]] Result<ObservationState> parseObservation(const Value::Object& object, const SnapshotEnvelope& snapshot);
[[nodiscard]] Result<std::vector<Body2DState>> parseBodies2D(const Value::Object& object);
[[nodiscard]] Result<std::vector<Body3DState>> parseBodies3D(const Value::Object& object);
/** @brief Owning material/filter projection from a synchronous backend observation. */
Value fixtureMaterialValue(const b2Fixture& fixture);
/** @brief Validate material fields before applying them to a detached fixture. */
[[nodiscard]] Result<void> restoreFixtureMaterial(Fixture& fixture, const Value& value);
/** @brief Migrate previous schema with explicit defaults for previously omitted state. */
[[nodiscard]] Result<void> migratePlanarV2(Value& payload);
Value                      fixtureTopologyValue(const Body& body);
Value                      shapesTopologyValue(const std::vector<Shape3D*>& shapes);
Value                      jointsTopologyValue(const std::vector<Joint3D*>& joints);
Value                      make2DPayload(const World& world, const std::vector<Body*>& bodies);
Value make3DPayload(const World3D& world, const std::vector<Body3D*>& bodies, const std::vector<Shape3D*>& shapes,
                    const std::vector<Joint3D*>& joints);
[[nodiscard]] Result<std::unique_ptr<World>>   prepareWorld2D(const std::vector<Body2DState>& states, float gravityX,
                                                              float gravityY, float meter,
                                                              const SimulationObservation& observation,
                                                              PersistentId                 instanceId);
[[nodiscard]] Result<std::unique_ptr<World3D>> prepareWorld3D(const std::vector<Body3DState>& states,
                                                              const Value::Array& shapes, const Value::Array& joints,
                                                              float gravityX, float gravityY, float gravityZ,
                                                              const SimulationObservation& observation,
                                                              PersistentId instanceId, bool legacyParameters,
                                                              bool allowSleep);
}  // namespace snapshot_detail
}  // namespace eve::physics
