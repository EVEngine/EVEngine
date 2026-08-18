#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/Exception.h"

namespace eve::rx {

// ---------------------------------------------------------------------------
// Value: runtime variant used by the script-facing (Squirrel) streams. The C++
// core is templated (Observer<T>/Observable<T>/Subject<T>); the module binds a
// Value-typed specialization so scripts can push nil/int/float/bool/string/ptr.
// ---------------------------------------------------------------------------
class Value {
public:
    enum class Type { Nil, Int, Float, Bool, String, Ptr };

    Type        type = Type::Nil;
    int64_t     i    = 0;
    double      f    = 0;
    bool        b    = false;
    std::string s;
    void*       p = nullptr;

    static Value makeNil() { return {}; }
    static Value makeInt(int64_t v) {
        Value x;
        x.type = Type::Int;
        x.i    = v;
        return x;
    }
    static Value makeFloat(double v) {
        Value x;
        x.type = Type::Float;
        x.f    = v;
        return x;
    }
    static Value makeBool(bool v) {
        Value x;
        x.type = Type::Bool;
        x.b    = v;
        return x;
    }
    static Value makeString(std::string v) {
        Value x;
        x.type = Type::String;
        x.s    = std::move(v);
        return x;
    }
    static Value makePtr(void* v) {
        Value x;
        x.type = Type::Ptr;
        x.p    = v;
        return x;
    }

    bool isNil() const { return type == Type::Nil; }
    bool isInt() const { return type == Type::Int; }
    bool isFloat() const { return type == Type::Float; }
    bool isBool() const { return type == Type::Bool; }
    bool isString() const { return type == Type::String; }
    bool isPtr() const { return type == Type::Ptr; }

    int64_t  toInt() const;
    double   toFloat() const;
    bool     toBool() const;
    std::string toString() const;

    bool equals(const Value& o) const;
};

// ---------------------------------------------------------------------------
// Subscription: RAII-ish dispose handle.
// ---------------------------------------------------------------------------
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> dispose) : dispose_(std::move(dispose)) {}
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept : dispose_(std::move(other.dispose_)), disposed_(other.disposed_) {
        other.dispose_  = nullptr;
        other.disposed_ = true;
    }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            dispose();
            dispose_  = std::move(other.dispose_);
            disposed_ = other.disposed_;
            other.dispose_  = nullptr;
            other.disposed_ = true;
        }
        return *this;
    }
    ~Subscription() { dispose(); }

    void dispose() {
        if (dispose_ && !disposed_) {
            disposed_ = true;
            auto fn = std::move(dispose_);
            dispose_ = nullptr;
            fn();
        }
    }
    bool isDisposed() const { return disposed_; }

private:
    std::function<void()> dispose_;
    bool                  disposed_ = false;
};

// ---------------------------------------------------------------------------
// Observer: callbacks pushed to a subscriber.
// ---------------------------------------------------------------------------
template <typename T>
class Observer {
public:
    using NextFn      = std::function<void(const T&)>;
    using ErrorFn     = std::function<void(const std::string&)>;
    using CompletedFn = std::function<void()>;

    NextFn      onNext;
    ErrorFn     onError;
    CompletedFn onCompleted;

    bool isStopped() const { return stopped_; }
    void setStopped() const { stopped_ = true; }

    void next(const T& v) const {
        if (!stopped_ && onNext) onNext(v);
    }
    void error(const std::string& e) const {
        if (stopped_) return;
        stopped_ = true;
        if (onError) onError(e);
    }
    void completed() const {
        if (stopped_) return;
        stopped_ = true;
        if (onCompleted) onCompleted();
    }

private:
    mutable bool stopped_ = false;
};

// ---------------------------------------------------------------------------
// Observable: push-based source. Operators return a new (owned) Observable.
// ---------------------------------------------------------------------------
template <typename T>
class Observable {
public:
    virtual ~Observable() = default;

    virtual Subscription subscribe(Observer<T> obs) = 0;

    Subscription subscribe(typename Observer<T>::NextFn next) {
        Observer<T> obs;
        obs.onNext = std::move(next);
        return subscribe(std::move(obs));
    }
    Subscription subscribe(typename Observer<T>::NextFn next,
                           typename Observer<T>::ErrorFn error,
                           typename Observer<T>::CompletedFn completed) {
        Observer<T> obs;
        obs.onNext      = std::move(next);
        obs.onError     = std::move(error);
        obs.onCompleted = std::move(completed);
        return subscribe(std::move(obs));
    }

    // ---- LINQ-style operators (return new Observable, caller owns) ----
    Observable<T>* filter(std::function<bool(const T&)> pred);
    template <typename R>
    Observable<R>* map(std::function<R(const T&)> fn);
    Observable<T>* take(int n);
    Observable<T>* skip(int n);
    Observable<T>* first();
    Observable<T>* takeUntil(Observable<T>* other);
    Observable<T>* distinctUntilChanged();
};

// ---- Internal: anonymous observable built from a subscribe function ----
template <typename T>
class AnonymousObservable : public Observable<T> {
public:
    explicit AnonymousObservable(std::function<Subscription(Observer<T>)> fn)
        : fn_(std::move(fn)) {}
    Subscription subscribe(Observer<T> obs) override { return fn_(std::move(obs)); }

private:
    std::function<Subscription(Observer<T>)> fn_;
};

// ---- Operators ----
template <typename T>
Observable<T>* Observable<T>::filter(std::function<bool(const T&)> pred) {
    auto* self = this;
    return new AnonymousObservable<T>([self, pred](Observer<T> out) {
        Observer<T> in;
        in.onNext = [out, pred](const T& v) {
            if (pred(v)) out.next(v);
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

template <typename T>
template <typename R>
Observable<R>* Observable<T>::map(std::function<R(const T&)> fn) {
    auto* self = this;
    return new AnonymousObservable<R>([self, fn](Observer<R> out) {
        Observer<T> in;
        in.onNext = [out, fn](const T& v) { out.next(fn(v)); };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

template <typename T>
Observable<T>* Observable<T>::take(int n) {
    auto* self = this;
    return new AnonymousObservable<T>([self, n](Observer<T> out) {
        auto remaining = std::make_shared<int>(n);
        Observer<T> in;
        in.onNext = [out, remaining](const T& v) {
            if (*remaining <= 0) return;
            *remaining -= 1;
            out.next(v);
            if (*remaining == 0) {
                out.completed();
                // note: upstream keeps running; completed() already stopped `out`
            }
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

template <typename T>
Observable<T>* Observable<T>::skip(int n) {
    auto* self = this;
    return new AnonymousObservable<T>([self, n](Observer<T> out) {
        auto remaining = std::make_shared<int>(n);
        Observer<T> in;
        in.onNext = [out, remaining](const T& v) {
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

template <typename T>
Observable<T>* Observable<T>::first() {
    auto* self = this;
    return new AnonymousObservable<T>([self](Observer<T> out) {
        auto done = std::make_shared<bool>(false);
        Observer<T> in;
        in.onNext = [out, done](const T& v) {
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

template <typename T>
Observable<T>* Observable<T>::takeUntil(Observable<T>* other) {
    auto* self = this;
    return new AnonymousObservable<T>([self, other](Observer<T> out) {
        Observer<T> in;
        in.onNext = [out](const T& v) { out.next(v); };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        auto src = std::make_shared<Subscription>(self->subscribe(std::move(in)));

        Observer<T> stopper;
        stopper.onNext = [out, src](const T&) mutable {
            out.completed();
            src->dispose();
        };
        stopper.onCompleted = [out, src]() mutable {
            out.completed();
            src->dispose();
        };
        auto stop = std::make_shared<Subscription>(other->subscribe(std::move(stopper)));

        return Subscription([src, stop]() mutable {
            src->dispose();
            stop->dispose();
        });
    });
}

template <typename T>
Observable<T>* Observable<T>::distinctUntilChanged() {
    auto* self = this;
    return new AnonymousObservable<T>([self](Observer<T> out) {
        auto last = std::make_shared<T>();
        auto has  = std::make_shared<bool>(false);
        Observer<T> in;
        in.onNext = [out, last, has](const T& v) {
            if (*has && *last == v) return;
            *has  = true;
            *last = v;
            out.next(v);
        };
        in.onError     = [out](const std::string& e) { out.error(e); };
        in.onCompleted = [out]() { out.completed(); };
        return self->subscribe(std::move(in));
    });
}

// ---------------------------------------------------------------------------
// Subject: multicast push-based. Both an Observable and an Observer source.
// Thread-safe: onNext/onError/onCompleted/subscribe are mutex-protected.
// ---------------------------------------------------------------------------
template <typename T>
class Subject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    ~Subject() override = default;

    Subscription subscribe(Observer<T> obs) override {
        auto slot = std::make_shared<Slot>();
        slot->observer = std::move(obs);
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (stopped_) {
                // Terminal state: deliver the terminal notification immediately
                // to a fresh copy of the observer and never register the slot.
                Observer<T> snapshot = slot->observer;
                snapshot.completed();
                return Subscription{};
            }
            slots_.push_back(slot);
        }
        return Subscription([this, slot]() { removeSlot(slot); });
    }

    void onNext(const T& v) { emit([&](Observer<T>& o) { o.next(v); }); }
    void onError(const std::string& e) {
        emit([&](Observer<T>& o) { o.error(e); });
        markStopped();
    }
    void onCompleted() {
        emit([&](Observer<T>& o) { o.completed(); });
        markStopped();
    }

    bool hasObservers() const {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& s : slots_)
            if (!s->removed) return true;
        return false;
    }
    int observerCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        int n = 0;
        for (const auto& s : slots_)
            if (!s->removed) n++;
        return n;
    }

private:
    struct Slot {
        Observer<T> observer;
        bool        removed = false;
    };

    void emit(const std::function<void(Observer<T>&)>& fn) {
        std::vector<std::shared_ptr<Slot>> copy;
        {
            std::lock_guard<std::mutex> lock(mu_);
            for (auto& s : slots_)
                if (!s->removed) copy.push_back(s);
        }
        for (auto& s : copy) {
            Observer<T> snapshot = s->observer;
            fn(snapshot);
        }
    }

    void removeSlot(const std::shared_ptr<Slot>& slot) {
        std::lock_guard<std::mutex> lock(mu_);
        slot->removed = true;
    }

    void markStopped() {
        std::lock_guard<std::mutex> lock(mu_);
        stopped_ = true;
        slots_.clear();
    }

    mutable std::mutex                   mu_;
    std::vector<std::shared_ptr<Slot>>   slots_;
    bool                                 stopped_ = false;
};

// ---------------------------------------------------------------------------
// BehaviorSubject: replays the latest value to new subscribers.
// ---------------------------------------------------------------------------
template <typename T>
class BehaviorSubject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    explicit BehaviorSubject(T initial) : latest_(std::move(initial)) {}

    Subscription subscribe(Observer<T> obs) override {
        T latest;
        bool replay = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!completed_) {
                latest = latest_;
                replay = true;
            }
        }
        if (replay) obs.next(latest);
        return base_.subscribe(std::move(obs));
    }

    T getValue() const {
        std::lock_guard<std::mutex> lock(mu_);
        return latest_;
    }
    void setValue(T v) {
        T emitted;
        {
            std::lock_guard<std::mutex> lock(mu_);
            latest_ = std::move(v);
            emitted = latest_;
        }
        base_.onNext(emitted);
    }
    void onNext(T v) { setValue(std::move(v)); }
    void onError(const std::string& e) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            completed_ = true;
        }
        base_.onError(e);
    }
    void onCompleted() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            completed_ = true;
        }
        base_.onCompleted();
    }
    bool hasObservers() const { return base_.hasObservers(); }

private:
    mutable std::mutex  mu_;
    T                   latest_;
    bool                completed_ = false;
    Subject<T>          base_;
};

// ---------------------------------------------------------------------------
// ReplaySubject: buffers up to `capacity` (0 = unlimited) values and replays
// them to every new subscriber.
// ---------------------------------------------------------------------------
template <typename T>
class ReplaySubject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    explicit ReplaySubject(int capacity = 0) : capacity_(capacity) {}

    Subscription subscribe(Observer<T> obs) override {
        std::vector<T> replay;
        {
            std::lock_guard<std::mutex> lock(mu_);
            replay = buffer_;
        }
        for (const auto& v : replay)
            if (!obs.isStopped()) obs.next(v);
        return base_.subscribe(std::move(obs));
    }

    void onNext(T v) {
        T emitted = v;
        {
            std::lock_guard<std::mutex> lock(mu_);
            buffer_.push_back(std::move(v));
            if (capacity_ > 0 && static_cast<int>(buffer_.size()) > capacity_)
                buffer_.erase(buffer_.begin());
        }
        base_.onNext(emitted);
    }
    void onError(const std::string& e) {
        base_.onError(e);
    }
    void onCompleted() {
        base_.onCompleted();
    }
    bool hasObservers() const { return base_.hasObservers(); }

private:
    mutable std::mutex mu_;
    std::vector<T>     buffer_;
    int                capacity_;
    Subject<T>         base_;
};

// ---------------------------------------------------------------------------
// ReactiveProperty: observable value. Reading returns the current value;
// writing pushes the new value to subscribers (and the property itself).
// ---------------------------------------------------------------------------
template <typename T>
class ReactiveProperty {
public:
    explicit ReactiveProperty(T initial = T()) : subject_(std::move(initial)) {}

    T get() const { return subject_.getValue(); }
    void set(T v) { subject_.setValue(std::move(v)); }

    Subscription subscribe(Observer<T> obs) { return subject_.subscribe(std::move(obs)); }
    Subscription subscribe(typename Observer<T>::NextFn next) { return subject_.subscribe(std::move(next)); }

    BehaviorSubject<T>* asSubject() { return &subject_; }
    Observable<T>*      asObservable() { return &subject_; }

private:
    BehaviorSubject<T> subject_;
};

}  // namespace eve::rx
