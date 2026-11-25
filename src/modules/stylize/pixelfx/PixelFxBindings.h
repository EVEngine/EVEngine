#pragma once
namespace ssq {
class Table;
}
namespace eve::stylize {
/** @brief Register PixelFx in the synchronously borrowed VM table; no table reference is retained. */
void exposePixelFx(ssq::Table& table);
/** @brief Register owned source Act/session/frame adapters in the caller-owned VM table. */
void exposePixelFxAct(ssq::Table& table);
}  // namespace eve::stylize
