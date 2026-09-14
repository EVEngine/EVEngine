#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "common/Result.h"

namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::graphics {
/** @brief Register shader loading and binary replacement on the VM owner thread.
 * Borrows both tables for this call only; invokes no script callbacks.
 */
void exposeShaderScriptBindings(ssq::Table& table, ssq::Class& cls);
namespace detail {
Result<std::vector<std::uint32_t>> readShaderStageFile(const std::string& path);
void                               exposeShaderResourceBindings(ssq::Table& table, ssq::Class& cls);
}  // namespace detail
}  // namespace eve::graphics
