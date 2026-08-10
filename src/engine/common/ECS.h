#pragma once

// EVEngine ECS 集成层：底层实现使用 sunxfancy/ECS.hpp
// https://github.com/sunxfancy/ECS.hpp
#include <algorithm>  // ECS.hpp 使用 std::all_of（上游头未直接包含）
#include "ECS.hpp"

namespace ssq {
class Table;
class VM;
}  // namespace ssq

namespace eve {

// 脚本侧 ECS：Entity / Component / System / ShaderSystem / view（见 ECS.cpp）。
// C++ 游戏实体仍直接用 ECS.hpp 的 ENTITY / COMPONENT / View。
// GPU System：eve.ShaderSystem + gpgpu::ShaderSystem / EcsGpu.h。
void exposeECS(ssq::Table& table);

// 在 ModuleManager::expose 之后调用；保证 eve.Component 等不被其它模块覆盖。
void exposeECSToVM(ssq::VM& vm);

}  // namespace eve
