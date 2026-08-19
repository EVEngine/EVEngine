#pragma once

#include <string>
#include <unordered_map>

namespace eve::network {

class Channel;

class Session {
public:
    Session() = default;
    ~Session();

    void add(std::string name, Channel* ch);
    Channel* get(std::string name);
    void remove(std::string name);
    void closeAll();

private:
    std::unordered_map<std::string, Channel*> channels_;
};

}  // namespace eve::network
