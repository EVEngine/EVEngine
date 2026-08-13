#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "common/ECS.h"
#include <cmath>

class Node : public ecs::Entity {
public:
    ENTITY(Node, ecs::Entity)

    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)
};

class Movable : public Node {
public:
    ENTITY(Movable, Node)

    void release() override { ecs::DestroyEntity(this); }

    struct Velocity {
        float dx = 1;
        float dy = 1;
    };
    COMPONENT(Velocity, velocity)
};

static int countNodes() {
    int n = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it)
        ++n;
    return n;
}

TEST_CASE("ECS.basicCreateAndComponent") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    Movable* b = Movable::create();

    a->position()->x = 10;
    a->position()->y = 0;

    b->position()->x = 0;
    b->position()->y = 10;
    b->velocity()->dx = 2;
    b->velocity()->dy = 2;

    CHECK(std::abs(a->position()->x - 10.f) < 1e-5f);
    CHECK(std::abs(b->position()->y - 10.f) < 1e-5f);
    CHECK(std::abs(b->velocity()->dx - 2.f) < 1e-5f);

    a->release();
    b->release();
}

TEST_CASE("ECS.viewIncludesSubclass") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node::create();
    Movable::create();

    int count = 0;
    auto view = ecs::View<Node, Node::Position>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [pos] = *it;
        pos->x = 0;
        pos->y = 0;
        ++count;
    }
    CHECK_EQ(count, 2);
}

TEST_CASE("ECS.dynamicCreateDestroy") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    Movable* b = Movable::create();
    a->position()->x = 3.f;
    b->position()->x = 7.f;
    CHECK(ecs::is_entity_visible(a));
    CHECK(ecs::is_entity_visible(b));
    CHECK_EQ(countNodes(), 2);

    ecs::EntityHandle ha = ecs::handle_of(a);
    const uint32_t oldGen = a->generation;
    a->release();
    CHECK(!ecs::is_entity_visible(a));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK_EQ(countNodes(), 1);

    Node* c = Node::create();
    c->position()->x = 11.f;
    CHECK(ecs::is_entity_visible(c));
    CHECK(ecs::try_get(ha) == nullptr);
    CHECK(ecs::try_get(ecs::handle_of(c)) == c);
    CHECK_EQ(countNodes(), 2);
    if (c == a)
        CHECK(c->generation != oldGen);

    b->release();
    CHECK_EQ(countNodes(), 1);
    c->release();
    CHECK_EQ(countNodes(), 0);
    CHECK(ecs::try_get(ecs::handle_of(c)) == nullptr);
}

TEST_CASE("ECS.deferredCreateDestroyDuringView") {
    ecs::Table world;
    ecs::ScopedTable guard(world);

    Node* a = Node::create();
    a->position()->x = 1.f;
    CHECK_EQ(countNodes(), 1);

    {
        ecs::ScopedDefer defer;
        Node* spawned = Node::create();
        CHECK((spawned->flags & ecs::kEntityStaging) != 0);
        CHECK_EQ(countNodes(), 1);

        a->release();
        CHECK((a->flags & ecs::kEntityDead) != 0);
        CHECK_EQ(countNodes(), 0);
        // spawned is in the staging table; do not keep the pointer after commit.
    }

    CHECK_EQ(countNodes(), 1);
}
