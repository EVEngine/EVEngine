#pragma once

/**
 * @file ScriptComponentPayloadProvider.h
 * @brief 让编辑器 inspector 读写脚本组件的字段（J-3 编辑器通路）。
 */

#include "common/ScriptComponentSnapshot.h"
#include "scene/editing/SceneComponentPayload.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace eve::scene_editor {

/**
 * @brief 脚本组件字段的域操作目标：往引擎侧存储写字段的**唯一**通路。
 *
 * 同一个类按两种身份工作，由是不是 `cloneDomainState()` 的产物决定：
 *
 * | 身份 | 谁调用 | 写到哪里 |
 * | --- | --- | --- |
 * | 权威实例 | `LocalWorldAuthority::commit`（正向写入） | 直接写引擎侧存储 |
 * | 候选副本 | `cloneDomainState()` 之后（撤销/重做） | 只改自己那份字段副本，`commitDomainState` 才发布 |
 *
 * **候选副本绝不写存储**——"候选上试算失败不留半套状态"就靠这一条（对齐
 * `AudioSourcePublishingTarget`：候选从不发布，只有权威实例发布）。
 *
 * @remarks 一个实例同一时刻只服务一个组件实例：`bind` 记下 `(实体, 组件名, 版本)`。这是安全的，
 *          因为域操作载荷里**也带着**组件身份（节点 + 名字 + 版本），绑错了的过期目标会明确
 *          `Conflict` 拒绝，而不是把值写到另一个组件上。
 * @remarks 主人线程亲和：读写引擎侧存储、以及撤销时的整份发布都按主线程约定，实现不做同步。
 */
class ScriptComponentOperationTarget final : public scene_editing::IDomainOperationTarget,
                                             public scene_editing::IDomainOperationTargetStaging {
public:
    /** @brief 域操作类型：一个脚本组件字段的绝对赋值。 */
    static std::string_view setOperationType();

    ScriptComponentOperationTarget() = default;

    /**
     * @brief 绑定到一个组件实例（编辑器每次解析选中项时重新绑定）。
     *
     * @param target 编辑器自动化目标 id（域操作与事务都用它做归属校验）。
     * @param nodeId 场景节点 id（进操作载荷，用来认出"这个操作是不是写这个组件的"）。
     * @param entityId 脚本 VM 内的实体 id（引擎侧存储键的第一段）。
     * @param name 组件名；@param version 组件版本。
     * @param generation 绑定时的注册代际（同名同版本换了一代就是引用过期）。
     * @remarks 绑定只记身份，不校验身份还有效：校验发生在写的那一刻，那时才有最新事实。
     */
    void bind(const scene_editing::TargetId& target, std::string nodeId, std::uint64_t entityId, std::string name,
              eve::SchemaVersion version, eve::Generation generation);

    scene_editing::TargetId   targetId() const override { return target_; }
    std::uint64_t             revision() const override;
    scene_editing::EditRegion dirtyRegion() const override { return {}; }
    void                      clearDirtyRegion() override {}

    eve::Result<void> applyDomainOperation(const scene_editing::DomainOperation& operation) override;
    [[nodiscard]] std::unique_ptr<scene_editing::IDomainOperationTarget> cloneDomainState() const override;
    [[nodiscard]] eve::Result<void>                                      commitDomainState(
        std::unique_ptr<scene_editing::IDomainOperationTarget> candidate) override;

private:
    /** 把一次字段赋值写进"当前身份该写的地方"：候选副本写副本，权威实例写存储。 */
    [[nodiscard]] eve::Result<void> writeField(const std::string& field, const eve::Value& value);

    scene_editing::TargetId target_;
    std::string             nodeId_;
    std::uint64_t           entityId_ = 0;
    std::string             name_;
    eve::SchemaVersion      version_;
    eve::Generation         generation_;
    /** 只在候选副本上为真：为真时 `applyDomainOperation` 不碰引擎侧存储。 */
    bool staging_ = false;
    bool bound_   = false;
    /** 候选副本持有的整份字段（按绑定时的权威状态克隆，发布时整份写回）。 */
    eve::Value::Object fields_;
};

/**
 * @brief 脚本组件（`eve.component` 登记过的）在场景 inspector 里的属性与写回通道。
 *
 * 一个提供方服务**所有**脚本组件：`componentType()` 是固定槽 `"eve.script-component"`，具体是哪个
 * 组件由 `SceneComponentPayloadRef::component` 里的稳定 id `"<节点>|<组件名>@<版本>"` 决定。这样编辑器里
 * 所有脚本定义出来的组件集中在一类下，而它们的名字与版本（热重载不变的那两个身份）仍然逐条可见；
 * 换成"每个组件类型一个提供方"就需要随脚本登记/热重载动态增删提供方，代价与收益不成比例。
 *
 * | 对象 id | 含义 |
 * | --- | --- |
 * | `SceneComponentPayloadRef::target` | 场景**宿主名**（`TargetId` 就是宿主名） |
 * | `SceneComponentPayloadRef::object` | 节点 id |
 * | `SceneComponentPayloadRef::component` | 稳定组件 id：`"<节点 id>\|<组件名>@<版本>"` |
 *
 * @remarks 写走 `IDomainOperationTarget` + staging（`ScriptComponentOperationTarget`）：编辑器
 *          的事务/撤销契约要求"能克隆出候选、能原子发布候选"，所以字段写回不能是一条直通的 setter。
 *          字段值落在引擎侧存储里，脚本侧下一次读就看见新值——不需要再通知脚本。
 * @remarks 字段的**类型**与**默认值**取自注册表里那份声明 schema（`scriptComponentDeclaredSchema`），
 *          不从当前值猜：否则脚本某次写入换个类型，inspector 的字段类型就会跟着漂。由此**可写 =
 *          脚本里给了非 null 的默认值**——那才是类型与默认值的来源；声明成 `null` 的字段与登记已丢的
 *          组件只读，但当前值照样显示（"有组件但没类型"看得见，而不是被静默藏掉）。
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

    /**
     * @brief 节点上每个脚本实体实例的**已登记**组件各一条；未登记组件不出现（§9.62.1）。
     * @return 每次调用返回一个**拥有**的 vector：调用方持有结果，提供方不保留指针。
     * @cost 与节点上挂着的脚本实例×其已登记组件数成正比，每次调用一次 vector 分配；编辑器只在
     *       选中/刷新 inspector 时调用，不在逐帧路径上。
     */
    std::vector<scene_editing::SceneComponentPayloadRef> components(
        const scene_editing::TargetId& scene, const scene_editing::ObjectId& object) const override;

    /**
     * @brief 选中组件的域操作目标（写回的唯一入口）。
     *
     * @return 借用指针，指向本提供方持有的那一个操作目标；它已按本次选中项绑定好身份。
     * @lifetime 与提供方同寿；但绑定内容会被下一次调用改写，所以过期的指针只会被**拒绝**
     *           （载荷身份对不上），不会写错组件。
     */
    eve::Result<scene_editing::IDomainOperationTarget*> payloadOperationTarget(
        const scene_editing::SelectionSnapshot& selection) const override;

    /**
     * @brief 引用还在不在：组件名+版本还登记着、且实例上仍挂着它。
     * @return 每次调用返回一个**拥有**的 vector：每条诊断说明一项为何失效（空 vector = 有效）。
     * @cost 与 1 + 该组件的祖先链长度 成正比（登记表一次哈希查找 + 实例槽位比对），每次调用
     *       一次 vector 分配；只在保存/校验路径上调用。
     */
    std::vector<scene_editing::EditorDiagnostic> validateComponent(
        const scene_editing::SceneComponentPayloadRef& component) const override;

    /** @brief 引擎侧存储的写版本号（任何一次写都会让它变），供 inspector 判断要不要刷新。 */
    [[nodiscard]] eve::Result<eve::Revision> currentRevision(
        const scene_editing::SelectionSnapshot& selection) const override;

    /** @brief 选中组件的字段表：类型与默认值来自声明 schema，当前值只在声明值为空时兜底。 */
    scene_editing::PropertySchema schema(const scene_editing::SelectionSnapshot& selection) const override;

    /** @brief 读一个字段：值来自引擎侧存储，不是脚本实例的槽位。 */
    scene_editing::PropertyReadResult read(const scene_editing::SelectionSnapshot& selection,
                                           const scene_editing::PropertyPath&      path) const override;

    /** @brief 计划一次字段写入（`Reset` 模式转给 `makeReset`）。 */
    eve::Result<scene_editing::DomainOperation> makeSet(const scene_editing::SelectionSnapshot& selection,
                                                        const scene_editing::PropertyPath&      path,
                                                        const scene_editing::EditorValue&       value,
                                                        scene_editing::PropertySetMode          mode) const override;

    /** @brief 计划一次"恢复声明默认值"；默认值来自注册表的声明 schema，不是当前值。 */
    eve::Result<scene_editing::DomainOperation> makeReset(const scene_editing::SelectionSnapshot& selection,
                                                          const scene_editing::PropertyPath&      path) const override;

private:
    /** 选中项解析出来的一个组件实例：编辑器目标 id、节点 id（编辑器侧）+ 引擎侧状态。 */
    struct Resolved {
        scene_editing::TargetId           target;
        std::string                       nodeId;
        eve::ScriptComponentInstanceState state;
    };

    /** 选中项解析成一个组件实例；解析不出来时返回失败（编辑器据此显示"引用过期"）。 */
    [[nodiscard]] eve::Result<Resolved> resolve(const scene_editing::SelectionSnapshot& selection) const;

    /** 一个组件实例的字段表（`schema` 与被写路径共用同一份描述符，避免两处判断漂移）。 */
    [[nodiscard]] scene_editing::PropertySchema schemaOf(const eve::ScriptComponentInstanceState& state) const;

    /** 把一次赋值封成可执行、可撤销的域操作（正向值与反向值都在里面）。 */
    [[nodiscard]] eve::Result<scene_editing::DomainOperation> planWrite(const Resolved&                    target,
                                                                        const scene_editing::PropertyPath& path,
                                                                        const scene_editing::EditorValue&  value) const;

    /** 目标 id → 宿主名（空 resolver 时就是目标 id 本身）。 */
    [[nodiscard]] std::string hostNameOf(const scene_editing::TargetId& target) const;

    HostResolver resolver_;
    /** 一个提供方只服务一个选中组件，所以只需要一个操作目标（每次解析时重新绑定身份）。 */
    mutable ScriptComponentOperationTarget operationTarget_;
};

}  // namespace eve::scene_editor
