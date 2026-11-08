#pragma once
#include <string_view>
#include "stylize/PixelFx.h"
namespace eve::stylize::pixelfx {
/** @brief Borrow process-lifetime compiled source Act data; no Act maps to an empty object.
 * @note Cold preparation only. Kind identity is owned by the existing effect descriptors. */
std::string_view actSourceData(PixelFxKind kind);
}  // namespace eve::stylize::pixelfx
