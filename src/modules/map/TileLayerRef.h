#pragma once

/** @file TileLayerRef.h @brief 一层地块的稳定身份（脚本与原生消费者都能解释）。 */

#include "common/Export.h"

#include <cstdint>

namespace eve::map {

/**
 * @brief 一层地块的稳定身份：ECS 槽位 + 换代号。
 *
 * 为什么不是指针：`ITileCollisionSink` 原先收 `const void* layer`，脚本既产生不出这个指针、
 * 也读不懂它（§9.71.1）。而"槽位 + 换代号"是仓库里既有的**过期句柄**范式（`common/Generation.h`
 * 用的是同一个形状）：同一个槽位被复用时会换代号，所以旧引用会被认出来，而不是悄悄写到新层上。
 *
 * @remarks 身份**不是名字**：`TileLayer` 没有名字（名字在编辑态的文档层模型上，运行期的地块层
 *          只有 ECS 身份）。脚本要认自己那一层，就拿 `eve.TileLayer.getEntityId()` 与这里的
 *          `slot` 比——两者是同一个数。
 */
struct EVENGINE_API TileLayerRef {
    /** @brief ECS 实体槽位（`ecs::Entity::id`）。 */
    std::uint32_t slot = 0;
    /** @brief 该槽位的换代号（`ecs::Entity::generation`）；槽位复用后会变。 */
    std::uint32_t generation = 0;

    friend bool operator==(const TileLayerRef&, const TileLayerRef&) noexcept = default;
};

}  // namespace eve::map
