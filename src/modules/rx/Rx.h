#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "common/Exception.h"

namespace eve::rx {

/**
 * @brief Runtime variant used by the script-facing (Squirrel) streams.
 * The C++ core is templated (Observer<T>/Observable<T>/Subject<T>); the module
 * binds a Value-typed specialization so scripts can push nil/int/float/bool/
 * string/ptr.
 */
class Value {
public:
    enum class Type { Nil, Int, Float, Bool, String, Ptr };

    Type        type = Type::Nil;
    int64_t     i    = 0;
    double      f    = 0;
    bool        b    = false;
    std::string s;
    void*       p = nullptr;

    /** @brief Constructs a nil value. */
    static Value makeNil() { return {}; }
    /** @brief Constructs an integer value. */
    static Value makeInt(int64_t v) {
        Value x;
        x.type = Type::Int;
        x.i    = v;
        return x;
    }
    /** @brief Constructs a floating-point value. */
    static Value makeFloat(double v) {
        Value x;
        x.type = Type::Float;
        x.f    = v;
        return x;
    }
    /** @brief Constructs a boolean value. */
    static Value makeBool(bool v) {
        Value x;
        x.type = Type::Bool;
        x.b    = v;
        return x;
    }
    /** @brief Constructs a string value (takes ownership). */
    static Value makeString(std::string v) {
        Value x;
        x.type = Type::String;
        x.s    = std::move(v);
        return x;
    }
    /** @brief Constructs a pointer value (borrowed, not owned). */
    static Value makePtr(void* v) {
        Value x;
        x.type = Type::Ptr;
        x.p    = v;
        return x;
    }

    /** @brief Type predicates. */
    bool isNil() const { return type == Type::Nil; }
    bool isInt() const { return type == Type::Int; }
    bool isFloat() const { return type == Type::Float; }
    bool isBool() const { return type == Type::Bool; }
    bool isString() const { return type == Type::String; }
    bool isPtr() const { return type == Type::Ptr; }

    /** @brief Converters (throw eve::Exception on type mismatch). */
    int64_t  toInt() const;
    double   toFloat() const;
    bool     toBool() const;
    std::string toString() const;

    /** @brief Value equality across the supported types. */
    bool equals(const Value& o) const;
};

/**
 * @brief RAII-style dispose handle for an active stream subscription.
 * Disposing unsubscribes from the source; move-only.
 */
class Subscription {
public:
    Subscription() = default;
    /** @brief Wraps a dispose callback (usually unsubscribing from a Subject). */
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

    /** @brief Runs the dispose callback once; idempotent. */
    void dispose() {
        if (dispose_ && !disposed_) {
            disposed_ = true;
            auto fn = std::move(dispose_);
            dispose_ = nullptr;
            fn();
        }
    }
    /** @brief True once dispose() has run (or the handle was moved from). */
    bool isDisposed() const { return disposed_; }

private:
    std::function<void()> dispose_;
    bool                  disposed_ = false;
};

/**
 * @brief Callback bundle pushed to a subscriber.
 * Once error() or completed() fires the observer is stopped and later
 * notifications are ignored.
 */
template <typename T>
class Observer {
public:
    using NextFn      = std::function<void(const T&)>;
    using ErrorFn     = std::function<void(const std::string&)>;
    using CompletedFn = std::function<void()>;

    /** @brief Value callback. */
    NextFn      onNext;
    /** @brief Error callback (terminal). */
    ErrorFn     onError;
    /** @brief Completion callback (terminal). */
    CompletedFn onCompleted;

    /** @brief True after error()/completed() (or setStopped()). */
    bool isStopped() const { return stopped_; }
    /** @brief Manually marks the observer stopped (used by operators). */
    void setStopped() const { stopped_ = true; }

    /** @brief Delivers a value unless stopped. */
    void next(const T& v) const {
        if (!stopped_ && onNext) onNext(v);
    }
    /** @brief Delivers a terminal error and stops. */
    void error(const std::string& e) const {
        if (stopped_) return;
        stopped_ = true;
        if (onError) onError(e);
    }
    /** @brief Delivers a terminal completion and stops. */
    void completed() const {
        if (stopped_) return;
        stopped_ = true;
        if (onCompleted) onCompleted();
    }

private:
    mutable bool stopped_ = false;
};

/**
 * @brief Push-based stream source. Operators return a new (caller-owned)
 * Observable; subscribe() returns a Subscription used to cancel.
 */
template <typename T>
class Observable {
public:
    virtual ~Observable() = default;

    /** @brief Subscribes with a full observer; returns a cancel handle. */
    virtual Subscription subscribe(Observer<T> obs) = 0;

    /** @brief Subscribes with a value callback only. */
    Subscription subscribe(typename Observer<T>::NextFn next) {
        Observer<T> obs;
        obs.onNext = std::move(next);
        return subscribe(std::move(obs));
    }
    /** @brief Subscribes with value/error/completed callbacks. */
    Subscription subscribe(typename Observer<T>::NextFn next,
                           typename Observer<T>::ErrorFn error,
                           typename Observer<T>::CompletedFn completed) {
        Observer<T> obs;
        obs.onNext      = std::move(next);
        obs.onError     = std::move(error);
        obs.onCompleted = std::move(completed);
        return subscribe(std::move(obs));
    }

    /** @brief Passes values through only when pred(v) is true. */
    Observable<T>* filter(std::function<bool(const T&)> pred);
    /** @brief Transforms each value with fn. */
    template <typename R>
    Observable<R>* map(std::function<R(const T&)> fn);
    /** @brief Emits at most the first n values, then completes. */
    Observable<T>* take(int n);
    /** @brief Drops the first n values. */
    Observable<T>* skip(int n);
    /** @brief Emits only the first value, then completes. */
    Observable<T>* first();
    /** @brief Stops the stream when `other` emits or completes. */
    Observable<T>* takeUntil(Observable<T>* other);
    /** @brief Suppresses consecutive duplicate values (uses operator==). */
    Observable<T>* distinctUntilChanged();
};

/** @brief Internal: observable built directly from a subscribe function. */
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

/**
 * @brief Multicast push-based stream: both an Observable and a push source.
 * Thread-safe: onNext/onError/onCompleted/subscribe are mutex-protected.
 */
template <typename T>
class Subject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    ~Subject() override = default;

    /** @brief Registers an observer; returns a Subscription that unregisters it. */
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

    /** @brief Pushes a value to every registered observer. */
    void onNext(const T& v) { emit([&](Observer<T>& o) { o.next(v); }); }
    /** @brief Pushes a terminal error and stops the subject. */
    void onError(const std::string& e) {
        emit([&](Observer<T>& o) { o.error(e); });
        markStopped();
    }
    /** @brief Pushes a terminal completion and stops the subject. */
    void onCompleted() {
        emit([&](Observer<T>& o) { o.completed(); });
        markStopped();
    }

    /** @brief True while at least one live observer is registered. */
    bool hasObservers() const {
        std::lock_guard<std::mutex> lock(mu_);
        for (const auto& s : slots_)
            if (!s->removed) return true;
        return false;
    }
    /** @brief Number of live (non-disposed) observers. */
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

/**
 * @brief Subject that replays the latest value to every new subscriber.
 * setValue()/onNext() update the stored value and push it to observers.
 */
template <typename T>
class BehaviorSubject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    /** @brief Creates a subject with an initial (replayed) value. */
    explicit BehaviorSubject(T initial) : latest_(std::move(initial)) {}

    /** @brief Replays the latest value, then subscribes the observer. */
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

    /** @brief Current stored value. */
    T getValue() const {
        std::lock_guard<std::mutex> lock(mu_);
        return latest_;
    }
    /** @brief Stores a new value and pushes it to observers. */
    void setValue(T v) {
        T emitted;
        {
            std::lock_guard<std::mutex> lock(mu_);
            latest_ = std::move(v);
            emitted = latest_;
        }
        base_.onNext(emitted);
    }
    /** @brief Alias of setValue(). */
    void onNext(T v) { setValue(std::move(v)); }
    /** @brief Stops replay and delivers a terminal error. */
    void onError(const std::string& e) {
        {
            std::lock_guard<std::mutex> lock(mu_);
            completed_ = true;
        }
        base_.onError(e);
    }
    /** @brief Stops replay and delivers a terminal completion. */
    void onCompleted() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            completed_ = true;
        }
        base_.onCompleted();
    }
    /** @brief True while at least one live observer is registered. */
    bool hasObservers() const { return base_.hasObservers(); }

private:
    mutable std::mutex  mu_;
    T                   latest_;
    bool                completed_ = false;
    Subject<T>          base_;
};

/**
 * @brief Subject that buffers up to `capacity` values (0 = unlimited) and
 * replays the buffer to every new subscriber.
 */
template <typename T>
class ReplaySubject : public Observable<T> {
public:
    using Observable<T>::subscribe;
    /** @brief Creates a replaying subject with the given buffer capacity. */
    explicit ReplaySubject(int capacity = 0) : capacity_(capacity) {}

    /** @brief Replays buffered values, then subscribes the observer. */
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

    /** @brief Buffers and pushes a new value to observers. */
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
    /** @brief Delivers a terminal error to observers. */
    void onError(const std::string& e) {
        base_.onError(e);
    }
    /** @brief Delivers a terminal completion to observers. */
    void onCompleted() {
        base_.onCompleted();
    }
    /** @brief True while at least one live observer is registered. */
    bool hasObservers() const { return base_.hasObservers(); }

private:
    mutable std::mutex mu_;
    std::vector<T>     buffer_;
    int                capacity_;
    Subject<T>         base_;
};

/**
 * @brief Observable value backed by a BehaviorSubject.
 * get() returns the current value; set() stores and pushes it to subscribers.
 */
template <typename T>
class ReactiveProperty {
public:
    /** @brief Creates a property with an initial value. */
    explicit ReactiveProperty(T initial = T()) : subject_(std::move(initial)) {}

    /** @brief Current value. */
    T get() const { return subject_.getValue(); }
    /** @brief Stores a new value and notifies subscribers. */
    void set(T v) { subject_.setValue(std::move(v)); }

    /** @brief Subscribes with a full observer or a value callback. */
    Subscription subscribe(Observer<T> obs) { return subject_.subscribe(std::move(obs)); }
    Subscription subscribe(typename Observer<T>::NextFn next) { return subject_.subscribe(std::move(next)); }

    /** @brief Underlying behavior subject / observable view. */
    BehaviorSubject<T>* asSubject() { return &subject_; }
    Observable<T>*      asObservable() { return &subject_; }

private:
    BehaviorSubject<T> subject_;
};

}  // namespace eve::rx
