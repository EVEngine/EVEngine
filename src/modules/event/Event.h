#pragma once

#include <cstdint>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "common/Module.h"

namespace ssq {
class Table;
class Class;
}  // namespace ssq

namespace eve {
namespace event {

struct Variant {
    enum class Type { Nil, Int, String, Ptr };
    Type        type = Type::Nil;
    int64_t     i    = 0;
    std::string s;
    void*       p = nullptr;

    static Variant makeNil() { return {}; }
    static Variant makeInt(int64_t v) {
        Variant x;
        x.type = Type::Int;
        x.i    = v;
        return x;
    }
    static Variant makeString(std::string v) {
        Variant x;
        x.type = Type::String;
        x.s    = std::move(v);
        return x;
    }
    static Variant makePtr(void* v) {
        Variant x;
        x.type = Type::Ptr;
        x.p    = v;
        return x;
    }
};

class Message {
public:
    Message(const std::string& name, const std::vector<Variant>& vargs = {});
    ~Message();

    const std::string         name;
    const std::vector<Variant> args;
};

class Event : public Module {
public:
    Module_REG(Event);
    virtual ~Event();

    /** Thread-safe: workers may push completions for the main loop to poll. */
    void         push(Message* msg);
    /** Script helper: push an event with an optional string payload. */
    void         pushData(std::string name, std::string data = "");
    Message*     poll();
    /** Script helper: pop one message; return name or empty if none. */
    std::string  pollName();
    /** First string arg from the message most recently returned by pollName/pollData. */
    std::string  getLastData() const;
    /**
     * Pop one message; return its first string arg (or "").
     * Name is discarded — use pollName when you need the type.
     */
    std::string  pollData();
    virtual void clear();

    virtual void     pump() = 0;
    virtual Message* wait() = 0;

protected:
    /** Wake a platform-specific blocking wait after a message is queued. */
    virtual void wakeWaiters() {}

    std::mutex           queueMu_;
    std::queue<Message*> queue;
    std::string          lastData_;
};

}  // namespace event
}  // namespace eve
