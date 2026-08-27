#include "common/Subscription.h"

#include <utility>

namespace eve {

Subscription::Subscription(Cancel cancel) : cancel_(std::move(cancel)), disposed_(!cancel_) {}

Subscription::Subscription(Subscription &&other) noexcept
    : cancel_(std::move(other.cancel_)), disposed_(other.disposed_) {
    other.disposed_ = true;
}

Subscription &Subscription::operator=(Subscription &&other) noexcept {
    if (this == &other) return *this;
    dispose();
    cancel_         = std::move(other.cancel_);
    disposed_       = other.disposed_;
    other.disposed_ = true;
    return *this;
}

Subscription::~Subscription() { dispose(); }

void Subscription::dispose() noexcept {
    if (disposed_) return;
    disposed_ = true;
    if (!cancel_) return;
    auto cancel = std::move(cancel_);
    cancel();
}

}  // namespace eve
