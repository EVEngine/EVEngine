#pragma once

#include <string>
#include <unordered_map>

namespace eve::network {

class Channel;

/**
 * @brief Named collection of Channels (a "session"). Lookup by name, close all.
 * Does not own the channels; the caller owns them.
 */
class Session {
public:
    Session() = default;
    ~Session();

    /** @brief Registers a channel under a name (replaces an existing entry). */
    void add(std::string name, Channel* ch);
    /** @brief Finds a channel by name, or nullptr. */
    Channel* get(std::string name);
    /** @brief Removes a channel from the session (does not delete it). */
    void remove(std::string name);
    /** @brief Closes every channel in the session. */
    void closeAll();

private:
    std::unordered_map<std::string, Channel*> channels_;
};

}  // namespace eve::network
