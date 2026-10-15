#pragma once

namespace ssq {
class Table;
}
namespace eve::fluids {
/** @brief Register owning ocean script types on the VM owner thread; table is borrowed only during registration. */
void exposeOceanTypes(ssq::Table& table);
/** @brief Register owning wake/boat adapters; VM owner thread, call-only table, no runtime objects retained. */
void exposeOceanInteractionTypes(ssq::Table& table);
/** @brief Register owning render projections on the VM/render thread; table is borrowed only during registration. */
void exposeOceanRenderTypes(ssq::Table& table);
/** @brief Register owning spray simulation/render types; owner-thread, call-only table, no retained inputs. */
void exposeOceanSprayTypes(ssq::Table& table);
}  // namespace eve::fluids
