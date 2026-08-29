#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "platform_event/PlatformEvent.h"

#include <chrono>
#include <memory>
#include <thread>

using eve::platform_event::Message;
using eve::platform_event::PlatformEvent;
using eve::platform_event::Variant;

namespace {

struct TrackedPayload {
    explicit TrackedPayload(int* destructions) : destructions(destructions) {}
    ~TrackedPayload() { ++*destructions; }
    int* destructions;
};

}  // namespace

TEST_CASE("PlatformEvent.quitMessageSurvivesPumpPoll") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("quit"));
    auto* msg = ev->poll();
    REQUIRE(msg != nullptr);
    CHECK(msg->name == "quit");
    delete msg;
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("PlatformEvent.pollNameReturnsEmptyWhenEmpty") {
    auto* ev = PlatformEvent::create();
    CHECK(ev->pollName() == "");
}

TEST_CASE("event.Variant.makeNil") {
    auto v = Variant::makeNil();
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Nil));
    CHECK(v.i == 0);
    CHECK(v.s.empty());
    CHECK(v.p == nullptr);
}

TEST_CASE("event.Variant.makeInt") {
    auto v = Variant::makeInt(-42);
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Int));
    CHECK(v.i == -42);
}

TEST_CASE("event.Variant.makeString") {
    auto v = Variant::makeString("hello");
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::String));
    CHECK(v.s == "hello");
}

TEST_CASE("event.Variant.makePtr") {
    int x = 7;
    auto v = Variant::makePtr(&x);
    CHECK(static_cast<int>(v.type) == static_cast<int>(Variant::Type::Ptr));
    CHECK(v.p == &x);
    CHECK(!v.ownsPointer());
}

TEST_CASE("event.Variant.makeOwnedPtrSharesOneOwnerAcrossCopies") {
    int destructions = 0;
    {
        auto a = Variant::makeOwnedPtr(new TrackedPayload(&destructions));
        auto b = a;
        CHECK(a.ownsPointer());
        CHECK(b.ownsPointer());
        CHECK(a.p == b.p);
    }
    CHECK_EQ(destructions, 1);
}

TEST_CASE("event.Message.withArgs") {
    std::vector<Variant> args;
    args.push_back(Variant::makeInt(100));
    args.push_back(Variant::makeString("payload"));
    args.push_back(Variant::makeNil());
    Message msg("custom", args);
    CHECK(msg.name == "custom");
    REQUIRE(msg.args.size() == 3);
    CHECK(static_cast<int>(msg.args[0].type) == static_cast<int>(Variant::Type::Int));
    CHECK(msg.args[0].i == 100);
    CHECK(msg.args[1].s == "payload");
    CHECK(static_cast<int>(msg.args[2].type) == static_cast<int>(Variant::Type::Nil));
}

TEST_CASE("event.pushPollRoundTrip") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("first"));
    ev->push(new Message("second"));
    auto* m1 = ev->poll();
    REQUIRE(m1 != nullptr);
    CHECK(m1->name == "first");
    delete m1;
    auto* m2 = ev->poll();
    REQUIRE(m2 != nullptr);
    CHECK(m2->name == "second");
    delete m2;
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.ownedPushPollDestroysPayloadWithMessage") {
    auto*                ev           = PlatformEvent::create();
    int destructions = 0;
    std::vector<Variant> args;
    args.push_back(Variant::makeOwnedPtr(new TrackedPayload(&destructions)));
    ev->push(std::make_unique<Message>("owned", args));

    {
        auto message = ev->pollOwned();
        REQUIRE(message.get() != nullptr);
        REQUIRE(message->args.size() == 1u);
        CHECK(message->args[0].ownsPointer());
        CHECK_EQ(destructions, 0);
    }
    CHECK_EQ(destructions, 1);
}

TEST_CASE("event.clearDestroysOwnedPayloadExactlyOnce") {
    auto*                ev           = PlatformEvent::create();
    int destructions = 0;
    std::vector<Variant> args;
    args.push_back(Variant::makeOwnedPtr(new TrackedPayload(&destructions)));
    ev->push(std::make_unique<Message>("unconsumed", args));

    ev->clear();
    CHECK_EQ(destructions, 1);
    auto empty = ev->pollOwned();
    CHECK(empty.get() == nullptr);
}

TEST_CASE("event.pollNameConsumesMessage") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("hello"));
    CHECK(ev->pollName() == "hello");
    CHECK(ev->pollName() == "");
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.pollEmptyQueue") {
    auto* ev = PlatformEvent::create();
    CHECK(ev->poll() == nullptr);
}

TEST_CASE("event.clearThenPollEmpty") {
    auto* ev = PlatformEvent::create();
    ev->push(new Message("a"));
    ev->push(new Message("b"));
    ev->clear();
    CHECK(ev->poll() == nullptr);
    CHECK(ev->pollName() == "");
}

TEST_CASE("PlatformEvent.wait.returnsQueuedMessage") {
    auto* ev = PlatformEvent::create();
    ev->pushData("wait.queued", "payload");
    auto* msg = ev->wait();
    REQUIRE(msg != nullptr);
    CHECK(msg->name == "wait.queued");
    delete msg;
}

TEST_CASE("event.pumpSmoke") {
    auto* ev = PlatformEvent::create();
    ev->pump();
}

TEST_CASE("event.workerPushWakesWait") {
    auto* ev = PlatformEvent::create();
    ev->clear();
    std::thread producer([ev] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ev->pushData("worker-wake", "ready");
    });
    Message *message = ev->wait();
    producer.join();
    REQUIRE(message != nullptr);
    CHECK_EQ(message->name, std::string("worker-wake"));
    REQUIRE(message->args.size() == 1);
    CHECK_EQ(message->args[0].s, std::string("ready"));
    delete message;
}
