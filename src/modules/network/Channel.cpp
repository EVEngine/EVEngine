#include "network/Channel.h"
#include "network/TcpSocket.h"
#include "network/Network.h"
#include "network/NetTypes.h"
#include "data/ByteData.h"
#include "event/Event.h"
#include "common/Assert.h"
#include "common/Module.h"

#include <cstring>

namespace eve::network {

namespace {

void writeBe32(uint32_t v, char out[4]) {
    out[0] = static_cast<char>((v >> 24) & 0xff);
    out[1] = static_cast<char>((v >> 16) & 0xff);
    out[2] = static_cast<char>((v >> 8) & 0xff);
    out[3] = static_cast<char>(v & 0xff);
}

uint32_t readBe32(const char* p) {
    return (uint32_t(uint8_t(p[0])) << 24) | (uint32_t(uint8_t(p[1])) << 16) |
           (uint32_t(uint8_t(p[2])) << 8) | uint32_t(uint8_t(p[3]));
}

}  // namespace

Channel::Channel(TcpSocket* socket) : socket_(socket) {
    EV_PARAM_CHECK(socket != nullptr, "channel requires a TCP socket");
}

Channel::~Channel() {
    if (socket_ && socket_->network()) socket_->network()->unbindChannel(socket_);
}

bool Channel::sendMsg(eve::data::ByteData* data) {
    if (!data || !socket_) return false;
    size_t n = data->getSize();
    if (n > kMaxFrameSize) {
        if (socket_->network()) {
            NetCompletion c;
            c.type   = NetEvType::Err;
            c.kind   = NetKind::Channel;
            c.handle = this;
            c.reason = "limit";
            socket_->network()->post(std::move(c));
        }
        return false;
    }
    std::vector<char> frame(4 + n);
    writeBe32(static_cast<uint32_t>(n), frame.data());
    std::memcpy(frame.data() + 4, data->getData(), n);
    eve::data::ByteData wrapped(frame.data(), frame.size());
    return socket_->send(&wrapped);
}

bool Channel::sendMsgString(std::string s) {
    if (s.empty()) return false;
    eve::data::ByteData data(s.data(), s.size());
    return sendMsg(&data);
}

void Channel::feed(const std::vector<char>& bytes) {
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
    auto* net = socket_ ? socket_->network() : nullptr;
    while (true) {
        if (!hasLen_) {
            if (buffer_.size() < 4) return;
            pendingLen_ = readBe32(buffer_.data());
            hasLen_     = true;
            buffer_.erase(buffer_.begin(), buffer_.begin() + 4);
            if (pendingLen_ > kMaxFrameSize) {
                if (net) {
                    NetCompletion c;
                    c.type   = NetEvType::ChClose;
                    c.kind   = NetKind::Channel;
                    c.handle = this;
                    c.reason = "limit";
                    net->post(std::move(c));
                    NetCompletion e;
                    e.type   = NetEvType::Err;
                    e.kind   = NetKind::Channel;
                    e.handle = this;
                    e.reason = "limit";
                    net->post(std::move(e));
                }
                if (socket_) socket_->close();
                buffer_.clear();
                hasLen_ = false;
                return;
            }
        }
        if (buffer_.size() < pendingLen_) return;
        auto payload = std::make_shared<std::vector<char>>(buffer_.begin(), buffer_.begin() + pendingLen_);
        buffer_.erase(buffer_.begin(), buffer_.begin() + pendingLen_);
        hasLen_     = false;
        pendingLen_ = 0;
        if (net) {
            NetCompletion c;
            c.type   = NetEvType::ChMsg;
            c.kind   = NetKind::Channel;
            c.handle = this;
            c.bytes  = std::move(payload);
            // Emit directly via event on main thread — feed is called from pump
            using eve::event::Variant;
            using eve::event::Message;
            auto* ev = eve::ModuleManager::getInstance<eve::event::Event>("Event");
            if (ev) {
                eve::data::ByteData* bd = nullptr;
                if (c.bytes && !c.bytes->empty())
                    bd = new eve::data::ByteData(c.bytes->data(), c.bytes->size());
                std::vector<Variant> args;
                args.push_back(Variant::makePtr(this));
                args.push_back(Variant::makePtr(bd));
                ev->push(new Message("chmsg", args));
            }
        }
    }
}

}  // namespace eve::network
