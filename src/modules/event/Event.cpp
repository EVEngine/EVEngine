#include "event/Event.h"
#include "event/sdl/Event.h"

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
    if (!msg)
        return;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        queue.push(msg);
    }
    wakeWaiters();
}

void Event::pushData(std::string name, std::string data) {
    std::vector<Variant> args;
    if (!data.empty())
        args.push_back(Variant::makeString(std::move(data)));
    push(new Message(std::move(name), args));
}

Message* Event::poll() {
    std::lock_guard<std::mutex> lock(queueMu_);
    if (queue.empty())
        return nullptr;
    auto msg = queue.front();
    queue.pop();
    return msg;
}

std::string Event::pollName() {
    Message* msg = poll();
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
    delete msg;
    return n;
}

std::string Event::getLastData() const { return lastData_; }

std::string Event::pollData() {
    Message* msg = poll();
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
    delete msg;
    return data;
}

void Event::clear() {
    std::lock_guard<std::mutex> lock(queueMu_);
    while (!queue.empty()) {
        delete queue.front();
        queue.pop();
    }
}

}  // namespace eve::event
