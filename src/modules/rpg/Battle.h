#pragma once

/**
 * @file Battle.h
 * @brief 回合制战斗对象：参战者、行动顺序、目标选取、行动执行。
 *
 * 一场战斗是一份参与者列表（分 阵营 party/enemies）。每回合：游戏为每名
 * 参战者设置行动（setAction），`startRound` 按先攻值排序，`executeNextAction`
 * 逐个执行（命中→伤害/治疗/施加效果）。敌人行动可用 `autoEnemyActions` 自动选取。
 * 结果写入 Vitals/Status，并通过本对象事件队列反馈。
 */

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

/** @brief 阵营是任意整数 id（0/1 仅为常见约定的便捷常量，可自定义更多阵营）。 */
namespace BattleSide {
inline constexpr int Party = 0;
inline constexpr int Enemies = 1;
}

/** @brief 一次战斗反馈事件。 */
struct BattleEvent {
    /** @brief roundStart | actionStart | damage | heal | miss | effect | victory | defeat */
    std::string action;
    std::string skillId;
    class RPGActor *caster = nullptr;
    class RPGActor *target = nullptr;
    double amount = 0.0;
    bool crit = false;
};

/**
 * @brief 一场回合制战斗（任意阵营数）。
 * @ownership 由调用方创建/销毁；Battle 不持有参与者的所有权（参与者为借用 ECS actor）。
 * @thread 在单一模拟线程上驱动整场战斗。
 */
class Battle {
public:
    Battle() = default;
    ~Battle() = default;

    Battle(const Battle &) = delete;
    Battle &operator=(const Battle &) = delete;

    /** @brief 加入参战者；side 为任意阵营 id。 */
    void addActor(RPGActor *actor, int side);
    /** @brief 为参战者设置本回合行动；target 为空由目标规则自动解析。 */
    void setAction(RPGActor *actor, const std::string &skillId, RPGActor *target = nullptr);
    /** @brief 未设行动的 AI 侧自动选一个随机已学技能（或普攻）打随机存活敌对目标。 */
    void autoEnemyActions();
    /** @brief 结算所有已设行动：先攻排序、清空待行动，回合数 +1。 */
    void startRound();
    /** @brief 执行下一条行动（compatibility facade (脚本兼容门面)）；返回是否执行了一条。 */
    bool executeNextAction();
    /** @brief 战斗是否已结束（仅剩一方存活 / 双方全灭）。 */
    bool isFinished() const;
    /** @brief 是否胜利：获胜方 == 玩家侧。 */
    bool isVictory() const;
    /** @brief 是否战败：已结束且获胜方 != 玩家侧。 */
    bool isDefeat() const;
    /** @brief 玩家阵营 id（isVictory/isDefeat 依据），默认 0。 */
    void setPlayerSide(int side);
    int getPlayerSide() const;
    /** @brief 获胜阵营 id；未结束或平局（双方全灭）返回 -1。 */
    int getWinnerSide() const;
    /** @brief 当前回合数（1 起）。 */
    int getTurn() const;
    bool isActorAlive(RPGActor *actor) const;

    int getActorCount() const;
    /**
     * @brief Return a participant actor by index, or null when out of range.
     * @return Borrowed nullable ECS actor; the battle does not own it.
     * @ownership The ECS world owns the actor; callers must not delete it.
     * @lifetime Valid until the actor or battle is destroyed; do not retain across rounds.
     */
    RPGActor *getActor(int index) const;
    int getSide(int index) const;

    int getEventCount() const;
    BattleEvent getEvent(int index) const;
    void pollEvents();
    // 事件字段访问（脚本向）
    std::string getEventAction(int index) const;
    std::string getEventSkillId(int index) const;
    /**
     * @brief Return the caster of a polled event, or null when out of range.
     * @return Borrowed nullable ECS actor; the event cache does not own it.
     * @ownership The ECS world owns the actor; callers must not delete it.
     * @lifetime Valid until the actor is destroyed; do not retain beyond the poll.
     */
    RPGActor *getEventCaster(int index) const;
    /**
     * @brief Return the target of a polled event, or null when out of range.
     * @return Borrowed nullable ECS actor; the event cache does not own it.
     * @ownership The ECS world owns the actor; callers must not delete it.
     * @lifetime Valid until the actor is destroyed; do not retain beyond the poll.
     */
    RPGActor *getEventTarget(int index) const;
    double getEventAmount(int index) const;
    bool getEventCrit(int index) const;

private:
    struct Participant {
        RPGActor *actor = nullptr;
        int side = 0;
    };
    struct PendingAction {
        RPGActor *actor = nullptr;
        std::string skillId;
        RPGActor *target = nullptr;
        double initiative = 0.0;
    };

    bool isDead(const Participant &p) const;
    std::vector<RPGActor *> livingOnSide(int side) const;
    int sideOf(RPGActor *actor) const;
    /**
     * @brief Pick a random living opponent for a side (borrowed actor).
     * @ownership The ECS world owns the returned actor; callers must not delete it.
     * @lifetime Valid until the actor is destroyed; do not retain across rounds.
     */
    RPGActor *randomOpponent(int mySide);
    int computeWinnerSide() const;
    void execute(PendingAction &pa, unsigned &seedCounter);

    std::vector<Participant> participants_;
    std::vector<PendingAction> queue_;
    std::vector<PendingAction> roundActions_;
    std::vector<BattleEvent> events_;
    std::vector<BattleEvent> polled_;
    int turn_ = 0;
    bool started_ = false;
    bool finished_ = false;
    int winner_ = -1;
    int playerSide_ = BattleSide::Party;
    unsigned seedCounter_ = 1;
};

}  // namespace eve::rpg