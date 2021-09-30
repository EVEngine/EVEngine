
#include "event/Event.h"

namespace eve {
namespace event {

Message::Message(const std::string &name, const std::vector<std::variant> &vargs) : name(name), args(vargs) {}

Message::~Message() {}

Event::~Event() {}

void Event::push(Message *msg) {
    // Lock lock(mutex);
    msg->retain();
    queue.push(msg);
}

bool Event::poll(Message *&msg) {
    // Lock lock(mutex);
    if (queue.empty()) return false;
    msg = queue.front();
    queue.pop();
    return true;
}

void Event::clear() {
    // Lock lock(mutex);
    while (!queue.empty()) {
        // std::queue::pop will remove the first (front) element.
        queue.front()->release();
        queue.pop();
    }
}

}  // namespace event
}  // namespace eve
