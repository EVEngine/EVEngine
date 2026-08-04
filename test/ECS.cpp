#include <gtest/gtest.h>
#include "common/ECS.h"

class Node : public ecs::Entity {
public:
    ENTITY(Node, ecs::Entity)

    void release() override {}

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)
};

class Movable : public Node {
public:
    ENTITY(Movable, Node)

    void release() override {}

    struct Velocity {
        float dx = 1;
        float dy = 1;
    };
    COMPONENT(Velocity, velocity)
};

TEST(ECS, basicCreateAndComponent) {
    Node* a = Node::create();
    Movable* b = Movable::create();

    a->position()->x = 10;
    a->position()->y = 0;

    b->position()->x = 0;
    b->position()->y = 10;
    b->velocity()->dx = 2;
    b->velocity()->dy = 2;

    EXPECT_FLOAT_EQ(a->position()->x, 10);
    EXPECT_FLOAT_EQ(b->position()->y, 10);
    EXPECT_FLOAT_EQ(b->velocity()->dx, 2);
}

TEST(ECS, viewIncludesSubclass) {
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
    EXPECT_GE(count, 2);
}
