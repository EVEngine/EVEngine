#include "network/NetHost.h"
#include <chrono>
#include <cstring>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "data/ByteData.h"
#include "network/NetRpc.h"
#include "network/NetStream.h"
#include "network/NetWorker.h"
#include "network/Network.h"
#include "network/UdpSocket.h"
#include "platform_event/PlatformEvent.h"

// NetHost.cpp
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
    link->setMessageHandler([this, id](UdpLink::MsgType t, uint8_t ch, const char* d, size_t n) {
        if (onMessage_) onMessage_(id, t, ch, d, n);
    });
    link->setDisconnectHandler([this, id](const std::string&) { pendingRemove_.push_back(id); });
    byAddr_[from] = link;
    byId_[id]     = link;
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

void NetHost::sendTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel, const void* data, size_t n) {
    UdpLink* link = linkByPeerId(peerId);
    if (link) link->send(type, channel, data, n);
}

bool NetHost::sendStringTo(uint32_t peerId, UdpLink::MsgType type, uint8_t channel, const std::string& s) {
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

// NetRpc.cpp
namespace eve::network {

namespace {

void callScriptFn(const ssq::Object& fn, const std::string& arg) {
    if (fn.isEmpty()) return;
    ssq::Function f = fn.toFunction();
    if (f.isEmpty()) return;
    // The failure used to be checked only to be ignored (an "if succeeded, discard"
    // body); the helper derives sq_call's count and reports the script error.
    (void)script::callFunction(f.getHandle(), f.getRaw(), script::rootTable, arg);
}

}  // namespace

NetRpc::NetRpc(UdpLink* link) : link_(link) {
    if (link_) {
        link_->setMessageHandler([this](UdpLink::MsgType type, uint8_t channel, const char* data, size_t n) {
            onLinkMessage(type, channel, data, n);
        });
    }
}

NetRpc::~NetRpc() {
    if (link_) link_->setMessageHandler(nullptr);
}

void NetRpc::call(uint16_t msgId, const void* data, size_t n, bool reliable) {
    if (!link_ || (data == nullptr && n > 0)) return;
    std::vector<char> env;
    env.push_back(static_cast<char>(msgId & 0xff));
    env.push_back(static_cast<char>((msgId >> 8) & 0xff));
    const char* p = static_cast<const char*>(data);
    env.insert(env.end(), p, p + n);
    link_->send(reliable ? UdpLink::MsgType::Reliable : UdpLink::MsgType::Unreliable, 0, env.data(), env.size());
}

bool NetRpc::callString(uint16_t msgId, const std::string& payload, bool reliable) {
    if (!link_) return false;
    call(msgId, payload.data(), payload.size(), reliable);
    return true;
}

void NetRpc::registerHandler(uint16_t msgId, Handler h) { handlers_[msgId] = std::move(h); }

void NetRpc::registerScript(uint16_t msgId, ssq::Object fn) {
    if (fn.isEmpty()) return;
    scriptHandlers_[msgId] = fn;
}

void NetRpc::onLinkMessage(UdpLink::MsgType type, uint8_t channel, const char* data, size_t n) {
    (void)type;
    if (channel != 0 || n < 2) return;
    const uint16_t msgId   = static_cast<uint16_t>(uint8_t(data[0])) | (static_cast<uint16_t>(uint8_t(data[1])) << 8);
    const char*    payload = data + 2;
    const size_t   payloadLen = n - 2;

    auto sit = scriptHandlers_.find(msgId);
    if (sit != scriptHandlers_.end()) {
        callScriptFn(sit->second, std::string(payload, payloadLen));
        return;
    }
    auto it = handlers_.find(msgId);
    if (it == handlers_.end()) return;
    NetReader reader(payload, payloadLen);
    it->second(reader);
}

}  // namespace eve::network

// NetStream.cpp
namespace eve::network {

namespace {

void putLe16(std::vector<char>& buf, uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
}

void putLe32(std::vector<char>& buf, uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xff));
    buf.push_back(static_cast<char>((v >> 8) & 0xff));
    buf.push_back(static_cast<char>((v >> 16) & 0xff));
    buf.push_back(static_cast<char>((v >> 24) & 0xff));
}

void putLe64(std::vector<char>& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
    }
}

uint16_t getLe16(const char* p) {
    return static_cast<uint16_t>(uint8_t(p[0])) | (static_cast<uint16_t>(uint8_t(p[1])) << 8);
}

uint32_t getLe32(const char* p) {
    return static_cast<uint32_t>(uint8_t(p[0])) | (static_cast<uint32_t>(uint8_t(p[1])) << 8) |
           (static_cast<uint32_t>(uint8_t(p[2])) << 16) | (static_cast<uint32_t>(uint8_t(p[3])) << 24);
}

uint64_t getLe64(const char* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= (uint64_t(uint8_t(p[i])) << (8 * i));
    }
    return v;
}

}  // namespace

void NetWriter::put(const void* d, size_t n) {
    if (!d || n == 0) return;
    const char* p = static_cast<const char*>(d);
    buf_.insert(buf_.end(), p, p + n);
}

void NetWriter::writeU8(uint8_t v) { buf_.push_back(static_cast<char>(v)); }

void NetWriter::writeI8(int8_t v) { buf_.push_back(static_cast<char>(v)); }

void NetWriter::writeU16(uint16_t v) { putLe16(buf_, v); }

void NetWriter::writeI16(int16_t v) { putLe16(buf_, static_cast<uint16_t>(v)); }

void NetWriter::writeU32(uint32_t v) { putLe32(buf_, v); }

void NetWriter::writeI32(int32_t v) { putLe32(buf_, static_cast<uint32_t>(v)); }

void NetWriter::writeU64(uint64_t v) { putLe64(buf_, v); }

void NetWriter::writeI64(int64_t v) { putLe64(buf_, static_cast<uint64_t>(v)); }

void NetWriter::writeF32(float v) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "float must be 32-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    putLe32(buf_, bits);
}

void NetWriter::writeF64(double v) {
    uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(v), "double must be 64-bit");
    std::memcpy(&bits, &v, sizeof(bits));
    putLe64(buf_, bits);
}

void NetWriter::writeBool(bool v) { buf_.push_back(v ? 1 : 0); }

void NetWriter::writeString(const std::string& s) {
    putLe32(buf_, static_cast<uint32_t>(s.size()));
    put(s.data(), s.size());
}

void NetWriter::writeBytes(const void* d, size_t n) {
    putLe32(buf_, static_cast<uint32_t>(n));
    put(d, n);
}

NetReader::NetReader(const void* d, size_t n) { init(d, n); }

NetReader::NetReader(const std::vector<char>& d) { init(d.data(), d.size()); }

bool NetReader::init(const void* d, size_t n) {
    if (d == nullptr && n != 0) {
        ok_ = false;
        return false;
    }
    data_ = static_cast<const char*>(d);
    size_ = n;
    pos_  = 0;
    ok_   = true;
    return true;
}

bool NetReader::take(void* out, size_t n) {
    if (!ok_) return false;
    if (n > size_ - pos_) {
        ok_ = false;
        return false;
    }
    if (out && n > 0) std::memcpy(out, data_ + pos_, n);
    pos_ += n;
    return true;
}

size_t NetReader::remaining() const {
    if (!ok_) return 0;
    return size_ >= pos_ ? size_ - pos_ : 0;
}

uint8_t NetReader::u8() {
    uint8_t v = 0;
    take(&v, 1);
    return v;
}

int8_t NetReader::i8() {
    int8_t v = 0;
    take(&v, 1);
    return v;
}

uint16_t NetReader::u16() {
    char tmp[2] = {0, 0};
    if (!take(tmp, 2)) return 0;
    return getLe16(tmp);
}

int16_t NetReader::i16() { return static_cast<int16_t>(u16()); }

uint32_t NetReader::u32() {
    char tmp[4] = {0, 0, 0, 0};
    if (!take(tmp, 4)) return 0;
    return getLe32(tmp);
}

int32_t NetReader::i32() { return static_cast<int32_t>(u32()); }

uint64_t NetReader::u64() {
    char tmp[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    if (!take(tmp, 8)) return 0;
    return getLe64(tmp);
}

int64_t NetReader::i64() { return static_cast<int64_t>(u64()); }

float NetReader::f32() {
    uint32_t bits = u32();
    float    v    = 0.f;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

double NetReader::f64() {
    uint64_t bits = u64();
    double   v    = 0.0;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

bool NetReader::b() { return u8() != 0; }

std::string NetReader::str() {
    uint32_t n = u32();
    if (!ok_) return {};
    std::string out;
    out.resize(n);
    if (!take(out.data(), n)) {
        out.clear();
        return out;
    }
    return out;
}

std::vector<char> NetReader::bytes(size_t n) {
    std::vector<char> out;
    uint32_t          len = u32();
    if (!ok_ || len != n || n > remaining()) {
        ok_ = false;
        return out;
    }
    out.resize(n);
    if (!take(out.data(), n)) out.clear();
    return out;
}

}  // namespace eve::network

// NetWorker.cpp
namespace eve::network {

NetWorker::NetWorker(Network* owner) : owner_(owner) {}

NetWorker::~NetWorker() { stop(); }

void NetWorker::start() {
    if (running_) return;
    running_ = true;
    thread_  = std::thread([this] { threadMain(); });
}

void NetWorker::stop() {
    running_ = false;
    cv_.notify_all();
    // A job may request shutdown from the worker itself. Joining the current
    // thread would deadlock; leave it joinable for the owner/destructor.
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
}

void NetWorker::post(NetCompletion c) {
    std::lock_guard<std::mutex> lock(mu_);
    completions_.push_back(std::move(c));
}

void NetWorker::drain(std::vector<NetCompletion>& out) {
    std::lock_guard<std::mutex> lock(mu_);
    out.swap(completions_);
}

void NetWorker::submit(std::function<void()> job) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        jobs_.push_back(std::move(job));
    }
    cv_.notify_one();
}

void NetWorker::threadMain() {
    while (true) {
        std::vector<std::function<void()>> batch;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] { return !running_ || !jobs_.empty(); });
            if (!running_ && jobs_.empty()) break;
            batch.swap(jobs_);
        }
        for (auto& job : batch) {
            if (job) job();
        }
        if (owner_) owner_->pollSockets();
    }
}

}  // namespace eve::network
