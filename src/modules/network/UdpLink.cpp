#include "network/UdpLink.h"
#include "network/Network.h"
#include "network/UdpSocket.h"
#include "data/ByteData.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace eve::network {

namespace {

constexpr uint8_t kMagic0 = 'E';
constexpr uint8_t kMagic1 = 'V';
constexpr uint8_t kVersion = 1;
constexpr size_t kHeaderLen = 18;
constexpr size_t kMaxOutOfOrder = 2048;
constexpr size_t kMaxFragments = 64;
constexpr size_t kMaxFragCount = 2048;
constexpr int64_t kFragExpireMs = 5000;
constexpr int64_t kPingIntervalMs = 500;
constexpr uint8_t kFragFlag = 1;

enum PktType : uint8_t {
    T_RELIABLE = 0,
    T_UNRELIABLE = 1,
    T_ORDERED = 2,
    T_ACK = 3,
    T_PING = 4,
    T_PONG = 5,
};

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void putU8(std::vector<char>& v, uint8_t x) {
    v.push_back(static_cast<char>(x));
}

void putU16(std::vector<char>& v, uint16_t x) {
    v.push_back(static_cast<char>(x & 0xff));
    v.push_back(static_cast<char>((x >> 8) & 0xff));
}

void putU32(std::vector<char>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) {
        v.push_back(static_cast<char>((x >> (8 * i)) & 0xff));
    }
}

struct PktView {
    const char* p = nullptr;
    size_t n = 0;
    size_t pos = 0;
    bool ok = true;

    bool u8(uint8_t& out) {
        if (!ok || n - pos < 1) {
            ok = false;
            return false;
        }
        out = static_cast<uint8_t>(p[pos++]);
        return true;
    }

    bool u16(uint16_t& out) {
        if (!ok || n - pos < 2) {
            ok = false;
            return false;
        }
        out = static_cast<uint16_t>(uint8_t(p[pos])) |
              (static_cast<uint16_t>(uint8_t(p[pos + 1])) << 8);
        pos += 2;
        return true;
    }

    bool u32(uint32_t& out) {
        if (!ok || n - pos < 4) {
            ok = false;
            return false;
        }
        out = static_cast<uint32_t>(uint8_t(p[pos])) |
              (static_cast<uint32_t>(uint8_t(p[pos + 1])) << 8) |
              (static_cast<uint32_t>(uint8_t(p[pos + 2])) << 16) |
              (static_cast<uint32_t>(uint8_t(p[pos + 3])) << 24);
        pos += 4;
        return true;
    }

    const char* rest(size_t& len) const {
        len = n - pos;
        return p + pos;
    }
};

bool splitAddress(const std::string& addr, std::string& host, uint16_t& port) {
    if (addr.empty()) return false;
    if (addr.front() == '[') {
        auto close = addr.find(']');
        if (close == std::string::npos || close + 2 >= addr.size() || addr[close + 1] != ':')
            return false;
        host = addr.substr(1, close - 1);
        port = static_cast<uint16_t>(std::atoi(addr.c_str() + close + 2));
        return port != 0;
    }
    auto colon = addr.rfind(':');
    if (colon == std::string::npos || colon == 0) return false;
    host = addr.substr(0, colon);
    port = static_cast<uint16_t>(std::atoi(addr.c_str() + colon + 1));
    return port != 0;
}

// true if b is strictly after a in the u32 circular order (within a 2^31 window)
inline bool after(uint32_t a, uint32_t b) {
    return a != b && (b - a) < 0x80000000u;
}

}  // namespace

UdpLink::UdpLink(Network* net, UdpSocket* sock) : net_(net), sock_(sock) {}

UdpLink::~UdpLink() {
    sendQueue_.clear();
    fragments_.clear();
    outOfOrder_.clear();
    unrelOrdBuf_.clear();
}

bool UdpLink::setRemote(std::string host, uint16_t port) {
    if (host.empty() || port == 0) return false;
    remoteHost_ = std::move(host);
    remotePort_ = port;
    remote_ = remoteHost_ + ":" + std::to_string(remotePort_);
    remoteSet_ = true;
    return true;
}

bool UdpLink::setRemoteString(const std::string& addr) {
    std::string host;
    uint16_t port = 0;
    if (!splitAddress(addr, host, port)) return false;
    return setRemote(std::move(host), port);
}

void UdpLink::setLossRate(float rate) {
    lossRate_ = std::max(0.f, std::min(1.f, rate));
}

void UdpLink::sendDatagram(std::vector<char> pkt) {
    if (!sock_ || !remoteSet_) return;
    lastSendMs_ = nowMs();
    if (lossRate_ > 0.f &&
        (rng_() % 10000) < static_cast<uint32_t>(lossRate_ * 10000.f)) {
        return;  // simulated loss
    }
    eve::data::ByteData d(pkt.data(), pkt.size());
    sock_->sendTo(&d, remoteHost_, remotePort_);
}

void UdpLink::send(MsgType type, uint8_t channel, const void* data, size_t n) {
    if (!remoteSet_ || !sock_) return;
    if (data == nullptr && n > 0) return;
    if (n > kMaxMessage) return;
    const char* p = static_cast<const char*>(data);

    uint32_t seq = 0;
    bool track = false;
    if (type == MsgType::Reliable || type == MsgType::UnreliableOrdered) {
        seq = nextSeq_++;
        track = (type == MsgType::Reliable);
    }

    const uint8_t wireType = type == MsgType::Reliable
                                 ? T_RELIABLE
                                 : (type == MsgType::UnreliableOrdered ? T_ORDERED
                                                                       : T_UNRELIABLE);

    const auto build = [&](uint8_t flags, const uint8_t* frag, size_t fragLen) {
        std::vector<char> pkt;
        putU8(pkt, kMagic0);
        putU8(pkt, kMagic1);
        putU8(pkt, kVersion);
        putU8(pkt, wireType);
        putU8(pkt, channel);
        putU8(pkt, flags);
        putU32(pkt, seq);
        putU32(pkt, ackSend_);
        putU32(pkt, ackBitsSend_);
        pkt.insert(pkt.end(), reinterpret_cast<const char*>(frag), 
                   reinterpret_cast<const char*>(frag) + fragLen);
        return pkt;
    };

    if (n > kPayloadMTU) {
        const uint32_t msgId = nextFragId_++;
        const uint16_t fragCount =
            static_cast<uint16_t>((n + kPayloadMTU - 1) / kPayloadMTU);
        for (uint16_t i = 0; i < fragCount; ++i) {
            const size_t off = static_cast<size_t>(i) * kPayloadMTU;
            const size_t len = std::min(kPayloadMTU, n - off);
            std::vector<char> frag;
            putU32(frag, msgId);
            putU16(frag, fragCount);
            putU16(frag, i);
            frag.insert(frag.end(), p + off, p + off + len);
            auto pkt = build(kFragFlag,
                             reinterpret_cast<const uint8_t*>(frag.data()), frag.size());
            if (track) {
                SendEntry& e = sendQueue_[seq];
                if (e.pkts.empty()) {
                    e.deadlineMs = nowMs() + retryBaseMs_;
                    e.attempts = 0;
                }
                e.pkts.push_back(pkt);
            }
            sendDatagram(std::move(pkt));
        }
        return;
    }

    auto pkt = build(0, reinterpret_cast<const uint8_t*>(p), n);
    if (track) {
        SendEntry& e = sendQueue_[seq];
        e.pkts.clear();
        e.pkts.push_back(pkt);
        e.deadlineMs = nowMs() + retryBaseMs_;
        e.attempts = 0;
    }
    sendDatagram(std::move(pkt));
}

bool UdpLink::sendString(MsgType type, uint8_t channel, const std::string& s) {
    if (s.empty()) return false;
    send(type, channel, s.data(), s.size());
    return true;
}

void UdpLink::sendAckNow() {
    std::vector<char> pkt;
    putU8(pkt, kMagic0);
    putU8(pkt, kMagic1);
    putU8(pkt, kVersion);
    putU8(pkt, T_ACK);
    putU8(pkt, 0);
    putU8(pkt, 0);
    putU32(pkt, 0);
    putU32(pkt, ackSend_);
    putU32(pkt, ackBitsSend_);
    sendDatagram(std::move(pkt));
}

void UdpLink::noteReceived() {
    lastRecvMs_ = nowMs();
    if (!alive_ && remoteSet_) {
        alive_ = true;
        disconnectNotified_ = false;
    }
}

void UdpLink::pruneAcked(uint32_t ack, uint32_t bits) {
    if (sendQueue_.empty()) return;
    auto it = sendQueue_.begin();
    while (it != sendQueue_.end() && !after(ack, it->first)) {
        it = sendQueue_.erase(it);
    }
    for (uint32_t i = 0; i < 32; ++i) {
        if (bits & (1u << i)) sendQueue_.erase(ack + 1 + i);
    }
}

void UdpLink::deliver(MsgType type, uint8_t channel, std::vector<char> payload) {
    if (onMessage_) {
        onMessage_(type, channel, payload.data(), payload.size());
    }
}

void UdpLink::handleData(uint8_t type, uint8_t channel, uint32_t seq,
                         const std::vector<char>& payload) {
    if (type == T_UNRELIABLE) {
        deliver(MsgType::Unreliable, channel, payload);
        return;
    }
    if (type == T_ORDERED) {
        if (seq == expectedUnrelOrd_) {
            deliver(MsgType::UnreliableOrdered, channel, payload);
            ++expectedUnrelOrd_;
            while (true) {
                auto it = unrelOrdBuf_.find(expectedUnrelOrd_);
                if (it == unrelOrdBuf_.end()) break;
                deliver(MsgType::UnreliableOrdered, channel, std::move(it->second));
                unrelOrdBuf_.erase(it);
                ++expectedUnrelOrd_;
            }
        } else if (seq > expectedUnrelOrd_ && unrelOrdBuf_.size() < kMaxOutOfOrder) {
            unrelOrdBuf_[seq] = payload;
        }
        return;
    }

    // reliable
    if (seq == expectedReliable_) {
        deliver(MsgType::Reliable, channel, payload);
        ++expectedReliable_;
        while (true) {
            auto it = outOfOrder_.find(expectedReliable_);
            if (it == outOfOrder_.end()) break;
            deliver(MsgType::Reliable, channel, std::move(it->second));
            outOfOrder_.erase(it);
            ++expectedReliable_;
        }
    } else if (seq > expectedReliable_ && outOfOrder_.size() < kMaxOutOfOrder) {
        outOfOrder_[seq] = payload;
    }

    ackSend_ = expectedReliable_ - 1;
    ackBitsSend_ = 0;
    uint32_t scanned = 0;
    for (const auto& kv : outOfOrder_) {
        if (scanned >= 32) break;
        if (after(ackSend_, kv.first)) {
            const uint32_t idx = kv.first - ackSend_ - 1;
            if (idx < 32) ackBitsSend_ |= (1u << idx);
        }
        ++scanned;
    }
    sendAckNow();
}

void UdpLink::onDatagram(const std::vector<char>& bytes, const std::string& from) {
    (void)from;
    if (bytes.size() < kHeaderLen) return;
    const char* p = bytes.data();
    if (static_cast<uint8_t>(p[0]) != kMagic0 || static_cast<uint8_t>(p[1]) != kMagic1 ||
        static_cast<uint8_t>(p[2]) != kVersion) {
        return;
    }
    const uint8_t type = static_cast<uint8_t>(p[3]);
    const uint8_t channel = static_cast<uint8_t>(p[4]);
    const uint8_t flags = static_cast<uint8_t>(p[5]);

    PktView v{p, bytes.size(), 6, true};
    uint32_t seq = 0, ack = 0, ackBits = 0;
    if (!v.u32(seq) || !v.u32(ack) || !v.u32(ackBits)) return;
    noteReceived();
    pruneAcked(ack, ackBits);

    if (type == T_ACK || type == T_PONG) return;
    if (type == T_PING) {
        std::vector<char> pong;
        putU8(pong, kMagic0);
        putU8(pong, kMagic1);
        putU8(pong, kVersion);
        putU8(pong, T_PONG);
        putU8(pong, 0);
        putU8(pong, 0);
        putU32(pong, 0);
        putU32(pong, ackSend_);
        putU32(pong, ackBitsSend_);
        sendDatagram(std::move(pong));
        return;
    }

    size_t restLen = 0;
    const char* rest = v.rest(restLen);
    std::vector<char> payload;
    if (flags & kFragFlag) {
        PktView fv{rest, restLen, 0, true};
        uint32_t msgId = 0;
        uint16_t fragCount = 0, fragIndex = 0;
        if (!fv.u32(msgId) || !fv.u16(fragCount) || !fv.u16(fragIndex)) return;
        size_t fragLen = 0;
        const char* frag = fv.rest(fragLen);
        if (fragCount == 1) {
            payload.assign(frag, frag + fragLen);
        } else {
            if (fragCount > kMaxFragCount || fragIndex >= fragCount) return;
            auto it = fragments_.find(msgId);
            if (it == fragments_.end()) {
                if (fragments_.size() >= kMaxFragments) return;
                FragBuf fb;
                fb.total = fragCount;
                fb.data.resize(static_cast<size_t>(fragCount) * kPayloadMTU);
                fb.seen.resize(fragCount, 0);
                fb.lastMs = nowMs();
                fragments_[msgId] = std::move(fb);
                it = fragments_.find(msgId);
            }
            FragBuf& fb = it->second;
            if (fb.total != fragCount) return;
            if (fb.seen[fragIndex] != 0) return;  // duplicate fragment
            fb.seen[fragIndex] = 1;
            std::memcpy(fb.data.data() + static_cast<size_t>(fragIndex) * kPayloadMTU,
                        frag, fragLen);
            if (fragIndex == fragCount - 1) fb.lastLen = fragLen;
            fb.received++;
            fb.lastMs = nowMs();
            if (fb.received == fb.total) {
                fb.data.resize((static_cast<size_t>(fb.total) - 1) * kPayloadMTU +
                               fb.lastLen);
                payload = std::move(fb.data);
                fragments_.erase(it);
            } else {
                return;  // still assembling
            }
        }
    } else {
        payload.assign(rest, rest + restLen);
    }

    handleData(type, channel, seq, payload);
}

void UdpLink::pump(int64_t now) {
    for (auto it = sendQueue_.begin(); it != sendQueue_.end();) {
        SendEntry& e = it->second;
        if (e.deadlineMs <= now) {
            if (e.attempts >= maxAttempts_) {
                it = sendQueue_.erase(it);
                if (!disconnectNotified_) {
                    disconnectNotified_ = true;
                    alive_ = false;
                    if (onDisconnect_) onDisconnect_("timeout");
                }
                continue;
            }
            for (const auto& pkt : e.pkts) sendDatagram(pkt);
            e.attempts++;
            e.deadlineMs = now + retryBaseMs_ * (1 << std::min(e.attempts, 6));
        }
        ++it;
    }

    for (auto it = fragments_.begin(); it != fragments_.end();) {
        if (now - it->second.lastMs > kFragExpireMs) {
            it = fragments_.erase(it);
        } else {
            ++it;
        }
    }

    if (remoteSet_ && now - lastSendMs_ >= kPingIntervalMs) {
        std::vector<char> ping;
        putU8(ping, kMagic0);
        putU8(ping, kMagic1);
        putU8(ping, kVersion);
        putU8(ping, T_PING);
        putU8(ping, 0);
        putU8(ping, 0);
        putU32(ping, 0);
        putU32(ping, ackSend_);
        putU32(ping, ackBitsSend_);
        sendDatagram(std::move(ping));
    }

    if (lastRecvMs_ > 0 && now - lastRecvMs_ > timeoutMs_ && !disconnectNotified_) {
        disconnectNotified_ = true;
        alive_ = false;
        if (onDisconnect_) onDisconnect_("timeout");
    }
}

}  // namespace eve::network
