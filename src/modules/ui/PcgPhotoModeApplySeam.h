#pragma once

/**
 * @file PcgPhotoModeApplySeam.h
 * @brief 让脚本成为 `IPhotoModeApplySink` 的提供方（决定③的第一个落地插座）。
 */

namespace eve::ui {

/**
 * @brief 声明"`IPhotoModeApplySink` 可以由脚本提供"这条接缝。
 *
 * @remarks 幂等：重复调用替换上一对回调（与 `declareScriptProjectileSeam` 同一条纪律）。
 *          由 `UI` 模块在构造时调用——消费者（`PcgPhotoModeApplyPlan::executeRegistered`）就在
 *          这个模块里，所以编组也归它。
 * @cost 一次 map 插入。只在模块构造时调用。
 */
void declareScriptPhotoModeApplySeam();

/**
 * @brief 撤回接缝与仍然在位的脚本适配器。
 * @remarks 由 `UI` 模块析构时调用。撤回分两半：先撤掉能力槽位里的适配器，再撤掉声明本身——
 *          留着声明只会宣传一个"绑上去也永远不会被调用"的插座（与武器投射物接缝同一条理由）。
 */
void withdrawScriptPhotoModeApplySeam();

}  // namespace eve::ui
