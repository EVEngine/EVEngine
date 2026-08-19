#pragma once

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
class NetHost {
public:
    explicit NetHost(Network* net);
    ~NetHost();

    bool start(uint16_t port);
    void setLossRate(float rate);
    void setTimeoutMs(int ms);

    void onDatagram(const std::vector<char>& bytes, const std::string& from);
    void pump(int64_t nowMs);

    UdpLink* linkByPeerId(uint32_t peerId) const;
    size_t peerCount() const { return byId_.size(); }

    void sendTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                const void* data, size_t n);
    bool sendStringTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                      const std::string& s);

    using MessageHandler = std::function<void(uint32_t peerId, UdpLink::MsgType,
                                              uint8_t channel, const char*, size_t)>;
    using PeerHandler = std::function<void(uint32_t peerId)>;

    void setMessageHandler(MessageHandler h) { onMessage_ = std::move(h); }
    void setPeerConnectedHandler(PeerHandler h) { onConnect_ = std::move(h); }
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
