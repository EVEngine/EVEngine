#pragma once

#include "common/Module.h"

#include <queue>
#include <variant>
#include <vector>
#include <string>

namespace eve {
namespace event {

class Message {
public:
    Message(const std::string &name, const std::vector<std::variant> &vargs = {});
    ~Message();

    const std::string               name;
    const std::vector<std::variant> args;
};  // Message

class Event : public Module {
public:
    virtual ~Event();

    // Implements Module.
    virtual std::string getName() const { return "event"; }

    void         push(Message *msg);
    Message *    poll();
    virtual void clear();

    virtual void     pump() = 0;
    virtual Message *wait() = 0;

protected:
    std::queue<Message *> queue;

};  // Event

}  // namespace event
}  // namespace eve
