#pragma once

namespace ssq {
class Table;
}
namespace eve::network::multiplayer {
/** @brief Register eve.PeerRoom on the VM owner thread; VM owns adapters, Network must outlive them. */
void exposePeerRoomBindings(ssq::Table& table);
}  // namespace eve::network::multiplayer
