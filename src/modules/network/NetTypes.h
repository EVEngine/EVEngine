#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::network {

/** @brief Maximum outgoing send buffer size per socket (1 MiB). */
constexpr size_t kMaxSendBuffer = 1024 * 1024;
/** @brief Maximum framed message size for Channel (1 MiB). */
constexpr size_t kMaxFrameSize  = 1024 * 1024;

/** @brief Which transport produced a completion. */
enum class NetKind { Tcp, Udp, Http, Channel };

/** @brief Completion event type delivered through Network::pump. */
enum class NetEvType {
    Conn,
    Data,
    Err,
    HttpResp,
    ChMsg,
    ChClose
};

/**
 * @brief One asynchronous network result/event.
 * handle points at the originating TcpSocket/UdpSocket/HttpRequest/Channel;
 * bytes carries received data; peer/reason/status describe connection context.
 */
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
