#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <string>
#include <string_view>

namespace eve::schema {

/**
 * @brief Runtime Protobuf schema compiled from a FileDescriptorSet.
 * @thread All methods run on the schema registry owner thread.
 * @reentrancy No method invokes scripts or external callbacks.
 */
class EVENGINE_API_FOUNDATION ProtobufSchema {
public:
    /**
     * @brief Register one exact schema and message type from serialized FileDescriptorSet bytes.
     * @return Conflict for an occupied id/version; no registration occurs on descriptor errors.
     * @thread Register on the schema registry thread before concurrent serialization.
     * @cost Builds a descriptor pool once per schema version; do not call per frame.
     */
    [[nodiscard]] static Result<void> registerDescriptorSet(std::string id, int version, std::string messageName,
                                                            std::string_view descriptorSet);

    /** @brief Encode strict Protobuf JSON using the registered message descriptor.
     * @cost O(message size) at a save/load boundary.
     */
    [[nodiscard]] static Result<std::string> encodeJson(std::string_view id, int version, std::string_view json);

    /** @brief Project a binary Protobuf message to JSON using its registered descriptor.
     * @cost O(message size) at a save/load boundary.
     */
    [[nodiscard]] static Result<std::string> decodeJson(std::string_view id, int version, std::string_view wire);

    /** @brief Whether the exact id/version is registered as a Protobuf descriptor. */
    [[nodiscard]] static bool contains(std::string_view id, int version);

    /** @brief Remove all Protobuf descriptor registrations. */
    static void clear();
};

}  // namespace eve::schema
