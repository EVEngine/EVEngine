#pragma once

#include <string>

namespace ssq {
class Object;
}

namespace eve::gpgpu {

class GpuBuffer;

/**
 * @brief Pack script ECS entities' component number fields into a GpuBuffer (AoS).
 * entities: Squirrel array of entity instances
 * slot: component slot name on the entity (e.g. "pos")
 * fields: Squirrel array of field name strings (e.g. ["x","y"])
 * Returns entity count packed (0 on empty / error).
 */
int packScriptEntityFloats(ssq::Object entities, const std::string &slot,
                           ssq::Object fields, GpuBuffer *buf);

/**
 * @brief Pack a contiguous entity range into the matching range of an existing buffer.
 * @param firstEntity First entity in the stable ECS view.
 * @param entityCount Number of entities to pack; values past the view are clamped.
 * @return Number of entities packed.
 */
int packScriptEntityFloatsRange(ssq::Object entities, const std::string &slot, ssq::Object fields, GpuBuffer *buf,
                                int firstEntity, int entityCount);

/** @brief Inverse of packScriptEntityFloats; entityCount should match pack result. */
int unpackScriptEntityFloats(ssq::Object entities, const std::string &slot,
                             ssq::Object fields, GpuBuffer *buf, int entityCount);

/**
 * @brief Unpack a contiguous buffer range into the matching stable ECS view range.
 * @param firstEntity First entity and first packed record to read.
 * @param entityCount Number of entities to unpack; values past the view are clamped.
 * @return Number of entities unpacked.
 */
int unpackScriptEntityFloatsRange(ssq::Object entities, const std::string &slot, ssq::Object fields, GpuBuffer *buf,
                                  int firstEntity, int entityCount);

}  // namespace eve::gpgpu
