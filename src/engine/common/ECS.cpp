#include "common/ECS.h"
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve {

void exposeECS(ssq::Table& table) {
    // 预留：将 ECS.hpp 的实体/组件模型暴露给 Squirrel。
    // 游戏热路径应通过声明实体与改组件完成，而不是每帧调用绘制 API。
    table.addFunc("ecsReady", []() { return true; });
}

}  // namespace eve
