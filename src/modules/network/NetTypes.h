#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::network {

constexpr size_t kMaxSendBuffer = 1024 * 1024;
constexpr size_t kMaxFrameSize  = 1024 * 1024;

enum class NetKind { Tcp, Udp, Http, Channel };

enum class NetEvType {
    Conn,
    Data,
    Err,
    HttpResp,
    ChMsg,
    ChClose
};

struct NetCompletion {
    NetEvType type = NetEvType::Err;
    NetKind   kind = NetKind::Tcp;
    void*     handle = nullptr;  // TcpSocket* / UdpSocket* / HttpRequest* / Channel*
    std::shared_ptr<std::vector<char>> bytes;
    std::string peer;
    std::string reason;
    int status = 0;
};

}  // namespace eve::network
