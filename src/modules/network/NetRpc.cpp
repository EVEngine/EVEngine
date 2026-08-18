#include "network/NetRpc.h"
#include "network/NetStream.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>

namespace eve::network {

namespace {

void callScriptFn(const ssq::Object& fn, const std::string& arg) {
    if (fn.isEmpty()) return;
    ssq::Function f = fn.toFunction();
    if (f.isEmpty()) return;
    HSQUIRRELVM raw = f.getHandle();
    SQInteger top = sq_gettop(raw);
    sq_pushobject(raw, f.getRaw());
    sq_pushroottable(raw);
    ssq::detail::pushValue(raw, arg);
    if (SQ_SUCCEEDED(sq_call(raw, 2, SQFalse, SQTrue))) {
        // discard result
    }
    sq_settop(raw, top);
}

}  // namespace

NetRpc::NetRpc(UdpLink* link) : link_(link) {
    if (link_) {
        link_->setMessageHandler(
            [this](UdpLink::MsgType type, uint8_t channel, const char* data, size_t n) {
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
    link_->send(reliable ? UdpLink::MsgType::Reliable : UdpLink::MsgType::Unreliable, 0,
                env.data(), env.size());
}

bool NetRpc::callString(uint16_t msgId, const std::string& payload, bool reliable) {
    if (!link_) return false;
    call(msgId, payload.data(), payload.size(), reliable);
    return true;
}

void NetRpc::registerHandler(uint16_t msgId, Handler h) {
    handlers_[msgId] = std::move(h);
}

void NetRpc::registerScript(uint16_t msgId, ssq::Object fn) {
    if (fn.isEmpty()) return;
    scriptHandlers_[msgId] = fn;
}

void NetRpc::onLinkMessage(UdpLink::MsgType type, uint8_t channel,
                           const char* data, size_t n) {
    (void)type;
    if (channel != 0 || n < 2) return;
    const uint16_t msgId = static_cast<uint16_t>(uint8_t(data[0])) |
                           (static_cast<uint16_t>(uint8_t(data[1])) << 8);
    const char* payload = data + 2;
    const size_t payloadLen = n - 2;

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
