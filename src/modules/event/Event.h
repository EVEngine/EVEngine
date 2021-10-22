#pragma once

#include <queue>
#include <string>
#include <vector>
#include <simplesquirrel/simplesquirrel.hpp>

#include "common/Module.h"

namespace eve {
namespace event {

class Message {
public:
    Message(const std::string &name, const std::vector<ssq::Object> &vargs = {});
    ~Message();

    const std::string name;

    const std::vector<ssq::Object> args;
};  // Message

class Event : public Module {
public:
    Module_REG(Event);
    virtual ~Event();

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
