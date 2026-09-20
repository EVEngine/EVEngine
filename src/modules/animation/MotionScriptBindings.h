#pragma once

/**
 * @file MotionScriptBindings.h
 * @brief Squirrel GC wrappers for Motion builder / handle / sequence.
 */

namespace ssq {
class Table;
class Class;
}

namespace eve::animation {

/** @brief Register Motion / MotionSequence script types and Animation factories. */
void exposeMotionScriptBindings(ssq::Table &table, ssq::Class &animationClass);

/**
 * @brief 释放所有**脚本 motion sink** 适配器，并撤回它们在 provider 注册表里的登记。
 *
 * 由 `Animation` 模块析构调用。适配器是模块级的（motion 槽位借用 sink 指针，所以它们必须比任何
 * 一次 `advance()` 活得久），析构时一起撤，免得一个已经不在的模块还握着脚本对象。
 *
 * @cost 与绑定的 sink 数成正比；只在模块析构时调用一次。
 */
void clearScriptMotionSinkAdapters();

}  // namespace eve::animation
