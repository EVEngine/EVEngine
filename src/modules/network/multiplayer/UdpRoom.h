#pragma once

#include <string_view>
#include "network/multiplayer/PeerRoom.h"

namespace eve::network {
class Network;
namespace multiplayer {

/**
 * @brief Direct-address UDP adapter for a host-mediated P2P room.
 * @ownership Owns room, host/link and sockets; borrows Network, which MUST outlive this adapter.
 * @thread Construct, operate and destroy on Network's owner thread. No user callbacks are installed.
 * @lifetime Destroy adapter before Network; internal callbacks are detached before socket release.
 * @note Uses existing reliable ordered UdpLink channel 7. This is direct-connect P2P,
 * not a mesh or rendezvous/relay service. Internet peers need a reachable host UDP port.
 */
class EVENGINE_API_PLATFORM UdpRoom {
public:
    /** @brief Create idle adapter, borrowing network for its entire lifetime. */
    explicit UdpRoom(Network& network);
    /** @brief Release transport without sending. Call room().close(), then pump until flushed first. */
    ~UdpRoom();
    UdpRoom(const UdpRoom&)            = delete;
    UdpRoom& operator=(const UdpRoom&) = delete;
    /** @brief Bind a nonzero UDP port and host a room; failures preserve idle state. */
    [[nodiscard]] Result<void> host(uint16_t port, uint64_t nonce, uint8_t capacity);
    /** @brief Connect to a direct address and queue handshake; joining completes asynchronously in pump.
     * @param address Borrowed during call and copied by socket/link; DNS resolution may block.
     * @param port Nonzero host UDP port.
     * @param nonce Unique caller-provided join nonce.
     */
    [[nodiscard]] Result<void> join(std::string_view address, uint16_t port, uint64_t nonce);
    /** @brief Pump Network and flush room packets. Inspect errors; Conflict requires draining room events.
     * @note Packet rejection is returned to caller; pending valid packets remain queued for next pump.
     * @cost Network polling plus bounded message copies; call once per network frame, not per entity.
     */
    [[nodiscard]] Result<void> pump();
    /** @brief Synchronous non-null borrow of the owned room; invalid after adapter destruction. */
    PeerRoom& room();
    /** @brief Queued plus unacknowledged sends. Pump to zero after close, or abandon on a caller deadline.
     * @note Zero means no outstanding transport work, not application-level acknowledgement.
     */
    size_t pendingSends() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace multiplayer
}  // namespace eve::network
