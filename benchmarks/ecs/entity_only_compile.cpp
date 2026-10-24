#include <ECS.hpp>

namespace {
class EntityOnly : public ecs::Entity {
public:
    ENTITY(EntityOnly, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
};
}  // namespace

int main() {
    ecs::Table table;
    auto*      entity = EntityOnly::create(table);
    (void)entity;
    auto view = ecs::View<EntityOnly>(table);
    return view.begin() == view.end() ? 1 : 0;
}
