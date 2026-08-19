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

/** @brief Inverse of packScriptEntityFloats; entityCount should match pack result. */
int unpackScriptEntityFloats(ssq::Object entities, const std::string &slot,
                             ssq::Object fields, GpuBuffer *buf, int entityCount);

}  // namespace eve::gpgpu
