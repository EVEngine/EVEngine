#pragma once

/**
 * @file StringMapInstances.h
 * @brief Share string attribute map operations through the Windows Foundation DLL.
 *
 * Each container retains its owning value, allocator and iterator lifetimes.
 * Operations run synchronously on the calling thread with the standard container's
 * synchronization contract. No state or borrowed pointers are retained by this header.
 * All DLL participants must use the same CRT and iterator-debug configuration.
 */

#include "common/Export.h"

#include <string>
#include <unordered_map>

// MSVC's plain extern template does not suppress these inline operations under
// /Ob0. Only cross-group DLL consumers import the compiled Foundation instance.
// Foundation TUs and non-DLL builds keep the original standard-library bodies.
#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL) && !defined(EVENGINE_EXPORTS_FOUNDATION)
extern template class __declspec(dllimport) std::unordered_map<std::string, std::string>;
#endif
