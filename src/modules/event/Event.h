#pragma once

#include <cstdint>
#include <memory>
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
    std::shared_ptr<void> owner;

    /** @brief Constructs a nil variant. */
    static Variant makeNil() { return {}; }
    /** @brief Constructs an integer variant. */
    static Variant makeInt(int64_t v) {
        Variant x;
        x.type = Type::Int;
        x.i    = v;
        return x;
    }
    /** @brief Constructs a string variant (takes ownership of the value). */
    static Variant makeString(std::string v) {
        Variant x;
        x.type = Type::String;
        x.s    = std::move(v);
        return x;
    }
    /** @brief Constructs a pointer variant (borrowed, not owned). */
    static Variant makePtr(void* v) {
        Variant x;
        x.type = Type::Ptr;
        x.p    = v;
        return x;
    }
    /** @brief Constructs an owning pointer variant deleted with the message. */
    template <class T>
    static Variant makeOwnedPtr(T* v) {
        Variant x;
        x.type = Type::Ptr;
        x.p    = v;
        if (v) x.owner = std::shared_ptr<void>(v, [](void* p) { delete static_cast<T*>(p); });
        return x;
    }
    /** @brief Whether this pointer payload owns the pointed-to object. */
    bool ownsPointer() const { return static_cast<bool>(owner); }
};

/**
 * @brief A named event carrying an ordered list of Variant payloads.
 * Pushed messages are heap-allocated; the queue owns them until polled,
 * after which the caller is responsible for deleting them.
 */
class Message {
public:
    /**
     * @brief Creates an event message.
     * @param name  Event name/type.
     * @param vargs Optional payload values.
     */
    Message(const std::string& name, const std::vector<Variant>& vargs = {});
    ~Message();

    const std::string         name;
    const std::vector<Variant> args;
};

class Event : public Module {
public:
    Module_REG(Event);
    virtual ~Event();

    /**
     * @brief Queues a message. Thread-safe: workers may push completions for the main loop to poll.
     * @param msg Message to queue (must be non-null; ownership transfers to the queue).
     */
    void         push(Message* msg);
    /** @brief Queues an owning message handle; ownership transfers to the queue. */
    void         push(std::unique_ptr<Message> msg);
    /**
     * @brief Script helper: pushes an event with an optional string payload.
     * @param name Event name.
     * @param data Optional string payload stored as the first argument.
     */
    void         pushData(std::string name, std::string data = "");
    /** @brief Pops the oldest message, or nullptr if the queue is empty (caller must delete). */
    Message*     poll();
    /** @brief Pops the oldest message into an RAII handle, or returns null. */
    std::unique_ptr<Message> pollOwned();
    /** @brief Script helper: pops one message and returns its name, or "" if none. */
    std::string  pollName();
    /** @brief First string arg of the message most recently returned by pollName/pollData. */
    std::string  getLastData() const;
    /**
     * @brief Pops one message and returns its first string arg (or "").
     * Name is discarded — use pollName when you need the type.
     */
    std::string  pollData();
    /** @brief Drops all queued messages and frees them. */
    virtual void clear();

    /** @brief Platform hook: pumps the native event queue into this module. */
    virtual void     pump() = 0;
    /** @brief Platform hook: blocks until a message is available, then polls it. */
    virtual Message* wait() = 0;
    /** @brief Blocking wait returning an RAII handle. */
    std::unique_ptr<Message> waitOwned() { return std::unique_ptr<Message>(wait()); }

protected:
    /** @brief Wake a platform-specific blocking wait after a message is queued. */
    virtual void wakeWaiters() {}

    std::mutex           queueMu_;
    std::queue<std::unique_ptr<Message>> queue;
    std::string          lastData_;
};

}  // namespace event
}  // namespace eve
