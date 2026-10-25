#pragma once
#include "common/Export.h"


#include <string>

namespace ssq {
class Object;
}

namespace eve::gpgpu {

class GpuBuffer;

/**
 * @brief Pack script ECS entities' component number fields into a GpuBuffer (AoS).
 *
 * @param entities Squirrel array of entity instances (as returned by `eve.view`).
 * @param slot Component slot name on the entity, e.g. `"pos"`.
 * @param fields Squirrel array of field name strings, e.g. `["x", "y"]`.
 * @param buf Destination buffer; must already be large enough for every packed row.
 * @return Number of entities packed (0 for an empty view or a non-array argument).
 * @throws `Exception` when the slot is missing, a field is absent or non-numeric, or the
 *         buffer is too small — a partially packed row is never reported as success.
 * @cost 与"实体数 × 字段数"成正比，一次 Squirrel 字段读加一次 buffer 写入；按帧调用时是逐帧
 *       成本，所以 GPU-resident 场景应优先用 `ShaderSystem` 的区间上传（`packEcsFloatsRange`），
 *       它只重传变化的那一段。
 * @remarks 字段来源是引擎侧那一个权威存储（§9.65 的 `readScriptComponentField`）：登记过的组件
 *          字段在存储里、没登记的在实例槽位里，这里不需要知道区别。行顺序必须与 `eve.view` 的
 *          稳定顺序（实体 id 升序）一致，因为回读按同一行号寻址。
 */
EVENGINE_API_WORLD int packScriptEntityFloats(ssq::Object entities, const std::string &slot, ssq::Object fields,
                                              GpuBuffer *buf);

/**
 * @brief Pack a contiguous entity range into the matching range of an existing buffer.
 * @param entities Squirrel array of entity instances.
 * @param slot Component slot name on the entity.
 * @param fields Squirrel array of field name strings.
 * @param buf Destination buffer.
 * @param firstEntity First entity in the stable ECS view.
 * @param entityCount Number of entities to pack; values past the view are clamped.
 * @return Number of entities packed.
 * @throws As `packScriptEntityFloats`.
 * @cost 与 `entityCount × 字段数` 成正比；这是 GPU-resident 的逐帧路径，只应传真正变化的那一段。
 */
EVENGINE_API_WORLD int packScriptEntityFloatsRange(ssq::Object entities, const std::string &slot, ssq::Object fields,
                                                   GpuBuffer *buf, int firstEntity, int entityCount);

/**
 * @brief Inverse of `packScriptEntityFloats`: write buffer rows back into component fields.
 * @param entities Squirrel array of entity instances.
 * @param slot Component slot name on the entity.
 * @param fields Squirrel array of field name strings.
 * @param buf Source buffer.
 * @param entityCount Number of entities to write back; should match the pack result.
 * @return Number of entities unpacked.
 * @throws As `packScriptEntityFloats`.
 * @cost 与 `entityCount × 字段数` 成正比，每个字段一次 Squirrel 写入；回读一帧后应当停止继续
 *       在 GPU 上迭代（见 `docs/usr/modules/gpgpu.md` 的 GPU-resident 说明）。
 */
EVENGINE_API_WORLD int unpackScriptEntityFloats(ssq::Object entities, const std::string &slot, ssq::Object fields,
                                                GpuBuffer *buf, int entityCount);

/**
 * @brief Unpack a contiguous buffer range into the matching stable ECS view range.
 * @param entities Squirrel array of entity instances.
 * @param slot Component slot name on the entity.
 * @param fields Squirrel array of field name strings.
 * @param buf Source buffer.
 * @param firstEntity First entity and first packed record to read.
 * @param entityCount Number of entities to unpack; values past the view are clamped.
 * @return Number of entities unpacked.
 * @throws As `packScriptEntityFloats`.
 * @cost 与 `entityCount × 字段数` 成正比；配合 `requestReadbackRange` 时只回读关心的区间。
 */
EVENGINE_API_WORLD int unpackScriptEntityFloatsRange(ssq::Object entities, const std::string &slot, ssq::Object fields,
                                                     GpuBuffer *buf, int firstEntity, int entityCount);

}  // namespace eve::gpgpu
