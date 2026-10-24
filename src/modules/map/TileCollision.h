#pragma once

#include "common/Export.h"
#include "map/TileLayerRef.h"

#include <cstddef>

namespace eve::map {

/** @brief One greedily merged solid tile rectangle in world coordinates. */
struct EVENGINE_API_WORLD_INLINE TileCollisionRect {
    float x      = 0.f;
    float y      = 0.f;
    float width  = 0.f;
    float height = 0.f;
};

/**
 * @brief Optional sink implemented by a physics or project adapter.
 *
 * @remarks **参数是身份，不是指针**（§9.71.1）：原先的 `const void* layer` 脚本既产生不出、
 *          也读不懂，所以收方拿不到"这是哪一层"这个事实。`TileLayerRef` 里的换代号让过期引用
 *          可以被认出来——实现必须**忽略**不属于自己的那一层，而不是把几何写到别处。
 * @remarks **失败由实现自己上报**：本方法是通知（"这一层的几何被整体替换成这些矩形"），不是
 *          事务——map 侧的 `collisionRects_` 才是权威，sink 失败不回滚任何东西。接口因此保持
 *          `void`，脚本实现走 `scriptTileCollisionSinkFailures()` 这个可观测计数，原生实现自己
 *          决定怎么报。
 */
class EVENGINE_API_WORLD_INLINE ITileCollisionSink {
public:
    static constexpr const char* capabilityName = "ITileCollisionSink";
    /** @brief I tile collision sink. */
    virtual ~ITileCollisionSink()               = default;

    /**
     * @brief Atomically replace collision geometry for a stable layer identity.
     * @param layer Layer this geometry belongs to; a stale `generation` must be ignored.
     * @param rects Borrowed rectangles, valid only for the duration of the call.
     * @param count Number of rectangles in `rects`.
     */
    virtual void replaceTileCollision(TileLayerRef layer, const TileCollisionRect* rects, size_t count) = 0;
};

}  // namespace eve::map
