#pragma once
#include "common/Export.h"


#include "network/UdpLink.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::network {

class Network;
class UdpSocket;

/**
 * UDP host: one bound socket shared by all peers. Datagrams are routed by the
 * sender address string to a per-peer UdpLink; new addresses get a peer id and
 * emit peerconn. Link death (timeout) emits peerdisconn and removes the peer.
 * All state is main-thread; Network::pump calls onDatagram/pump.
 */
/** @brief EVENGINE_API_PLATFORM public API. */
class EVENGINE_API_PLATFORM NetHost {
public:
    /** @brief Net host. */
    explicit NetHost(Network* net);
    /** @brief Net host. */
    ~NetHost();

    /** @brief Starts . */
    bool start(uint16_t port);
    /** @brief Sets the loss rate. */
    void setLossRate(float rate);
    /** @brief Sets the timeout ms. */
    void setTimeoutMs(int ms);

    /** @brief On datagram. */
    void onDatagram(const std::vector<char>& bytes, const std::string& from);
    /** @brief Pump. */
    void pump(int64_t nowMs);

    /** @brief Link by peer id. */
    UdpLink* linkByPeerId(uint32_t peerId) const;
    /** @brief Peer count. */
    size_t peerCount() const { return byId_.size(); }

    /** @brief Send to. */
    void sendTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                const void* data, size_t n);
    /** @brief Send string to. */
    bool sendStringTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                      const std::string& s);

    using MessageHandler = std::function<void(uint32_t peerId, UdpLink::MsgType,
                                              uint8_t channel, const char*, size_t)>;
    using PeerHandler = std::function<void(uint32_t peerId)>;

    /** @brief Sets the message handler. */
    void setMessageHandler(MessageHandler h) { onMessage_ = std::move(h); }
    /** @brief Sets the peer connected handler. */
    void setPeerConnectedHandler(PeerHandler h) { onConnect_ = std::move(h); }
    /** @brief Sets the peer disconnected handler. */
    void setPeerDisconnectedHandler(PeerHandler h) { onDisconnect_ = std::move(h); }

private:
    void emitPeerConnected(uint32_t peerId);
    void emitPeerDisconnected(uint32_t peerId);

    Network* net_ = nullptr;
    UdpSocket* sock_ = nullptr;
    std::unordered_map<std::string, UdpLink*> byAddr_;
    std::unordered_map<uint32_t, UdpLink*> byId_;
    std::vector<UdpLink*> owned_;
    std::vector<uint32_t> pendingRemove_;
    uint32_t nextPeerId_ = 1;
    float lossRate_ = 0.f;
    int timeoutMs_ = 10000;
    MessageHandler onMessage_;
    PeerHandler onConnect_;
    PeerHandler onDisconnect_;
};

}  // namespace eve::network
