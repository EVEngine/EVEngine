#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace eve::network {

class Network;
class UdpSocket;

/**
 * Point-to-point reliable/unreliable UDP link.
 *
 * Owns no socket: it drives an externally-owned UdpSocket (client: connect();
 * server: bind() + setRemote(addr) per link). All link state is advanced on the
 * main thread from Network::pump(): inbound datagrams are routed here and
 * timers (retransmit / heartbeat / fragment expiry) are driven from pump().
 *
 * Protocol (little-endian):
 *   [0]u8 'E' [1]u8 'V' [2]u8 ver=1 [3]u8 type [4]u8 channel [5]u8 flags
 *   [6]u32 seq [10]u32 ack [14]u32 ackBits
 *   optional frag header (flags&1): u32 msgId u16 fragCount u16 fragIndex
 *   payload follows.
 */
class UdpLink {
public:
    enum class MsgType : uint8_t {
        Reliable = 0,
        Unreliable = 1,
        UnreliableOrdered = 2,
    };

    using MessageHandler =
        std::function<void(MsgType, uint8_t channel, const char* data, size_t n)>;
    using DisconnectHandler = std::function<void(const std::string& reason)>;

    UdpLink(Network* net, UdpSocket* sock);
    ~UdpLink();

    // --- transport setup --------------------------------------------------
    bool setRemote(std::string host, uint16_t port);
    /** Accept a peer address string exactly as reported by the socket ("host:port"). */
    bool setRemoteString(const std::string& addr);
    std::string peer() const { return remote_; }

    // --- sending ----------------------------------------------------------
    void send(MsgType type, uint8_t channel, const void* data, size_t n);
    bool sendString(MsgType type, uint8_t channel, const std::string& s);

    // --- main-thread pump hooks (called by Network::pump) -----------------
    void onDatagram(const std::vector<char>& bytes, const std::string& from);
    void pump(int64_t nowMs);

    // --- callbacks & config ----------------------------------------------
    void setMessageHandler(MessageHandler h) { onMessage_ = std::move(h); }
    void setDisconnectHandler(DisconnectHandler h) { onDisconnect_ = std::move(h); }
    void setLossRate(float rate);  // 0..1, test hook: drop outbound datagrams
    void setTimeoutMs(int ms) { timeoutMs_ = ms; }

    bool isAlive() const { return alive_; }
    uint32_t peerId() const { return peerId_; }
    void setPeerId(uint32_t id) { peerId_ = id; }
    size_t pendingReliable() const { return sendQueue_.size(); }
    size_t pendingFragments() const { return fragments_.size(); }

    // Max payload per UDP datagram (below typical MTU to avoid IP fragmentation).
    static constexpr size_t kPayloadMTU = 1200;
    static constexpr size_t kMaxMessage = 256 * 1024;

private:
    void sendDatagram(std::vector<char> pkt);
    void sendAckNow();
    void pruneAcked(uint32_t ack, uint32_t bits);
    void deliver(MsgType type, uint8_t channel, std::vector<char> payload);
    void handleData(uint8_t type, uint8_t channel, uint32_t seq,
                    const std::vector<char>& payload);
    void noteReceived();

    Network* net_ = nullptr;
    UdpSocket* sock_ = nullptr;
    std::string remote_;
    std::string remoteHost_;
    uint16_t remotePort_ = 0;
    bool remoteSet_ = false;

    // sender
    uint32_t nextSeq_ = 0;
    struct SendEntry {
        std::vector<std::vector<char>> pkts;  // one per datagram (fragments share a seq)
        int64_t deadlineMs = 0;
        int attempts = 0;
    };
    std::map<uint32_t, SendEntry> sendQueue_;

    // receiver
    uint32_t expectedReliable_ = 0;
    std::map<uint32_t, std::vector<char>> outOfOrder_;
    uint32_t expectedUnrelOrd_ = 0;
    std::map<uint32_t, std::vector<char>> unrelOrdBuf_;
    // Encoding "last contiguous received seq" = expectedReliable_ - 1, so the
    // "nothing received yet" value wraps to 0xFFFFFFFF (never prune seq 0).
    uint32_t ackSend_ = 0xFFFFFFFFu;
    uint32_t ackBitsSend_ = 0;

    // fragmentation
    struct FragBuf {
        uint32_t total = 0;
        uint32_t received = 0;
        size_t lastLen = 0;
        std::vector<char> data;
        std::vector<uint8_t> seen;
        int64_t lastMs = 0;
    };
    std::map<uint32_t, FragBuf> fragments_;
    uint32_t nextFragId_ = 0;

    // liveness / config
    int64_t lastRecvMs_ = 0;
    int64_t lastSendMs_ = 0;
    int64_t lastPingMs_ = 0;
    int64_t lastAckMs_ = 0;
    int timeoutMs_ = 10000;
    int retryBaseMs_ = 100;
    int maxAttempts_ = 10;
    float lossRate_ = 0.f;
    std::mt19937 rng_{0xE9E1};
    bool alive_ = false;
    bool disconnectNotified_ = false;

    uint32_t peerId_ = 0;
    MessageHandler onMessage_;
    DisconnectHandler onDisconnect_;
};

}  // namespace eve::network
