#pragma once

/**
 * @brief EVEngine ECS 集成层：底层实现使用 sunxfancy/ECS.hpp
 * https://github.com/sunxfancy/ECS.hpp
 */
#include <algorithm>  // ECS.hpp 使用 std::all_of（上游头未直接包含）
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "ECS.hpp"

#include "common/Generation.h"
#include "common/Result.h"
#include "common/SchemaVersion.h"
#include "common/Value.h"

namespace ssq {
class Table;
class VM;
class Array;
class Class;
class Object;
}  // namespace ssq

namespace eve {

/** @brief 脚本侧 ECS：Entity / Component / System / ShaderSystem / view（见 ECS.cpp）。
 * C++ 游戏实体仍直接用 ECS.hpp 的 ENTITY / COMPONENT / View。
 * GPU System：eve.ShaderSystem + gpgpu::ShaderSystem / EcsGpu.h。 */
void exposeECS(ssq::Table& table);

/**
 * @brief 脚本组件注册表的一条记录。
 *
 * 组件的身份是**名字 + 版本**，不是类对象：热重载会重新定义同名类、类对象会换掉，而名字
 * 与版本不变——这正是"旧实例能被新版本接管"的前提（对齐 `DefinitionRegistry` 的
 * `(type,id)` + `SchemaVersion` + `Generation` + tombstone 范式）。
 */
struct ScriptComponentRecord {
    /** @brief 脚本侧声明的组件名。 */
    std::string name;
    /** @brief 脚本侧声明的版本（正整数）。 */
    SchemaVersion version;
    /** @brief 代际：同名同版本每次重新登记 +1。 */
    Generation generation;
    /** @brief 该代已被同名同版本的新代替换（墓碑），只留作可观测与迁移用。 */
    bool retired = false;
    /**
     * @brief 该代声明的字段名，**按声明顺序**（基类字段在前）。
     *
     * 顺序是声明顺序而不是成员表的遍历顺序：`_members` 给的是槽位号、`_defaultvalues` 按声明
     * 追加，所以两者合起来能还原声明顺序（见 §10.17）。schema 相关消费者需要它稳定——增删
     * 字段不该让无关字段换位置。
     *
     * @remarks 墓碑记录保留**旧代**的字段列表，迁移要知道"从哪一组字段搬到哪一组"。
     */
    std::vector<std::string> fields;
};

/**
 * @brief 登记一个脚本组件：`eve.component(name, version, cls)` 的 C++ 侧。
 *
 * @param name 非空组件名。
 * @param version 正整数版本。
 * @param cls 必须是 Squirrel 类对象。
 * @return 成功，或说明原因的失败（名字空 / 版本为 0 / 不是类）。
 * @remarks 同名同版本再次登记 = 热重载换了一代：旧代记为 retired、新代 generation +1。
 *          登记按 VM 隔离（新 VM 从空开始，见 `prepareEcsScriptInjection`），且**不改变
 *          现有脚本写法**——类对象仍然照旧驱动 `eve.view` / `create`。
 * @cost 一次 map 查找 + 一次 `sq_addref`；只应在脚本加载/热重载时调用。
 */
[[nodiscard]] Result<void> registerScriptComponent(const std::string& name, SchemaVersion version,
                                                   const ssq::Object& cls);

/**
 * @brief 当前未退役的那条登记。
 * @return 只读指针，指向注册表内部记录；没有该名字+版本时返回 nullptr。
 * @lifetime 在下一次 `registerScriptComponent` 或 `prepareEcsScriptInjection` 前有效。
 */
[[nodiscard]] const ScriptComponentRecord* findScriptComponent(const std::string& name, SchemaVersion version);

/**
 * @brief 当前 VM 的全部登记（含 retired 历史），按名字、版本、代际排序。
 * @cost 一次拷贝；仅供编辑器/诊断/测试等非热路径使用。
 */
[[nodiscard]] std::vector<ScriptComponentRecord> scriptComponentRecords();

/**
 * @brief 读一个脚本组件实例的字段，**不关心这个字段存在哪**。
 *
 * 登记过的组件（`eve.component(name, version, cls)`）的字段在引擎侧存储里，没登记的在实例
 * 自己的槽位里。原生消费者（GPU 打包、存档、编辑器）不该自己判断这件事——那是 ECS 的归属
 * 约定（登记时摘槽位、装上 `_get`/`_set`），在别处再写一份必然漂移。所以这里由 ECS 统一回答。
 *
 * @param instance 组件**实例**（`eve.Component` 子类的实例），不是类。
 * @param field 字段名；登记组件必须是 schema 里声明过的，未登记组件必须是实例上真实存在的槽位。
 * @return 字段值；`NotFound` 表示没有这个字段，`InvalidArgument` 表示传进来的不是实例。
 * @remarks 返回的是**拷贝**，不引用脚本对象，可以跨帧保存。
 * @cost 登记组件一次存储查找（键含字段名）；未登记组件一次 Squirrel 槽位查找。
 *        热路径请按 `(实体, 组件名, 版本)` 直接用 `common/ScriptComponentStore.h`，不要按名字反复问。
 */
[[nodiscard]] EVENGINE_API Result<Value> readScriptComponentField(const ssq::Object& instance,
                                                                  const std::string& field);

/**
 * @brief 写一个脚本组件实例的字段，归属规则见 `readScriptComponentField`。
 *
 * @param instance 组件实例。
 * @param field 字段名。
 * @param value 新值（按 `Value` 的通用类型转成 Squirrel 值）。
 * @return 成功，或 `NotFound`（没有这个字段）/ `InvalidArgument`（不是实例）。
 * @remarks 未登记组件写的是实例槽位；登记组件写的是引擎侧存储——脚本侧下一次读就看见新值。
 * @cost 同 `readScriptComponentField`。
 */
[[nodiscard]] EVENGINE_API Result<void> writeScriptComponentField(const ssq::Object& instance, const std::string& field,
                                                                  const Value& value);

/** @brief Resets per-VM script injection bookkeeping before a new VM is exposed. */
void prepareEcsScriptInjection();

/** @brief 在 ModuleManager::expose 之后调用；保证 eve.Component 等不被其它模块覆盖。 */
void exposeECSToVM(ssq::VM& vm);

/**
 * @brief C++ 实体 → 脚本 eve.view() 桥接。
 * 用 registerCppEntityView(cls, fn) 登记脚本类的收集函数；
 * 脚本 eve.view(cls) 沿类链找到登记的 C++ 类型后调用 fn 填充输出数组。
 */
using CppEntityViewFn = std::function<void(ssq::Array& out)>;
void registerCppEntityView(const ssq::Class& cls, CppEntityViewFn fn);

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
