#pragma once
namespace ssq {
class Table;
}
namespace eve::physics {
/** @brief Register owning Joint2D Result factories; borrows table during this call on the VM owner thread only. */
void exposeJoint2DBindings(ssq::Table& table);
}  // namespace eve::physics
