#pragma once

/**
 * @brief 宿主（eve / libmain）导出宏；插件从进程导入同一批符号。
 *
 * 分域 DLL 模式（`EVENGINE_MODULE_LINKAGE=SHARED`）下，导出面仍然是
 * `EVENGINE_API` 标注过的那些符号 —— 不启用 `WINDOWS_EXPORT_ALL_SYMBOLS`
 * （它会把 Poco/ImGui 的模板实例化也塞进 .def，撞 MSVC LNK1189 的 65535 上限；
 * 实测 EVFoundation 那一组 39 个模块就有 75,885 个导出）。因此需要区分
 * "在编引擎 DLL" 与 "在消费引擎 DLL"：
 *   - `EVENGINE_ENGINE_EXPORTS`：编进模块对象时定义，标注符号为 dllexport；
 *   - `EVENGINE_MODULE_DLL`：宿主 / 测试 / benchmark / 插件定义，为 dllimport。
 * 两者都未定义时保持原行为（宿主 dllexport、插件 dllimport），OBJECT 模式不受影响。
 */
#if defined(EVENGINE_ENGINE_EXPORTS) && defined(_WIN32)
#  define EVENGINE_API __declspec(dllexport)
#elif defined(EVENGINE_MODULE_DLL) && defined(_WIN32)
#  define EVENGINE_API __declspec(dllimport)
#elif defined(EVENGINE_PLUGIN)
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
