#pragma once

// Host (eve / libmain) exports; plugins import the same symbols from the process.
#if defined(EVENGINE_PLUGIN)
#  if defined(_WIN32)
#    define EVENGINE_API __declspec(dllimport)
#  else
#    define EVENGINE_API
#  endif
#elif defined(_WIN32)
#  define EVENGINE_API __declspec(dllexport)
#else
#  if defined(__GNUC__) || defined(__clang__)
#    define EVENGINE_API __attribute__((visibility("default")))
#  else
#    define EVENGINE_API
#  endif
#endif

// C ABI entry required by eve.plugins.load (return 0 on success).
#if defined(_WIN32)
#  define EVE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define EVE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
