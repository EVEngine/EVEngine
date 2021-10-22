
#include "event/Event.h"
#include "event/sdl/Event.h"

namespace eve::event {

Message::Message(const std::string &name, const std::vector<ssq::Object> &vargs) : name(name), args(vargs) {}
Message::~Message() {}

Module_IMPL(Event, new sdl::Event());


void Event::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Event::create, false);
    expose(cls);
}

void Event::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Event::getName);
}

Event::~Event() {}

void Event::push(Message *msg) {
    // Lock lock(mutex);
    // msg->retain();
    queue.push(msg);
}

Message *Event::poll() {
    // Lock lock(mutex);
    if (queue.empty()) return nullptr;
    auto msg = queue.front();
    queue.pop();
    return msg;
}

void Event::clear() {
    // Lock lock(mutex);
    while (!queue.empty()) {
        // std::queue::pop will remove the first (front) element.
        delete queue.front();
        queue.pop();
    }
}

}  // namespace eve::event
