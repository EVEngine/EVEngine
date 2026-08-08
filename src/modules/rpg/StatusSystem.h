#pragma once

// 状态（buff/debuff）系统：把 EffectDefinition 施加/移除为具体 actor 身上的
// StatusInstance，并驱动持续时间 / 周期 tick。

#include "rpg/StatusTypes.h"

#include <string>
#include <vector>

namespace eve::rpg {

class RPGActor;

class StatusSystem {
public:
    /**
     * 施加一个效果。返回值：
     *   -1  — 失败（actor 为空 / 效果不存在 / stackPolicy=="none" 且已存在同效果）
     *    0  — durationPolicy=="instant"：已立即生效，无需跟踪的状态实例
     *   >0  — 新建或更新后的状态实例 id
     */
    static int apply(RPGActor *actor, const std::string &effectId, const std::string &source = "");

    /** 按实例 id 精确移除（撤销其属性修改器）；返回是否命中。 */
    static bool remove(RPGActor *actor, int instanceId);

    /** 移除该 actor 身上所有 effectId 匹配的实例；返回移除数量。 */
    static int removeByEffect(RPGActor *actor, const std::string &effectId);

    /** 移除该 actor 身上所有 source 匹配的实例；返回移除数量。 */
    static int removeBySource(RPGActor *actor, const std::string &source);

    /** 移除该 actor 身上所有 tag 匹配（效果定义带该 tag）的实例；返回移除数量。 */
    static int removeByTag(RPGActor *actor, const std::string &tag);

    static bool hasEffect(RPGActor *actor, const std::string &effectId);
    static int getActiveCount(RPGActor *actor);
    static std::string getActiveEffectId(RPGActor *actor, int index);
    static int getActiveStacks(RPGActor *actor, int index);
    static float getActiveRemaining(RPGActor *actor, int index);
    static int getActiveInstanceId(RPGActor *actor, int index);

    /** 遍历 RPGActor::liveActors()：倒计时长、到期移除、周期效果产生 tick 事件。 */
    static void update(float dt);

    /** 取出并清空自上次调用以来累积的周期 tick 事件（push/poll 风格，见 event 模块）。 */
    static void pollTicks(std::vector<StatusTickEvent> &out);
};

}  // namespace eve::rpg
