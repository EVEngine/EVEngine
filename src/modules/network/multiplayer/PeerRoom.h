#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace eve::network::multiplayer {

/** @brief Transport-local connection identity; zero is invalid. Never reuse within a room. */
struct PeerId {
    uint32_t value = 0;
    /** @brief Compare transport identities within the same room. */
    bool operator==(const PeerId&) const = default;
};

/** @brief Logical player identity on the wire, scoped to a caller-generated unique room nonce. */
struct PlayerId {
    uint64_t room  = 0;
    uint32_t value = 0;
    /** @brief Compare logical identities including their room nonce. */
    bool operator==(const PlayerId&) const = default;
};

/** @brief One-way room lifecycle; a closed room must be replaced, never restored. */
enum class RoomState : uint8_t { Idle, Joining, Lobby, Playing, Closed };
/** @brief Owned notification; messages include the authenticated sender and caller-supplied simulation tick. */
enum class RoomEventKind : uint8_t { Joined, RosterChanged, Started, Message, Closed, Rejected };
/** @brief Stable join rejection codes; None for all non-rejection events. */
enum class RoomRejectReason : uint8_t { None, Full, AlreadyPlaying };
/** @brief Immutable copied membership projection. Player 1 is the host. */
struct RoomMember {
    PlayerId id;
    bool     ready = false;
};
/** @brief Owned event, independent of room lifetime. Payload is game-defined binary data. */
struct RoomEvent {
    RoomEventKind          kind = RoomEventKind::Closed;
    PlayerId               sender;
    uint64_t               tick = 0;
    std::vector<std::byte> payload;
    RoomRejectReason       rejection = RoomRejectReason::None;
};
/** @brief Owned outbound packet; deliver once through a reliable ordered transport to peer. */
struct RoomPacket {
    PeerId                 peer;
    std::vector<std::byte> bytes;
};

/**
 * @brief Host-mediated P2P room: the host also plays; clients connect only to the host.
 * @ownership Owns membership, queues and protocol state, never gameplay entities or transports.
 * @thread All calls run on one owner thread; no callbacks, locks or internal clocks.
 * @lifetime Noncopyable/nonmovable. Returned events/packets/members own their data.
 * @note Host is the sole membership authority. No ECS, capability lookup, persistence,
 * automatic state replication, NAT traversal, authentication or host migration.
 * A transport must authenticate connection identity and preserve reliable packet order.
 * Nonces come from the caller's named RNG stream and must not repeat across sessions.
 * Protocol schema EVMP v1 rejects unknown versions/types, extra fields and malformed lengths.
 * Given identical ordered input and nonces, room transitions and output bytes are bit-exact.
 */
class EVENGINE_API_PLATFORM PeerRoom {
public:
    /** @brief Construct an idle room with bounded queues (256 entries, 4096 bytes per game message). */
    PeerRoom();
    /** @brief Release state only; call close and flush packets before destruction for graceful notification. */
    ~PeerRoom();
    PeerRoom(const PeerRoom&)            = delete;
    PeerRoom& operator=(const PeerRoom&) = delete;

    /** @brief Create a lobby. Nonzero unique nonce and capacity 2..16 required; host occupies one slot. */
    [[nodiscard]] Result<void> host(uint64_t roomNonce, uint8_t capacity);
    /** @brief Queue a join request to hostPeer. Nonzero unique requestNonce correlates the welcome. */
    [[nodiscard]] Result<void> join(PeerId hostPeer, uint64_t requestNonce);
    /** @brief Validate and atomically consume one packet. Failure leaves room and queues unchanged.
     * @param peer Transport-authenticated sender, never taken from payload.
     * @param bytes Borrowed only during this call; malformed/unauthorized/stale packets return diagnostics.
     * @cost Bounded by one payload and at most 16 recipients; drain queues every network frame.
     */
    [[nodiscard]] Result<void> receive(PeerId peer, std::span<const std::byte> bytes);
    /** @brief Change local readiness in the lobby. Clients request; host publishes authoritative roster. */
    [[nodiscard]] Result<void> setReady(bool ready);
    /** @brief Host-only transition when every member is ready and at least two players are present. */
    [[nodiscard]] Result<void> start();
    /** @brief Broadcast a game message while playing; host authenticates and relays client messages.
     * @param tick Caller-injected simulation tick, forwarded unchanged; game validates tick and payload semantics.
     * @param payload Borrowed during call, copied into owned packets; maximum 4096 bytes.
     * @cost Copies up to 4096 bytes per recipient (at most 16), amortized per submitted message.
     */
    [[nodiscard]] Result<void> send(uint64_t tick, std::span<const std::byte> payload);
    /** @brief Notify transport loss. Host removes peer; losing host closes client with an explicit event. */
    [[nodiscard]] Result<void> disconnected(PeerId peer);
    /** @brief Queue leave/close notification and close locally; transport flush is the caller's responsibility. */
    [[nodiscard]] Result<void> close();
    /** @brief Current lifecycle, constant-time and without side effects. */
    RoomState state() const;
    /** @brief Local logical identity, zero until welcome; invalidated by close. */
    PlayerId localPlayer() const;
    /** @brief Whether this instance owns the host authority. */
    bool isHost() const;
    /** @brief Copy up to 16 membership entries; no borrowing or gameplay state is exposed.
     * @cost One small allocation and O(players) copies; request when the roster changes.
     */
    std::vector<RoomMember> members() const;
    /** @brief Number of queued packets not yet handed to a transport; constant time. */
    size_t pendingPackets() const;
    /** @brief Move out the oldest event, or empty. Drain on every owner-thread update. */
    [[nodiscard]] std::optional<RoomEvent> pollEvent();
    /** @brief Move out the oldest packet, or empty. Delivery errors must call disconnected or close. */
    [[nodiscard]] std::optional<RoomPacket> pollPacket();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::network::multiplayer
