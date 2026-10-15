#include <zeroerr/unittest.h>
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

namespace {
template <class T>
std::size_t cameraCount(ecs::Table& table) {
    std::size_t count = 0;
    for (auto&& entity : ecs::View<T, typename T::Data>(table)) {
        (void)entity;
        ++count;
    }
    return count;
}
}  // namespace

TEST_CASE("graphics.cameraFactoryUsesCallerTable") {
    ecs::Table       table;
    ecs::ScopedTable scope(table);
    auto*            two   = eve::graphics::Camera2D::createCamera();
    auto*            three = eve::graphics::Camera3D::createCamera();
    CHECK(two != nullptr);
    CHECK(three != nullptr);
    CHECK(two->data()->entity == two);
    CHECK(three->data()->entity == three);
    CHECK(two->data()->active);
    CHECK(three->data()->active);
    CHECK(cameraCount<eve::graphics::Camera2D>(table) == 1);
    CHECK(cameraCount<eve::graphics::Camera3D>(table) == 1);
    ecs::DestroyEntity(two);
    ecs::DestroyEntity(three);
    CHECK(cameraCount<eve::graphics::Camera2D>(table) == 0);
    CHECK(cameraCount<eve::graphics::Camera3D>(table) == 0);
}
