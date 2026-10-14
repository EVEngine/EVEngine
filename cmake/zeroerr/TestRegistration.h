#pragma once

/** @file TestRegistration.h
 * @brief Internal registration bridge for engine-owned zeroerr test macros.
 */

#include <zeroerr/unittest.h>
#include <initializer_list>
#include <string_view>

#if defined(ZEROERR_COMPILED_DIAGNOSTICS)
namespace zeroerr::detail {
/**
 * @brief Construct registration metadata without a caller-side decorator vector.
 * @param name Borrowed test name. @param file Borrowed source filename. @param line Source line.
 * @param function Borrowed static test function; not invoked during construction.
 * @param decorators Borrowed pointer list, copied synchronously before returning.
 * @return Metadata owning its strings, callable wrapper and decorator pointer vector.
 * @lifetime Decorator objects must outlive all copies of the returned metadata.
 * @note Runs on the registration thread, retains no borrowed text or list storage and invokes no callbacks.
 * @cost Allocates metadata per registration, amortized over test execution.
 */
[[nodiscard]] TestCase makeTestCaseFromDecorators(std::string_view name, std::string_view file, unsigned line,
                                                  void (*function)(TestContext*),
                                                  std::initializer_list<Decorator*> decorators);
}  // namespace zeroerr::detail
#endif
