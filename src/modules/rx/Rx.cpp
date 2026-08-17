#include "rx/Rx.h"

#include "event/Event.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <squirrel.h>

#include <unordered_map>

namespace eve::rx {

// ---------------------------------------------------------------------------
// Value conversion helpers.
// ---------------------------------------------------------------------------
int64_t Value::toInt() const {
    switch (type) {
        case Type::Int: return i;
        case Type::Float: return static_cast<int64_t>(f);
        case Type::Bool: return b ? 1 : 0;
        case Type::String:
            try {
                return static_cast<int64_t>(std::stoll(s));
            } catch (...) {
                return 0;
            }
        default: return 0;
    }
}

double Value::toFloat() const {
    switch (type) {
        case Type::Int: return static_cast<double>(i);
        case Type::Float: return f;
        case Type::Bool: return b ? 1.0 : 0.0;
        case Type::String:
            try {
                return std::stod(s);
            } catch (...) {
                return 0.0;
            }
        default: return 0.0;
    }
}

bool Value::toBool() const {
    switch (type) {
        case Type::Int: return i != 0;
        case Type::Float: return f != 0.0;
        case Type::Bool: return b;
        case Type::String: return !s.empty();
        case Type::Ptr: return p != nullptr;
        default: return false;
    }
}

std::string Value::toString() const {
    switch (type) {
        case Type::Int: return std::to_string(i);
        case Type::Float: return std::to_string(f);
        case Type::Bool: return b ? "true" : "false";
        case Type::String: return s;
        case Type::Ptr: return "ptr";
        default: return "";
    }
}

bool Value::equals(const Value& o) const {
    if (type != o.type) return false;
    switch (type) {
        case Type::Int: return i == o.i;
        case Type::Float: return f == o.f;
        case Type::Bool: return b == o.b;
        case Type::String: return s == o.s;
        case Type::Ptr: return p == o.p;
        default: return true;
    }
}

}  // namespace eve::rx

// ---------------------------------------------------------------------------
// ssq::detail specializations so Value can cross the Squirrel boundary in
// bound method parameters / return values. Declared here so all bindings in
// this TU instantiate against them.
// ---------------------------------------------------------------------------
namespace ssq {
namespace detail {

template <>
inline void pushValue(HSQUIRRELVM vm, const eve::rx::Value& v) {
    switch (v.type) {
        case eve::rx::Value::Type::Nil: sq_pushnull(vm); break;
        case eve::rx::Value::Type::Int: sq_pushinteger(vm, static_cast<SQInteger>(v.i)); break;
        case eve::rx::Value::Type::Float: sq_pushfloat(vm, static_cast<SQFloat>(v.f)); break;
        case eve::rx::Value::Type::Bool: sq_pushbool(vm, v.b ? SQTrue : SQFalse); break;
        case eve::rx::Value::Type::String:
            sq_pushstring(vm, v.s.c_str(), static_cast<SQInteger>(v.s.size()));
            break;
        case eve::rx::Value::Type::Ptr: sq_pushuserpointer(vm, v.p); break;
    }
}

template <>
inline eve::rx::Value popValue(HSQUIRRELVM vm, SQInteger index) {
    switch (sq_gettype(vm, index)) {
        case OT_NULL: return eve::rx::Value::makeNil();
        case OT_INTEGER: {
            SQInteger i = 0;
            sq_getinteger(vm, index, &i);
            return eve::rx::Value::makeInt(static_cast<int64_t>(i));
        }
        case OT_FLOAT: {
            SQFloat f = 0;
            sq_getfloat(vm, index, &f);
            return eve::rx::Value::makeFloat(static_cast<double>(f));
        }
        case OT_BOOL: {
            SQBool b = SQFalse;
            sq_getbool(vm, index, &b);
            return eve::rx::Value::makeBool(b == SQTrue);
        }
        case OT_STRING: {
            const SQChar* s = nullptr;
            sq_getstring(vm, index, &s);
            return eve::rx::Value::makeString(s ? s : "");
        }
        case OT_USERPOINTER: {
            SQUserPointer p = nullptr;
            sq_getuserpointer(vm, index, &p);
            return eve::rx::Value::makePtr(p);
        }
        default: return eve::rx::Value::makeNil();
    }
}

}  // namespace detail
}  // namespace ssq

namespace eve::rx {

// ---------------------------------------------------------------------------
// Script-facing aliases (Value-typed).
// ---------------------------------------------------------------------------
using SubjectV          = Subject<Value>;
using BehaviorSubjectV  = BehaviorSubject<Value>;
using ReplaySubjectV    = ReplaySubject<Value>;
using ObservableV       = Observable<Value>;
using ReactivePropertyV = ReactiveProperty<Value>;

namespace {

// Call a script function with the given Value args; returns its Value result.
Value callScript(const ssq::Function& fn, const std::vector<Value>& args, bool wantResult) {
    if (fn.isEmpty()) return Value::makeNil();
    HSQUIRRELVM raw = fn.getHandle();
    SQInteger   top = sq_gettop(raw);
    sq_pushobject(raw, fn.getRaw());
    sq_pushroottable(raw);
    for (const auto& a : args) ssq::detail::pushValue(raw, a);
    Value result = Value::makeNil();
    if (SQ_SUCCEEDED(sq_call(raw, static_cast<SQInteger>(args.size() + 1),
                             wantResult ? SQTrue : SQFalse, SQTrue))) {
        if (wantResult && sq_gettop(raw) > top) result = ssq::detail::popValue<Value>(raw, -1);
    }
    sq_settop(raw, top);
    return result;
}

std::function<void()> scriptCompleted(ssq::Function fn) {
    return [fn]() { callScript(fn, {}, false); };
}
std::function<void(const Value&)> scriptNext(ssq::Function fn) {
    return [fn](const Value& v) { callScript(fn, {v}, false); };
}
std::function<void(const std::string&)> scriptError(ssq::Function fn) {
    return [fn](const std::string& e) { callScript(fn, {Value::makeString(e)}, false); };
}

bool isClosure(const ssq::Object& obj) {
    if (obj.isEmpty()) return false;
    auto t = obj.getType();
    return t == ssq::Type::CLOSURE || t == ssq::Type::NATIVECLOSURE;
}

// Return a copy of obj if it is a usable closure, else an empty Object.
ssq::Object asClosure(const ssq::Object& obj) {
    if (isClosure(obj)) return obj;
    return ssq::Object();
}

Observer<Value> makeObserver(ssq::Object next, ssq::Object error, ssq::Object done) {
    Observer<Value> o;
    ssq::Object n = asClosure(next);
    ssq::Object e = asClosure(error);
    ssq::Object c = asClosure(done);
    if (!n.isEmpty()) o.onNext = scriptNext(n.toFunction());
    if (!e.isEmpty()) o.onError = scriptError(e.toFunction());
    if (!c.isEmpty()) o.onCompleted = scriptCompleted(c.toFunction());
    return o;
}

Subscription* doSubscribe(ObservableV* obs, ssq::Object next, ssq::Object error, ssq::Object done) {
    if (!obs) throw eve::Exception("Rx.subscribe: null observable");
    return new Subscription(obs->subscribe(makeObserver(next, error, done)));
}

// map(fn) : ObservableV -> ObservableV via script function returning Value.
ObservableV* doMap(ObservableV* self, ssq::Function fn) {
    if (!self) throw eve::Exception("Rx.map: null observable");
    return new AnonymousObservable<Value>([self, fn](Observer<Value> out) {
        Observer<Value> in;
        in.onNext = [out, fn](const Value& v) { out.next(callScript(fn, {v}, true)); };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

ObservableV* doFilter(ObservableV* self, ssq::Function pred) {
    if (!self) throw eve::Exception("Rx.filter: null observable");
    return new AnonymousObservable<Value>([self, pred](Observer<Value> out) {
        Observer<Value> in;
        in.onNext = [out, pred](const Value& v) {
            if (callScript(pred, {v}, true).toBool()) out.next(v);
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

ObservableV* doTake(ObservableV* self, int n) {
    if (!self) throw eve::Exception("Rx.take: null observable");
    return new AnonymousObservable<Value>([self, n](Observer<Value> out) {
        auto remaining = std::make_shared<int>(n);
        Observer<Value> in;
        in.onNext = [out, remaining](const Value& v) {
            if (*remaining <= 0) return;
            *remaining -= 1;
            out.next(v);
            if (*remaining == 0) out.completed();
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

ObservableV* doSkip(ObservableV* self, int n) {
    if (!self) throw eve::Exception("Rx.skip: null observable");
    return new AnonymousObservable<Value>([self, n](Observer<Value> out) {
        auto remaining = std::make_shared<int>(n);
        Observer<Value> in;
        in.onNext = [out, remaining](const Value& v) {
            if (*remaining > 0) {
                *remaining -= 1;
                return;
            }
            out.next(v);
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

ObservableV* doFirst(ObservableV* self) {
    if (!self) throw eve::Exception("Rx.first: null observable");
    return new AnonymousObservable<Value>([self](Observer<Value> out) {
        auto done = std::make_shared<bool>(false);
        Observer<Value> in;
        in.onNext = [out, done](const Value& v) {
            if (*done) return;
            *done = true;
            out.next(v);
            out.completed();
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

ObservableV* doDistinctUntilChanged(ObservableV* self) {
    if (!self) throw eve::Exception("Rx.distinctUntilChanged: null observable");
    return new AnonymousObservable<Value>([self](Observer<Value> out) {
        auto last = std::make_shared<Value>();
        auto has  = std::make_shared<bool>(false);
        Observer<Value> in;
        in.onNext = [out, last, has](const Value& v) {
            if (*has && last->equals(v)) return;
            *has  = true;
            *last = v;
            out.next(v);
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

}  // namespace

// ---------------------------------------------------------------------------
// Rx module.
// ---------------------------------------------------------------------------
class Rx : public Module {
public:
    Module_REG(Rx);

    SubjectV*          newSubject() { return new SubjectV(); }
    BehaviorSubjectV*  newBehaviorSubject(const Value& initial) { return new BehaviorSubjectV(initial); }
    ReplaySubjectV*    newReplaySubject(int capacity = 0) { return new ReplaySubjectV(capacity); }
    ReactivePropertyV* newProperty(const Value& initial) { return new ReactivePropertyV(initial); }

    // Event bridge: fromEvent(name) returns an Observable that forwards matching
    // messages pushed by pump() (fed from an eve::event::Event queue).
    ObservableV* fromEvent(const std::string& name) {
        auto subject = std::make_shared<SubjectV>();
        {
            std::lock_guard<std::mutex> lock(mu_);
            bridges_[name] = subject;
        }
        // Return a fresh observable that subscribes to the shared subject. The
        // returned AnonymousObservable is owned by the script; the shared subject
        // stays alive in bridges_ so pump() can keep feeding it.
        return new AnonymousObservable<Value>([subject](Observer<Value> out) {
            return subject->subscribe(std::move(out));
        });
    }

    void pump(event::Event* ev) {
        if (!ev) return;
        event::Message* msg = nullptr;
        while ((msg = ev->poll()) != nullptr) {
            std::string name = msg->name;
            std::string data;
            for (const auto& a : msg->args) {
                if (a.type == event::Variant::Type::String) {
                    data = a.s;
                    break;
                }
            }
            delete msg;
            std::shared_ptr<SubjectV> subject;
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto it = bridges_.find(name);
                if (it != bridges_.end()) subject = it->second;
            }
            if (subject) subject->onNext(Value::makeString(data));
        }
    }

private:
    std::mutex                                                    mu_;
    std::unordered_map<std::string, std::shared_ptr<SubjectV>>    bridges_;
};

Module_IMPL(Rx, new Rx());

// ---------------------------------------------------------------------------
// Squirrel binding.
// ---------------------------------------------------------------------------
namespace {

using ObsFn = std::function<ObservableV*(ObservableV*)>;

void bindCommon(ssq::Class& cls) {
    cls.addFunc("subscribe",
        std::function<Subscription*(ObservableV*, ssq::Object)>(
            [](ObservableV* self, ssq::Object n) { return doSubscribe(self, n, {}, {}); }));
    cls.addFunc("subscribe3",
        std::function<Subscription*(ObservableV*, ssq::Object, ssq::Object, ssq::Object)>(
            [](ObservableV* self, ssq::Object n, ssq::Object e, ssq::Object d) {
                return doSubscribe(self, n, e, d);
            }));
    cls.addFunc("map",
        std::function<ObservableV*(ObservableV*, ssq::Function)>(
            [](ObservableV* self, ssq::Function f) { return doMap(self, f); }));
    cls.addFunc("filter",
        std::function<ObservableV*(ObservableV*, ssq::Function)>(
            [](ObservableV* self, ssq::Function p) { return doFilter(self, p); }));
    cls.addFunc("take",
        std::function<ObservableV*(ObservableV*, int)>(
            [](ObservableV* self, int n) { return doTake(self, n); }));
    cls.addFunc("skip",
        std::function<ObservableV*(ObservableV*, int)>(
            [](ObservableV* self, int n) { return doSkip(self, n); }));
    cls.addFunc("first",
        std::function<ObservableV*(ObservableV*)>([](ObservableV* self) { return doFirst(self); }));
    cls.addFunc("distinctUntilChanged",
        std::function<ObservableV*(ObservableV*)>(
            [](ObservableV* self) { return doDistinctUntilChanged(self); }));
}

}  // namespace

void Rx::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Rx::create, false);
    expose(cls);

    auto obs = table.addClass<ObservableV>(
        "Observable", std::function<ObservableV*()>([]() -> ObservableV* { return nullptr; }), true);
    bindCommon(obs);

    auto subject = table.addClass<SubjectV>(
        "Subject", std::function<SubjectV*()>([]() -> SubjectV* { return nullptr; }), true);
    subject.addFunc("onNext", &SubjectV::onNext);
    subject.addFunc("onError", &SubjectV::onError);
    subject.addFunc("onCompleted", &SubjectV::onCompleted);
    subject.addFunc("hasObservers", &SubjectV::hasObservers);
    bindCommon(subject);

    auto behavior = table.addClass<BehaviorSubjectV>(
        "BehaviorSubject", std::function<BehaviorSubjectV*()>([]() -> BehaviorSubjectV* { return nullptr; }), true);
    behavior.addFunc("onNext", &BehaviorSubjectV::onNext);
    behavior.addFunc("onError", &BehaviorSubjectV::onError);
    behavior.addFunc("onCompleted", &BehaviorSubjectV::onCompleted);
    behavior.addFunc("hasObservers", &BehaviorSubjectV::hasObservers);
    behavior.addFunc("getValue", &BehaviorSubjectV::getValue);
    behavior.addFunc("setValue", &BehaviorSubjectV::setValue);
    bindCommon(behavior);

    auto replay = table.addClass<ReplaySubjectV>(
        "ReplaySubject", std::function<ReplaySubjectV*()>([]() -> ReplaySubjectV* { return nullptr; }), true);
    replay.addFunc("onNext", &ReplaySubjectV::onNext);
    replay.addFunc("onError", &ReplaySubjectV::onError);
    replay.addFunc("onCompleted", &ReplaySubjectV::onCompleted);
    replay.addFunc("hasObservers", &ReplaySubjectV::hasObservers);
    bindCommon(replay);

    auto prop = table.addClass<ReactivePropertyV>(
        "ReactiveProperty", std::function<ReactivePropertyV*()>([]() -> ReactivePropertyV* { return nullptr; }), true);
    prop.addFunc("get", &ReactivePropertyV::get);
    prop.addFunc("set", &ReactivePropertyV::set);
    prop.addFunc("subscribe",
        std::function<Subscription*(ReactivePropertyV*, ssq::Object)>(
            [](ReactivePropertyV* self, ssq::Object n) {
                if (!self) throw eve::Exception("Rx.subscribe: null property");
                return doSubscribe(self->asObservable(), n, {}, {});
            }));
    prop.addFunc("subscribe3",
        std::function<Subscription*(ReactivePropertyV*, ssq::Object, ssq::Object, ssq::Object)>(
            [](ReactivePropertyV* self, ssq::Object n, ssq::Object e, ssq::Object d) {
                if (!self) throw eve::Exception("Rx.subscribe: null property");
                return doSubscribe(self->asObservable(), n, e, d);
            }));

    auto sub = table.addClass<Subscription>(
        "Subscription", std::function<Subscription*()>([]() -> Subscription* { return nullptr; }), true);
    sub.addFunc("dispose", &Subscription::dispose);
    sub.addFunc("isDisposed", &Subscription::isDisposed);
}

void Rx::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Rx::getName);
    cls.addFunc("newSubject", &Rx::newSubject);
    cls.addFunc("newBehaviorSubject", &Rx::newBehaviorSubject);
    cls.addFunc("newReplaySubject", &Rx::newReplaySubject);
    cls.addFunc("newProperty", &Rx::newProperty);
    cls.addFunc("fromEvent", &Rx::fromEvent);
    cls.addFunc("pump", &Rx::pump);
}

}  // namespace eve::rx
