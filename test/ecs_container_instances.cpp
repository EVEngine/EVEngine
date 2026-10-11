#include <zeroerr/assert.h>
#include <zeroerr/unittest.h>
#include <ECS.hpp>

namespace {
class ContainerBase : public ecs::Entity {
public:
    ENTITY(ContainerBase, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }
    struct Position {
        int value = 0;
    };
    COMPONENT(Position, position)
};

class ContainerDerived : public ContainerBase {
public:
    ENTITY(ContainerDerived, ContainerBase)
    struct Detail {
        int value = 0;
    };
    COMPONENT(Detail, detail)
};
}  // namespace

TEST_CASE("ecs.containers_keep_table_isolation_and_stale_handle_detection") {
    ecs::Table first;
    ecs::Table second;
    auto*      a         = ContainerDerived::create(first);
    auto*      b         = ContainerDerived::create(second);
    a->position()->value = 7;
    b->position()->value = 9;
    const auto handle    = ecs::handle_of(a);
    CHECK(ecs::try_get(handle) == a);
    CHECK(first.getManager(typeid(ContainerDerived)) != second.getManager(typeid(ContainerDerived)));
    {
        ecs::ScopedTable scope(second);
        CHECK_EQ(a->position()->value, 7);
        CHECK_EQ(b->position()->value, 9);
    }
    ecs::DestroyEntity(a);
    CHECK(ecs::try_get(handle) == nullptr);
    auto* replacement = ContainerDerived::create(first);
    CHECK_EQ(replacement->id, handle.id);
    CHECK(replacement->generation != handle.generation);
    CHECK(ecs::try_get(ecs::handle_of(replacement)) == replacement);
}

TEST_CASE("ecs.containers_publish_deferred_inherited_components") {
    ecs::Table table;
    {
        ecs::ScopedDefer scope(table);
        auto*            entity   = ContainerDerived::create(table);
        entity->position()->value = 4;
        entity->detail()->value   = 8;
        int visible               = 0;
        for (auto [position] : ecs::View<ContainerBase, ContainerBase::Position>(table)) {
            (void)position;
            ++visible;
        }
        CHECK_EQ(visible, 0);
    }
    int visible = 0;
    for (auto [position, detail] :
         ecs::View<ContainerDerived, ContainerBase::Position, ContainerDerived::Detail>(table)) {
        CHECK_EQ(position->value, 4);
        CHECK_EQ(detail->value, 8);
        ++visible;
    }
    CHECK_EQ(visible, 1);
}
