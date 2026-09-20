#pragma once

/**
 * @file ScriptComponentPayloadProvider.h
 * @brief 让编辑器 inspector 读到脚本组件的字段（J-3 编辑器通路的第一版：**只读**）。
 */

#include "common/ScriptComponentSnapshot.h"
#include "scene/editing/SceneComponentPayload.h"

#include <functional>
#include <string>
#include <vector>

namespace eve::scene_editor {

/**
 * @brief 脚本组件（`eve.component` 登记过的）在场景 inspector 里的只读属性提供方。
 *
 * 一个提供方服务**所有**脚本组件：`componentType()` 是固定槽 `"eve.script-component"`，具体是哪个
 * 组件由 `SceneComponentPayloadRef::component` 里的稳定 id `"<组件名>@<版本>"` 决定。这样编辑器里
 * 所有脚本定义出来的组件集中在一类下，而它们的名字与版本（热重载不变的那两个身份）仍然逐条可见；
 * 换成"每个组件类型一个提供方"就需要随脚本登记/热重载动态增删提供方，代价与收益不成比例。
 *
 * | 对象 id | 含义 |
 * | --- | --- |
 * | `SceneComponentPayloadRef::target` | 场景**宿主名**（`TargetId` 就是宿主名） |
 * | `SceneComponentPayloadRef::object` | 节点 id |
 * | `SceneComponentPayloadRef::component` | 稳定组件 id：`"<组件名>@<版本>"` |
 *
 * @remarks 本版**只读**：`schema`/`read` 给得出真实字段与值（值来自引擎侧那一个权威存储），而
 *          `makeSet`/`makeReset`/`payloadOperationTarget` 明确返回 `Unsupported`——写与撤销要有
 *          `IDomainOperationTarget` + staging（对齐 §9.71 记录的那条纪律），下一片做。返回明确失败
 *          而不是"计划成功但什么都没发生"，是因为后者会让编辑器显示一个假的成功。
 */
class ScriptComponentPayloadProvider final : public scene_editing::ISceneComponentPayloadProvider {
public:
    /**
     * @brief 编辑器目标 id → 场景**宿主名**。
     *
     * 自动化创建的编辑器目标 id 是**调用方选的**（`eve_editor_target_create` 的 `target` 字段），不
     * 等于宿主名；宿主名是创建那个目标时才知道的事实（`scene-host` 目标的 `host` 请求字段）。
     * 所以这条映射由创建目标的模块注入（`SceneEditorModule`），提供方不猜。
     */
    using HostResolver = std::function<std::string(const scene_editing::TargetId&)>;

    /**
     * @brief 构造。
     * @param resolver 目标 id → 宿主名；**空**表示"目标 id 本身就是宿主名"（测试与简单嵌入用）。
     * @ownership 回调归调用方所有，必须在提供方存活期间保持可调用（不捕获即将销毁的对象）。
     */
    explicit ScriptComponentPayloadProvider(HostResolver resolver = {});

    /** @brief 稳定组件类型槽：所有脚本组件共用。 */
    static const std::string& payloadType();

    const std::string& componentType() const override;

    /** @brief 节点上每个脚本实体实例的**已登记**组件各一条；未登记组件不出现（§9.62.1）。 */
    std::vector<scene_editing::SceneComponentPayloadRef> components(const scene_editing::TargetId& scene,
                                                                    const scene_editing::ObjectId& object) const override;

    /** @brief 只读 v1：没有可写的域目标。 */
    scene_editing::EditorResult<scene_editing::IDomainOperationTarget*> payloadOperationTarget(
        const scene_editing::SelectionSnapshot& selection) const override;

    /** @brief 引用还在不在：组件名+版本还登记着、且实例上仍挂着它。 */
    std::vector<scene_editing::EditorDiagnostic> validateComponent(
        const scene_editing::SceneComponentPayloadRef& component) const override;

    /** @brief 引擎侧存储的写版本号（任何一次写都会让它变），供 inspector 判断要不要刷新。 */
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(
        const scene_editing::SelectionSnapshot& selection) const override;

    /** @brief 选中组件的字段表；字段类型由**当前值**推断（脚本的声明里只有默认值，没有类型）。 */
    scene_editing::PropertySchema schema(const scene_editing::SelectionSnapshot& selection) const override;

    /** @brief 读一个字段：值来自引擎侧存储，不是脚本实例的槽位。 */
    scene_editing::PropertyReadResult read(const scene_editing::SelectionSnapshot& selection,
                                           const scene_editing::PropertyPath&      path) const override;

    /** @brief 只读 v1：明确拒绝而不是给一个假的成功。 */
    scene_editing::EditorResult<scene_editing::DomainOperation> makeSet(
        const scene_editing::SelectionSnapshot& selection, const scene_editing::PropertyPath& path,
        const scene_editing::EditorValue& value, scene_editing::PropertySetMode mode) const override;

    /** @brief 只读 v1：明确拒绝。 */
    scene_editing::EditorResult<scene_editing::DomainOperation> makeReset(
        const scene_editing::SelectionSnapshot& selection, const scene_editing::PropertyPath& path) const override;

private:
    /** 选中项解析成一个组件实例的状态；解析不出来时返回失败（编辑器据此显示"引用过期"）。 */
    [[nodiscard]] eve::Result<std::vector<eve::ScriptComponentInstanceState>> resolve(
        const scene_editing::SelectionSnapshot& selection) const;

    /** 目标 id → 宿主名（空 resolver 时就是目标 id 本身）。 */
    [[nodiscard]] std::string hostNameOf(const scene_editing::TargetId& target) const;

    HostResolver resolver_;
};

}  // namespace eve::scene_editor
