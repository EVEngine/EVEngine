#pragma once

/**
 * @brief EVEngine ECS 集成层：底层实现使用 sunxfancy/ECS.hpp
 * https://github.com/sunxfancy/ECS.hpp
 */
#include <algorithm>  // ECS.hpp 使用 std::all_of（上游头未直接包含）
#include <functional>
#include <vector>
#include "ECS.hpp"

namespace ssq {
class Table;
class VM;
class Array;
}  // namespace ssq

namespace eve {

/** @brief 脚本侧 ECS：Entity / Component / System / ShaderSystem / view（见 ECS.cpp）。
 * C++ 游戏实体仍直接用 ECS.hpp 的 ENTITY / COMPONENT / View。
 * GPU System：eve.ShaderSystem + gpgpu::ShaderSystem / EcsGpu.h。 */
void exposeECS(ssq::Table& table);

/** @brief 在 ModuleManager::expose 之后调用；保证 eve.Component 等不被其它模块覆盖。 */
void exposeECSToVM(ssq::VM& vm);

/**
 * @brief C++ 实体 → 脚本 eve.view() 桥接。
 * 用 registerCppEntityView(typeid(T*).hash_code(), fn) 登记 T 的收集函数；
 * 脚本 eve.view(cls) 沿类链找到登记的 C++ 类型后调用 fn 填充输出数组。
 */
using CppEntityViewFn = std::function<void(ssq::Array& out)>;
void registerCppEntityView(size_t typeHash, CppEntityViewFn fn);

/**
 * @brief 在脚本 ECS 基类（eve.Component / eve.Entity / eve.System）注入之后执行的回调。
 * 模块用它注入"extends eve.Entity"的脚本基类（例如 eve.SceneEntity）。
 * 在 exposeECS / exposeECSToVM 末尾运行，早于任何游戏脚本。
 */
using PostEcsHook = std::function<void(ssq::Table& table)>;
void registerPostEcsHook(PostEcsHook fn);

/**
 * @brief Run post-ECS hooks that were registered after `exposeECS` already ran.
 *
 * Native module classes bind on first script access. A module may therefore
 * register a hook (or depend on Entity already existing) after the initial
 * `exposeECS` call; this flushes those pending hooks onto the live `eve` table.
 */
void flushPostEcsHooks(ssq::Table& table);

}  // namespace eve
