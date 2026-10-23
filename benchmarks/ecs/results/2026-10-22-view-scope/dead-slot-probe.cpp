#include <ECS.hpp>
#include <iostream>
struct Counted {
    inline static unsigned constructed = 0;
    Counted() { ++constructed; }
};
class Unit : public ecs::Entity {
public:
    ENTITY(Unit, ecs::Entity) void release() override { ecs::DestroyEntity(this); }
};
int main() {
    ecs::Table                     table;
    std::vector<ecs::EntityHandle> handles;
    for (unsigned i = 0; i < 130; ++i) handles.push_back(ecs::handle_of(Unit::create(table)));
    { auto view = ecs::View<Unit, Counted>(table); }
    for (unsigned i = 1; i < handles.size(); ++i) ecs::DestroyEntity(ecs::try_get(handles[i]));
    const auto before = Counted::constructed;
    unsigned   live   = 0;
    for (auto [value] : ecs::View<Unit, Counted>(table)) {
        (void)value;
        ++live;
    }
    std::cout << "live=" << live << " new_default_constructions=" << Counted::constructed - before << '\n';
    return live == 1 && Counted::constructed - before == 129 ? 0 : 1;
}
