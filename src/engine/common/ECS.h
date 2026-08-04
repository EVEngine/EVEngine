#pragma once

// EVEngine ECS 集成层：底层实现使用 sunxfancy/ECS.hpp
// https://github.com/sunxfancy/ECS.hpp
#include <algorithm>  // ECS.hpp 使用 std::all_of（上游头未直接包含）
#include "ECS.hpp"

namespace ssq {
class Table;
}

namespace eve {

// 脚本侧 ECS 注册入口（实体工厂 / 组件声明），实现见 ECS.cpp
void exposeECS(ssq::Table& table);

}  // namespace eve
