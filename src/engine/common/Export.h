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
#define EVENGINE_API __declspec(dllexport)
#elif defined(EVENGINE_MODULE_DLL) && defined(_WIN32)
#define EVENGINE_API __declspec(dllimport)
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

/**
 * @brief 每个链接组（link group）各自的导出宏。
 *
 * SHARED 模式下一个链接组就是一个 DLL（`EVE_LINK_GROUP_TABLE`，见
 * cmake/module_manifest.cmake）：`EVFoundation` / `EVPlatform` / `EVBackends` /
 * `EVWorld` / `EVDomains` / `EVOrchestration` / `EVEditors`。编进 A 组的模块，
 * 必须把 B 组标注过的符号看成 **导入**，而不是再导出一次 —— 在 A 的目标文件里
 * 声明 dllexport，等于让 A 声称自己定义了实际位于 B.dll 的符号。
 *
 * 这对函数只是浪费一个跳转（链接器能用 thunk 兜住），对 **数据符号** 却是致命的：
 * MSVC 的导入库为导入的数据只提供 `__imp_?sym`，而"再导出"的那一侧引用的是
 * 裸符号 `?sym`，没有任何一个组定义它，于是链接期报 LNK2001。实测正是
 * `common/Module.h` 里 `Module_REG` 声明的三个 `static const char* <Module>::name`
 * 数据成员（EVPlatform.dll 的跨组依赖）。
 *
 * 因此每个组一个宏。属于 G 组的翻译单元定义 `EVENGINE_EXPORTS_<G>`
 * （`create_module()` 以 PRIVATE 方式加入），G 组自己的声明是 dllexport；
 * 其它组、宿主、测试、benchmark、插件都不定义任何 `EVENGINE_EXPORTS_<G>`，
 * 看到的是 dllimport。非 SHARED 模式下没有任何目标定义 `EVENGINE_MODULE_DLL`，
 * 所有组宏退化为 `EVENGINE_API`，OBJECT 模式的预处理器输出逐字节不变。
 */
#if defined(EVENGINE_MODULE_DLL) || defined(EVENGINE_PLUGIN)
#if defined(_WIN32)
#define EVENGINE_GROUP_EXPORT __declspec(dllexport)
#define EVENGINE_GROUP_IMPORT __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
// ELF/Mach-O 没有 dllimport：两侧都用默认可见性，符号由真正拥有它的那个组导出。
#define EVENGINE_GROUP_EXPORT __attribute__((visibility("default")))
#define EVENGINE_GROUP_IMPORT __attribute__((visibility("default")))
#else
#define EVENGINE_GROUP_EXPORT
#define EVENGINE_GROUP_IMPORT
#endif

#if defined(EVENGINE_EXPORTS_FOUNDATION)
#define EVENGINE_API_FOUNDATION EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_FOUNDATION EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_PLATFORM)
#define EVENGINE_API_PLATFORM EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_PLATFORM EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_BACKENDS)
#define EVENGINE_API_BACKENDS EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_BACKENDS EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_WORLD)
#define EVENGINE_API_WORLD EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_WORLD EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_DOMAINS)
#define EVENGINE_API_DOMAINS EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_DOMAINS EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_ORCHESTRATION)
#define EVENGINE_API_ORCHESTRATION EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_ORCHESTRATION EVENGINE_GROUP_IMPORT
#endif
#if defined(EVENGINE_EXPORTS_EDITORS)
#define EVENGINE_API_EDITORS EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_EDITORS EVENGINE_GROUP_IMPORT
#endif
#else
// OBJECT / 静态模式：不分区，每个组宏都与原来的 EVENGINE_API 完全一致。
#define EVENGINE_API_FOUNDATION    EVENGINE_API
#define EVENGINE_API_PLATFORM      EVENGINE_API
#define EVENGINE_API_BACKENDS      EVENGINE_API
#define EVENGINE_API_WORLD         EVENGINE_API
#define EVENGINE_API_DOMAINS       EVENGINE_API
#define EVENGINE_API_ORCHESTRATION EVENGINE_API
#define EVENGINE_API_EDITORS       EVENGINE_API
#endif

/**
 * @brief 仅供"只有内联成员"的类型使用的组宏：拥有者 dllexport，消费方不导入。
 *
 * 一个标注类型如果在任何 .cpp 里都没有类外成员定义，它在 DLL 边界上就没有可导入
 * 的符号：vtable、隐式特殊成员（构造/析构/拷贝）、`static constexpr` 成员都由每个
 * 使用它的翻译单元按 vague linkage 就地生成。若消费方把它标成 dllimport，MSVC 会
 * 生成 `__imp_` 引用，而拥有者那一组因为没有任何翻译单元 ODR-use 这些成员，
 * 根本不会把它们放进导出表 —— 链接期就是 LNK2019。实测：只有 EVPlatform 组的
 * `scene/SceneCapabilities.cpp` 用到的 `ProcgenInstanceDesc` / `IProcgenSceneSink`
 * （common/ProcgenSceneSink.h）与 `IWindowSurfaceHost`（common/WindowSurfaceHost.h）
 * 属于这种情形：它们没有任何 .cpp 定义，EVFoundation 里也没人实例化。
 *
 * 所以这一族宏在消费方展开为空（不导入，各自保留内联副本），只在拥有者那一组
 * 展开为 dllexport（导出面与今天完全一致）；OBJECT 模式下同样退化为 EVENGINE_API。
 * 判据是"该类有没有类外成员定义"，不是"它是不是接口"：像 `Module` / `ModuleManager`
 * 这种有 .cpp 定义的类（vtable 与静态数据成员是数据符号，导入库只提供
 * `__imp_<data>`）必须继续用 `EVENGINE_API_<GROUP>` 走 dllimport。
 */
#if defined(EVENGINE_MODULE_DLL) || defined(EVENGINE_PLUGIN)
#if defined(EVENGINE_EXPORTS_FOUNDATION)
#define EVENGINE_API_FOUNDATION_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_FOUNDATION_INLINE
#endif
#if defined(EVENGINE_EXPORTS_PLATFORM)
#define EVENGINE_API_PLATFORM_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_PLATFORM_INLINE
#endif
#if defined(EVENGINE_EXPORTS_BACKENDS)
#define EVENGINE_API_BACKENDS_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_BACKENDS_INLINE
#endif
#if defined(EVENGINE_EXPORTS_WORLD)
#define EVENGINE_API_WORLD_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_WORLD_INLINE
#endif
#if defined(EVENGINE_EXPORTS_DOMAINS)
#define EVENGINE_API_DOMAINS_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_DOMAINS_INLINE
#endif
#if defined(EVENGINE_EXPORTS_ORCHESTRATION)
#define EVENGINE_API_ORCHESTRATION_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_ORCHESTRATION_INLINE
#endif
#if defined(EVENGINE_EXPORTS_EDITORS)
#define EVENGINE_API_EDITORS_INLINE EVENGINE_GROUP_EXPORT
#else
#define EVENGINE_API_EDITORS_INLINE
#endif
#else
#define EVENGINE_API_FOUNDATION_INLINE    EVENGINE_API
#define EVENGINE_API_PLATFORM_INLINE      EVENGINE_API
#define EVENGINE_API_BACKENDS_INLINE      EVENGINE_API
#define EVENGINE_API_WORLD_INLINE         EVENGINE_API
#define EVENGINE_API_DOMAINS_INLINE       EVENGINE_API
#define EVENGINE_API_ORCHESTRATION_INLINE EVENGINE_API
#define EVENGINE_API_EDITORS_INLINE       EVENGINE_API
#endif

/** @brief eve.plugins.load 要求的 C ABI 入口（成功返回 0）。 */
#if defined(_WIN32)
#  define EVE_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#  define EVE_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif
