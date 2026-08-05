#pragma once

#include <cstdint>
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

    void         push(Message* msg);
    Message*     poll();
    virtual void clear();

    virtual void     pump() = 0;
    virtual Message* wait() = 0;

protected:
    std::queue<Message*> queue;
};

}  // namespace event
}  // namespace eve
