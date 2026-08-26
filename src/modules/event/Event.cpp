#include "event/Event.h"
#include "event/sdl/Event.h"

#include "common/Assert.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::event {

Message::Message(const std::string& name, const std::vector<Variant>& vargs) : name(name), args(vargs) {}
Message::~Message() {}

Module_IMPL(Event, new sdl::Event());

void Event::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Event::create, false);
    expose(cls);
}

void Event::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Event::getName);
    cls.addFunc("pump", &Event::pump);
    cls.addFunc("poll", &Event::pollName);
    cls.addFunc("pollData", &Event::pollData);
    cls.addFunc("getLastData", &Event::getLastData);
    cls.addFunc("pushData", &Event::pushData);
}

Event::~Event() {}

void Event::push(Message* msg) {
    push(std::unique_ptr<Message>(msg));
}

void Event::push(std::unique_ptr<Message> msg) {
    EV_PARAM_CHECK(msg.get() != nullptr, "event message must not be null");
    if (!msg)
        return;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        queue.push(std::move(msg));
    }
    wakeWaiters();
}

void Event::pushData(std::string eventName, std::string data) {
    std::vector<Variant> args;
    if (!data.empty())
        args.push_back(Variant::makeString(std::move(data)));
    push(std::make_unique<Message>(std::move(eventName), args));
}

Message* Event::poll() {
    return pollOwned().release();
}

std::unique_ptr<Message> Event::pollOwned() {
    std::unique_ptr<Message> msg;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        if (queue.empty())
            return nullptr;
        msg = std::move(queue.front());
        queue.pop();
    }
    // Report consumption outside the lock so the observer may re-enter push/poll.
    if (msg && pollObserver_) pollObserver_(*msg);
    return msg;
}

std::string Event::pollName() {
    auto msg = pollOwned();
    lastData_.clear();
    if (!msg)
        return {};
    for (const auto& a : msg->args) {
        if (a.type == Variant::Type::String) {
            lastData_ = a.s;
            break;
        }
    }
    std::string n = msg->name;
    return n;
}

std::string Event::getLastData() const { return lastData_; }

std::string Event::pollData() {
    auto msg = pollOwned();
    lastData_.clear();
    if (!msg)
        return {};
    std::string data;
    for (const auto& a : msg->args) {
        if (a.type == Variant::Type::String) {
            data = a.s;
            break;
        }
    }
    lastData_ = data;
    return data;
}

void Event::clear() {
    std::lock_guard<std::mutex> lock(queueMu_);
    while (!queue.empty()) queue.pop();
}

}  // namespace eve::event
