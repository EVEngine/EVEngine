#include <gtest/gtest.h>
#include "common/ECS.h"

using namespace eve;

struct position {
    int x, y;
};

ComponentRegister<position> position_reg;


class Person : public Entity {
public:
    Component<position> pos;
    Person() : Entity(), pos(*this) {}
};


TEST(ECS, basic) {
    Person person_a;
    person_a.pos->x = 10;
    person_a.pos->y = 0;
}