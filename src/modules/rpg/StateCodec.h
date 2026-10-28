#pragma once

#include <string>

namespace eve::rpg {
class RPGActor;
class Tracker;

/** @brief RPG 运行时 JSON 快照；定义数据仍由内容包注册，不重复写入存档。 */
class StateCodec {
public:
    /** @brief 将角色运行时序列化为版本化 JSON。 */
    static std::string serializeActor(RPGActor* actor);
    /** @brief 从 JSON 覆盖角色运行时；静态定义须事先注册。 */
    static bool deserializeActor(RPGActor* actor, const std::string& json, std::string* error = nullptr);
    /** @brief 将任务追踪器序列化为版本化 JSON。 */
    static std::string serializeTracker(const Tracker* tracker);
    /** @brief 从 JSON 覆盖任务追踪器状态。 */
    static bool deserializeTracker(Tracker* tracker, const std::string& json, std::string* error = nullptr);
};
}  // namespace eve::rpg
