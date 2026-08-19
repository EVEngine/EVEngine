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

/**
 * @brief Length-prefixed (big-endian uint32) message framing over a TcpSocket.
 * sendMsg() writes one framed message; feed() consumes bytes and emits received
 * frames as "chmsg" events on the main thread via the Event module.
 */
class Channel {
public:
    /** @brief Creates a channel over an existing socket (socket must be non-null). */
    explicit Channel(TcpSocket* socket);
    ~Channel();

    /**
     * @brief Sends one framed message.
     * @param data Payload bytes (must be non-null; size capped at kMaxFrameSize).
     * @return True when the frame was handed to the socket.
     */
    bool sendMsg(eve::data::ByteData* data);
    /** @brief Sends a string payload as one framed message (empty strings are rejected). */
    bool sendMsgString(std::string s);
    /** @brief The underlying TCP socket, or nullptr. */
    TcpSocket* getSocket() const { return socket_; }

    /**
     * @brief Feeds received bytes into the frame parser.
     * Called on the main thread from Network::pump / the emitCompletion path.
     */
    void feed(const std::vector<char>& bytes);

private:
    TcpSocket* socket_ = nullptr;
    std::vector<char> buffer_;
    uint32_t pendingLen_ = 0;
    bool hasLen_ = false;
};

}  // namespace eve::network
