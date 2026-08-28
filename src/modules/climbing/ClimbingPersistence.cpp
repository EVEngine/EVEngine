#include "climbing/Climbing.h"

#include "climbing/ClimbingCodec.h"

#include "physics/World3D.h"

#include <utility>

namespace eve::climbing {

eve::Result<void> ClimbingRuntime::reloadProfile(ClimbingProfile profile) {
    return publishProfile(std::move(profile), true);
}

eve::Result<void> ClimbingRuntime::setProfileJson(std::string_view json) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<void>::failure(value.status());
    auto profile = decodeClimbingProfileDefinition(value.value());
    if (!profile) return eve::Result<void>::failure(profile.status());
    return setProfile(std::move(profile).takeValue());
}

eve::Result<void> ClimbingRuntime::reloadProfileJson(std::string_view json) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<void>::failure(value.status());
    auto profile = decodeClimbingProfileDefinition(value.value());
    if (!profile) return eve::Result<void>::failure(profile.status());
    return reloadProfile(std::move(profile).takeValue());
}

eve::Result<std::string> ClimbingRuntime::snapshotJson() const {
    auto value = snapshot();
    if (!value) return eve::Result<std::string>::failure(value.status());
    return value.value().toJson();
}

eve::Result<void> ClimbingRuntime::restoreJson(std::string_view json, physics::World3D& world) {
    auto value = eve::Value::fromJson(json);
    if (!value) return eve::Result<void>::failure(value.status());
    return restore(value.value(), world);
}

}  // namespace eve::climbing
