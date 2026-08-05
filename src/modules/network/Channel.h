#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::data {
class ByteData;
}

namespace eve::network {

class TcpSocket;
class Network;

class Channel {
public:
    explicit Channel(TcpSocket* socket);
    ~Channel();

    bool sendMsg(eve::data::ByteData* data);
    bool sendMsgString(std::string s);
    TcpSocket* getSocket() const { return socket_; }

    // Called on main thread from Network::pump / emitCompletion path
    void feed(const std::vector<char>& bytes);

private:
    TcpSocket* socket_ = nullptr;
    std::vector<char> buffer_;
    uint32_t pendingLen_ = 0;
    bool hasLen_ = false;
};

}  // namespace eve::network
