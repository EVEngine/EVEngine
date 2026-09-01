#include "network/NetHost.h"
#include "data/ByteData.h"
#include "network/Network.h"
#include "network/UdpSocket.h"
#include "platform_event/PlatformEvent.h"

namespace eve::network {

NetHost::NetHost(Network* net) : net_(net) {}

NetHost::~NetHost() {
    if (net_ && sock_) net_->unbindUdpHost(sock_);
    for (UdpLink* l : owned_) delete l;
    owned_.clear();
    byAddr_.clear();
    byId_.clear();
    delete sock_;
    sock_ = nullptr;
}

bool NetHost::start(uint16_t port) {
    if (!net_ || sock_) return false;
    sock_ = net_->newUdp();
    if (!sock_ || !sock_->bind(port)) {
        delete sock_;
        sock_ = nullptr;
        return false;
    }
    net_->bindUdpHost(sock_, this);
    return true;
}

void NetHost::setLossRate(float rate) {
    lossRate_ = rate;
    for (auto& kv : byId_) kv.second->setLossRate(rate);
}

void NetHost::setTimeoutMs(int ms) {
    timeoutMs_ = ms;
    for (auto& kv : byId_) kv.second->setTimeoutMs(ms);
}

void NetHost::onDatagram(const std::vector<char>& bytes, const std::string& from) {
    auto it = byAddr_.find(from);
    if (it != byAddr_.end()) {
        it->second->onDatagram(bytes, from);
        return;
    }

    auto* link = new UdpLink(net_, sock_);
    link->setRemoteString(from);
    const uint32_t id = nextPeerId_++;
    link->setPeerId(id);
    link->setLossRate(lossRate_);
    link->setTimeoutMs(timeoutMs_);
    link->setMessageHandler(
        [this, id](UdpLink::MsgType t, uint8_t ch, const char* d, size_t n) {
            if (onMessage_) onMessage_(id, t, ch, d, n);
        });
    link->setDisconnectHandler([this, id](const std::string&) {
        pendingRemove_.push_back(id);
    });
    byAddr_[from] = link;
    byId_[id] = link;
    owned_.push_back(link);
    emitPeerConnected(id);
    link->onDatagram(bytes, from);
}

void NetHost::pump(int64_t nowMs) {
    pendingRemove_.clear();
    for (auto& kv : byId_) kv.second->pump(nowMs);
    for (uint32_t id : pendingRemove_) emitPeerDisconnected(id);
}

UdpLink* NetHost::linkByPeerId(uint32_t peerId) const {
    auto it = byId_.find(peerId);
    return it == byId_.end() ? nullptr : it->second;
}

void NetHost::sendTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                     const void* data, size_t n) {
    UdpLink* link = linkByPeerId(peerId);
    if (link) link->send(type, channel, data, n);
}

bool NetHost::sendStringTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel,
                           const std::string& s) {
    UdpLink* link = linkByPeerId(peerId);
    return link && link->sendString(type, channel, s);
}

void NetHost::emitPeerConnected(uint32_t peerId) {
    if (onConnect_) onConnect_(peerId);
    if (net_) {
        auto* ev = eve::ModuleManager::getInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
        if (ev) {
            std::vector<eve::platform_event::Variant> args;
            args.push_back(eve::platform_event::Variant::makeInt(peerId));
            ev->push(new eve::platform_event::Message("peerconn", args));
        }
    }
}

void NetHost::emitPeerDisconnected(uint32_t peerId) {
    auto it = byId_.find(peerId);
    if (it == byId_.end()) return;  // already removed
    UdpLink* link = it->second;
    byId_.erase(it);
    for (auto a = byAddr_.begin(); a != byAddr_.end(); ++a) {
        if (a->second == link) {
            byAddr_.erase(a);
            break;
        }
    }
    if (onDisconnect_) onDisconnect_(peerId);
    if (net_) {
        auto* ev = eve::ModuleManager::getInstance<eve::platform_event::PlatformEvent>("PlatformEvent");
        if (ev) {
            std::vector<eve::platform_event::Variant> args;
            args.push_back(eve::platform_event::Variant::makeInt(peerId));
            ev->push(new eve::platform_event::Message("peerdisconn", args));
        }
    }
}

}  // namespace eve::network
