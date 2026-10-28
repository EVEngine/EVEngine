#pragma once

// Private backend-independent query guards and compatibility-result adapters.
#include <cmath>
#include "common/Exception.h"
#include "physics/Body3D.h"
#include "physics/Shape3D.h"
#include "physics/World3D.h"

namespace eve::physics::world3d_detail {

class ScopedQueryState3D {
public:
    ScopedQueryState3D(World3D &world, QueryFilter3D filter)
        : world_(world),
          category_(world.getQueryCategoryBits()),
          mask_(world.getQueryMaskBits()),
          body_(world.getQueryIgnoredBodyId()),
          shape_(world.getQueryIgnoredShapeId()) {
        world_.setQueryFilter(static_cast<int>(filter.categoryBits), static_cast<int>(filter.maskBits));
        world_.setQueryIgnoredBodyId(filter.ignoredBodyId);
        world_.setQueryIgnoredShapeId(filter.ignoredShapeId);
    }

    ~ScopedQueryState3D() noexcept {
        world_.setQueryFilter(category_, mask_);
        world_.setQueryIgnoredBodyId(body_);
        world_.setQueryIgnoredShapeId(shape_);
    }

private:
    World3D &world_;
    int      category_;
    int      mask_;
    int      body_;
    int      shape_;
};

template <class T>
eve::Result<T> ownedQueryFailure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, message, path, {}, "physics.query3d"));
}

template <class T>
T worldValue(eve::Result<T> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
    return std::move(result).value();
}
inline void worldValue(eve::Result<void> result) {
    if (!result.ok()) throw eve::Exception("%s", result.error()->message().c_str());
}
}  // namespace eve::physics::world3d_detail
