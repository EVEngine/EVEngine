#pragma once

/**
 * @brief 武器模块入口（eve.Weapon）：定义注册 / 实体工厂 / 挂点操作 / 帧调度。
 * 设计文档：docs/dev/通用载具系统设计.md
 */

#include "common/ECS.h"
#include "common/BorrowedRef.h"
#include "common/Module.h"
#include "definitions/Definitions.h"
#include "weapon/WeaponDefinitionRuntime.h"
#include "weapon/WeaponLogic.h"
#include "weapon/WeaponTypes.h"

#include <string>
#include <vector>

namespace eve::weapon {

/** @brief 武器模块（eve.Weapon）。 */
class Weapon : public Module {
public:
    Module_REG(Weapon);
    Weapon();
    ~Weapon() override;

    /** @brief 从 JSON 注册武器模板；返回成功注册数量。 */
    int registerWeaponsFromJson(const std::string& json);
    /** @brief 清空全部武器模板。 */
    void clearWeaponDefinitions();
    /** @brief 已注册武器模板数量。 */
    int getWeaponDefinitionCount();
    /** @brief 武器模板查询。 */
    bool        hasWeaponDefinition(const std::string& id);
    std::string getWeaponDefinitionLogic(const std::string& id);
    float       getWeaponDefinitionDamage(const std::string& id);
    float       getWeaponDefinitionRange(const std::string& id);

    /**
     * @brief Return the common registry backing legacy weapon definitions.
     * @return A borrowed owner-thread-affine registry used by
     *         WeaponDefinitionRuntime. The module owns it.
     * @ownership Borrowed; this module retains ownership.
     * @lifetime Valid until this Weapon module is destroyed.
     */
    [[nodiscard]] eve::definitions::DefinitionRegistry& definitionRegistry() noexcept {
        return definitionRegistry_;
    }

    /** @brief 注册武器逻辑（C++ 插件/游戏侧扩展点；同名替换）。 */
    static void registerLogic(IWeaponLogic* logic);
    /** @brief 已注册逻辑数量（内置 hitscan / projectile 起步）。 */
    static int getLogicCount();

    /**
     * @brief 工厂：对象由 ECS 表持有，脚本持有的是非拥有句柄。
     * @return Borrowed nullable ECS-owned weapon; null means validation or creation failed.
     * @ownership The ECS world owns the entity; callers must not delete it.
     * @lifetime Valid until entity/world destruction; retain its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no external callbacks; do not re-enter structural ECS mutation.
     */
    WeaponEntity* newWeapon(const std::string& defId);
    /**
     * @brief 创建一个挂点（炮塔/机枪座等）。
     * @return Borrowed nullable ECS-owned mount; null means creation failed.
     * @ownership The ECS world owns the entity; callers must not delete it.
     * @lifetime Valid until entity/world destruction; retain its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no external callbacks; do not re-enter structural ECS mutation.
     */
    WeaponMountEntity* newMount(const std::string& id, const std::string& type = "turret");
    /**
     * @brief 创建一个手持位（ARPG/RPG 角色手持武器）。
     * @return Borrowed nullable ECS-owned rig; null means creation failed.
     * @ownership The ECS world owns the entity; callers must not delete it.
     * @lifetime Valid until entity/world destruction; retain its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no external callbacks; do not re-enter structural ECS mutation.
     */
    WeaponRigEntity* newRig(const std::string& id, const std::string& wield = "right_hand");

    /** @brief 手持位操作：挂武器 / 取武器 / 设置持有位姿。 */
    bool          rigAttachWeapon(WeaponRigEntity* rig, WeaponEntity* w);
    WeaponEntity* rigGetWeapon(WeaponRigEntity* rig);
    void          rigSetPose(WeaponRigEntity* rig, float px, float py, float pz, float rx, float ry, float rz);

    /** @brief 共享弹药池操作：创建 / 补弹 / 查询 / 绑定武器 / 解绑 / 取绑定池。 */
    /**
     * @brief Creates an ECS-owned shared ammunition pool.
     * @return Borrowed nullable ECS-owned pool; null means creation failed.
     * @ownership The ECS world owns the pool; callers must not delete it.
     * @lifetime Valid until pool/world destruction; retain its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy Creation invokes no external callbacks; do not re-enter structural ECS mutation.
     */
    AmmoPoolEntity* newAmmoPool(const std::string& id, const std::string& ammoType, int max = -1);
    void            ammoPoolAdd(AmmoPoolEntity* pool, int n);
    int             ammoPoolGetCount(AmmoPoolEntity* pool);
    bool            bindAmmoPool(WeaponEntity* w, AmmoPoolEntity* pool);
    void            unbindAmmoPool(WeaponEntity* w);
    /**
     * @brief Returns the weapon's shared ammunition pool, or null when unbound.
     * @return Borrowed nullable ECS-owned pool.
     * @ownership The ECS world owns the pool; this query transfers no ownership.
     * @lifetime Valid until pool/world destruction or unbinding; use its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy The query invokes no callbacks and is invalid across ECS mutation.
     */
    AmmoPoolEntity* getAmmoPool(WeaponEntity* w);

    /** @brief 挂点操作：挂武器 / 取武器 / 限位 / 瞄准 / 击毁。 */
    bool          mountAttachWeapon(WeaponMountEntity* m, WeaponEntity* w);
    /**
     * @brief Returns the weapon mounted on a mount, or null when empty.
     * @return Borrowed nullable ECS-owned weapon.
     * @ownership The ECS world owns the weapon; this query transfers no ownership.
     * @lifetime Valid until weapon/world destruction or unmounting; use its generation handle across frames.
     * @thread Call on the owning ECS thread.
     * @reentrancy The query invokes no callbacks and is invalid across ECS mutation.
     */
    WeaponEntity* mountGetWeapon(WeaponMountEntity* m);
    void          mountSetLimits(WeaponMountEntity* m, float yawMin, float yawMax, float pitchMin, float pitchMax,
                                 float rotSpeed, float firingArc);
    /** @brief 按限位夹取目标角并写入挂载武器的瞄准目标（角度，度）。 */
    void mountAimAt(WeaponMountEntity* m, float yaw, float pitch);
    /** @brief 标记挂点被击毁（炮塔被打掉）；已毁挂点不再瞄准/开火。 */
    void mountDestroy(WeaponMountEntity* m);

    /** @brief 武器操作（脚本便捷入口，转发 WeaponSystem）。 */
    bool fire(WeaponEntity* w, const FireRequest& req);
    /** @brief 向目标点开火（自动生成 FireRequest）。 */
    bool fireAt(WeaponEntity* w, float x, float y, float z, int shooterId = 0);
    /** @brief 触发展开攻击（近战/法杖/导弹走阶段机；热武器等价 fire）。 */
    bool attack(WeaponEntity* w, float yaw, float pitch, int shooterId = 0);
    bool canFire(WeaponEntity* w);
    void startReload(WeaponEntity* w);
    void cancelReload(WeaponEntity* w);
    void setAim(WeaponEntity* w, float yaw, float pitch);

    /** @brief 运行时射击模式切换（safe/semi/auto）。 */
    bool setFireMode(WeaponEntity* w, const std::string& mode);
    /** @brief 当前射击模式名（"single"|"burst"|"auto"）。 */
    std::string getFireMode(WeaponEntity* w);
    /** @brief 可选射击模式数量 / 第 index 个模式名。 */
    int  getSelectableModeCount(WeaponEntity* w);
    std::string getSelectableMode(WeaponEntity* w, int index);

    /** @brief 开镜（ADS）：切换 aiming 状态并推 aim_in/aim_out 事件。 */
    bool setAiming(WeaponEntity* w, bool aiming);
    bool isAiming(WeaponEntity* w);
    /** @brief 开镜缩放 FOV（模板 zoomFov）。 */
    float getZoomFov(WeaponEntity* w);
    /** @brief 手感查询：当前散布 / 未回正后坐。 */
    float getSpread(WeaponEntity* w);
    float getRecoilPitch(WeaponEntity* w);
    float getRecoilYaw(WeaponEntity* w);

    /** @brief 每帧推进全部武器与挂点。 */
    void update(float dt);

    /** @brief 事件队列（fire/reload/empty，上一次 update 产生）。 */
    void        clearEvents();
    int         getEventCount() const;
    std::string getEventType(int index) const;
    std::string getEventWeaponId(int index) const;
    std::string getEventDefId(int index) const;
    std::string getEventMountId(int index) const;
    int         getEventAmmoLeft(int index) const;
    /** @brief 近战命中弧（仅 fire 事件）。 */
    float getEventArc(int index) const;
    /** @brief 法术/导弹爆炸半径（仅 fire 事件）。 */
    float getEventAoe(int index) const;
    /** @brief P0 手感/伤害事件载荷（仅 fire 事件）。 */
    float getEventSpread(int index) const;
    int   getEventPellets(int index) const;
    float getEventRecoilPitch(int index) const;
    float getEventRecoilYaw(int index) const;
    std::string getEventDamageType(int index) const;
    std::string getEventElement(int index) const;
    /** @brief 武器当前阶段（"idle"|"windup"|"active"|"recover"）。 */
    std::string getStage(WeaponEntity* w) const;
    /** @brief 武器当前资源值（弹药/法力/充能/体力）。 */
    float getResourceValue(WeaponEntity* w) const;

private:
    /**
     * @brief Finds one registered weapon definition for a synchronous operation.
     * @return Borrowed nullable definition owned by this module's registry.
     * @ownership Weapon owns the definition map; callers must not delete or mutate the result.
     * @lifetime Valid until definition registration/clear or module destruction; copy data before mutation.
     * @thread Call on the Weapon module's owning thread.
     * @reentrancy The lookup invokes no callbacks and is invalid across registry mutation.
     */
    /**
     * @brief Resolves one canonical weapon definition into an owning typed snapshot.
     * @return An independent value safe to move into an entity projection; failure describes
     *         an unknown or invalid registry definition.
     * @ownership The returned snapshot owns its fields; the registry owns canonical JSON.
     * @lifetime Independent of later lookups and registry mutation.
     */
    [[nodiscard]] eve::Result<WeaponDefinition> findDef(const std::string& id) const;

    eve::definitions::DefinitionRegistry              definitionRegistry_;
    std::vector<ecs::EntityHandle>                    weapons_;
    std::vector<ecs::EntityHandle>                    mounts_;
    std::vector<ecs::EntityHandle>                    rigs_;
    std::vector<ecs::EntityHandle>                    pools_;
    std::vector<WeaponEvent>                          events_;
    int                                               nextInstance_ = 1;
};

}  // namespace eve::weapon
