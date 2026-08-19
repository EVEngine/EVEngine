#pragma once

/**
 * @file Assert.h
 * @brief EVEngine assertion entry point, backed by zeroerr.
 *
 * All parameter validation and internal invariant checks in the engine should
 * go through the macros defined here (or use zeroerr's ASSERT family directly).
 *
 * Build policy:
 *  - Debug builds: checks are compiled in and always active.
 *  - Release / other configurations: checks are compiled out by default
 *    (CMake defines ZEROERR_NO_ASSERT), so there is zero runtime cost.
 *  - To force checks on in non-Debug builds, configure with
 *    `-DEVENGINE_ENABLE_ASSERTS=ON`
 *    (or `make ... CMAKE_EXTRA_ARGS=-DEVENGINE_ENABLE_ASSERTS=ON`).
 *
 * A failed check throws zeroerr::AssertionData (a std::exception) carrying the
 * file, line, decomposed expression and an optional message.
 */

#include "zeroerr/assert.h"

/**
 * @brief Validate a function parameter / public API precondition.
 * @param cond The boolean precondition that must hold.
 * @param ...  Optional message printed when the check fails
 *             (plain text, printf-style pattern supported by zeroerr::format).
 */
#define EV_PARAM_CHECK(cond, ...) ASSERT(cond, ##__VA_ARGS__)

/**
 * @brief Assert an internal engine invariant (state that must always hold).
 * @param cond The boolean invariant that must hold.
 * @param ...  Optional message printed when the check fails.
 */
#define EV_ASSERT(cond, ...) ASSERT(cond, ##__VA_ARGS__)

