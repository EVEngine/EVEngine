#pragma once

/** @brief 宿主（eve / libmain）导出宏；插件从进程导入同一批符号。 */
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

/** @brief eve.plugins.load 要求的 C ABI 入口（成功返回 0）。 */
#if defined(_WIN32)
#  define EVE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define EVE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
