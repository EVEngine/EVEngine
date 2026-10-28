#include "platform_event/PlatformEvent.h"
#include "platform_event/sdl/Event.h"

#include "common/Assert.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::platform_event {

Message::Message(const std::string& name, const std::vector<Variant>& vargs) : name(name), args(vargs) {}
Message::~Message() {}

Module_IMPL(PlatformEvent, new sdl::Event());

void PlatformEvent::expose(ssq::Table& table) {
    auto cls = table.addClass(name, PlatformEvent::create, false);
    expose(cls);
}

void PlatformEvent::expose(ssq::Class& cls) {
    cls.addFunc("getName", &PlatformEvent::getName);
    cls.addFunc("pump", &PlatformEvent::pump);
    cls.addFunc("poll", &PlatformEvent::pollName);
    cls.addFunc("pollData", &PlatformEvent::pollData);
    cls.addFunc("getLastData", &PlatformEvent::getLastData);
    cls.addFunc("pushData", &PlatformEvent::pushData);
}

PlatformEvent::~PlatformEvent() {}

eve::Subscription PlatformEvent::subscribePoll(std::function<void(const Message&)> callback) {
    return pollObservers_.subscribe(std::move(callback));
}

void PlatformEvent::push(Message* msg) { push(std::unique_ptr<Message>(msg)); }

void PlatformEvent::push(std::unique_ptr<Message> msg) {
    EV_PARAM_CHECK(msg.get() != nullptr, "event message must not be null");
    if (!msg) return;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        queue.push(std::move(msg));
    }
    wakeWaiters();
}

void PlatformEvent::pushData(std::string eventName, std::string data) {
    std::vector<Variant> args;
    if (!data.empty()) args.push_back(Variant::makeString(std::move(data)));
    push(std::make_unique<Message>(std::move(eventName), args));
}

Message* PlatformEvent::poll() { return pollOwned().release(); }

std::unique_ptr<Message> PlatformEvent::pollOwned() {
    std::unique_ptr<Message> msg;
    {
        std::lock_guard<std::mutex> lock(queueMu_);
        if (queue.empty()) return nullptr;
        msg = std::move(queue.front());
        queue.pop();
    }
    // Report consumption outside the lock so observers may re-enter push/poll.
    // The queue mutation is already committed; callback exceptions are
    // contained and counted instead of making a successful poll look failed.
    if (msg) {
        static_cast<void>(pollObservers_.notifyChecked([this]() noexcept { ++pollObserverFailures_; }, *msg));
    }
    return msg;
}

std::string PlatformEvent::pollName() {
    auto msg = pollOwned();
    lastData_.clear();
    if (!msg) return {};
    for (const auto& a : msg->args) {
        if (a.type == Variant::Type::String) {
            lastData_ = a.s;
            break;
        }
    }
    std::string n = msg->name;
    return n;
}

std::string PlatformEvent::getLastData() const { return lastData_; }

std::string PlatformEvent::pollData() {
    auto msg = pollOwned();
    lastData_.clear();
    if (!msg) return {};
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

void PlatformEvent::clear() {
    std::lock_guard<std::mutex> lock(queueMu_);
    while (!queue.empty()) queue.pop();
}

}  // namespace eve::platform_event
