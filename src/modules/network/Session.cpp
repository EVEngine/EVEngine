#include "network/Session.h"
#include "network/Channel.h"
#include "network/TcpSocket.h"

namespace eve::network {

Session::~Session() {
    closeAll();
}

void Session::add(std::string name, Channel* ch) {
    channels_[std::move(name)] = ch;
}

Channel* Session::get(std::string name) {
    auto it = channels_.find(name);
    return it == channels_.end() ? nullptr : it->second;
}

void Session::remove(std::string name) {
    channels_.erase(name);
}

void Session::closeAll() {
    for (auto& kv : channels_) {
        if (kv.second && kv.second->getSocket()) kv.second->getSocket()->close();
    }
    channels_.clear();
}

}  // namespace eve::network
