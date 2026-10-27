#pragma once
#include "sensing/EnvironmentQuery.h"

namespace eve::sensing {
/** @brief Owning request decoded at a script or tooling boundary. */
struct SensingQueryRequest {
    QueryOrigin origin;
    QuerySpec   spec;
};
/** @brief Decodes eve.sensing.query v1; rejects malformed known fields, ignores unknown fields.
 * @cost Linear in input bytes per admission; retain typed requests for repeated C++ queries.
 * @thread Caller thread; no callbacks or retained input storage.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<SensingQueryRequest> decodeSensingQuery(std::string_view json);
/** @brief Decodes eve.sensing.environmentDefinition v1 into an owning definition.
 * @cost Linear in input bytes per definition revision; compile and retain the resulting plan.
 * @thread Caller thread; custom native metrics are supplied through the typed C++ API.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<EnvironmentQueryDefinition> decodeEnvironmentDefinition(
    std::string_view json);
/** @brief Decodes eve.sensing.environmentRequest v1; locations retain explicit coordinate tags.
 * @cost Linear in contexts and items per execution; no references to input are retained.
 * @thread Caller thread; no callbacks.
 */
[[nodiscard]] EVENGINE_API_PLATFORM Result<EnvironmentRequest> decodeEnvironmentRequest(std::string_view json);
}  // namespace eve::sensing
