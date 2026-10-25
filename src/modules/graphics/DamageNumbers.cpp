#include "graphics/DamageNumbersInternal.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace eve::graphics {
namespace {
Result<void> invalid(std::string message) {
    return Result<void>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "graphics.damageNumbers"));
}
bool validKind(DamageNumberKind kind) { return kind >= DamageNumberKind::Damage && kind <= DamageNumberKind::Miss; }
bool bounded(double value, double maximum) { return std::isfinite(value) && std::abs(value) <= maximum; }
std::string text(const DamageNumberRequest& request, const DamageNumberStyle& style) {
    if (request.kind == DamageNumberKind::Miss) return "MISS";
    double      value = request.amount;
    std::string suffix;
    if (style.compact && value >= 1000000.0) {
        value /= 1000000.0;
        suffix = "M";
    } else if (style.compact && value >= 1000.0) {
        value /= 1000.0;
        suffix = "K";
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << (request.kind == DamageNumberKind::Heal ? "+" : "-") << std::fixed << std::setprecision(style.decimals)
           << value << suffix;
    if (request.kind == DamageNumberKind::Critical) stream << '!';
    return stream.str();
}
}  // namespace

DamageNumbers::DamageNumbers() : impl_(std::make_unique<Impl>()) {
    impl_->items.reserve(capacity());
    auto& critical    = impl_->styles[1];
    critical.scale    = 1.4f;
    critical.shake    = 4.f;
    critical.g        = 0.78f;
    critical.b        = 0.15f;
    critical.lifetime = 1.35f;
    critical.pop      = 0.7f;
    auto& heal        = impl_->styles[2];
    heal.r            = 0.3f;
    heal.g            = 1.f;
    heal.b            = 0.6f;
    heal.gravity      = 0.f;
    heal.velocityY    = -65.f;
    auto& miss        = impl_->styles[3];
    miss.r            = 0.65f;
    miss.g            = 0.75f;
    miss.b            = 0.9f;
    miss.scale        = 0.8f;
    miss.mergeWindow  = 0.0;
}
DamageNumbers::~DamageNumbers() = default;

Result<void> DamageNumbers::setStyle(DamageNumberKind kind, const DamageNumberStyle& style) {
    if (!validKind(kind) || !bounded(style.lifetime, 60.0) || style.lifetime <= 0.0 ||
        !bounded(style.velocityY, 10000.0) || !bounded(style.gravity, 10000.0) || !bounded(style.scale, 20.0) ||
        style.scale <= 0.f || !bounded(style.pop, 5.0) || style.pop < 0.f || !bounded(style.shake, 100.0) ||
        style.shake < 0.f || !bounded(style.r, 1.0) || style.r < 0.f || !bounded(style.g, 1.0) || style.g < 0.f ||
        !bounded(style.b, 1.0) || style.b < 0.f || !bounded(style.outline, 8.0) || style.outline < 0.f ||
        !bounded(style.fadeIn, style.lifetime) || style.fadeIn < 0.0 || !bounded(style.fadeOut, style.lifetime) ||
        style.fadeOut < 0.0 || !bounded(style.mergeWindow, style.lifetime) || style.mergeWindow < 0.0 ||
        !bounded(style.mergeRadius, 4096.0) || style.mergeRadius < 0.f || style.decimals < 0 || style.decimals > 3)
        return invalid("Invalid popup style");
    impl_->styles[static_cast<std::size_t>(kind)] = style;
    return Result<void>::success();
}
Result<DamageNumberStyle> DamageNumbers::style(DamageNumberKind kind) const {
    if (!validKind(kind)) return Result<DamageNumberStyle>::failure(invalid("Invalid popup kind").status());
    return Result<DamageNumberStyle>::success(impl_->styles[static_cast<std::size_t>(kind)]);
}
Result<DamageNumberAdmission> DamageNumbers::spawn(const DamageNumberRequest& request) {
    if (!validKind(request.kind) || !bounded(request.amount, 1.0e12) || request.amount < 0.0 ||
        !bounded(request.x, 1.0e7) || !bounded(request.y, 1.0e7) || !bounded(request.velocityX, 10000.0) ||
        request.group.size() > 128)
        return Result<DamageNumberAdmission>::failure(invalid("Invalid popup request").status());
    const auto style = impl_->styles[static_cast<std::size_t>(request.kind)];
    if (!request.group.empty() && request.kind != DamageNumberKind::Miss) {
        for (auto& p : impl_->items) {
            const double dx = static_cast<double>(p.request.x) - request.x;
            const double dy = static_cast<double>(p.request.y) - request.y;
            if (p.request.kind == request.kind && p.request.group == request.group && p.style.mergeWindow > 0.0 &&
                p.age < p.style.mergeWindow && dx * dx + dy * dy <= p.style.mergeRadius * p.style.mergeRadius &&
                p.request.amount + request.amount <= 1.0e12) {
                auto merged = p.request;
                merged.amount += request.amount;
                auto label = text(merged, p.style);
                p.request  = std::move(merged);
                p.text     = std::move(label);
                return Result<DamageNumberAdmission>::success(DamageNumberAdmission::Merged);
            }
        }
    }
    // Explicit deterministic lanes reduce exact overlap without a per-frame pairwise solver.
    Impl::Popup popup{request, style, text(request, style), 0.0,
                      static_cast<float>(static_cast<int>(impl_->sequence % 5) - 2) * 7.f};
    auto        admission = DamageNumberAdmission::Spawned;
    if (impl_->items.size() == capacity()) {
        impl_->items.erase(impl_->items.begin());
        admission = DamageNumberAdmission::ReplacedOldest;
    }
    impl_->items.push_back(std::move(popup));
    impl_->sequence = (impl_->sequence + 1) % 5;
    return Result<DamageNumberAdmission>::success(admission);
}
Result<void> DamageNumbers::update(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return invalid("dt must be finite and nonnegative");
    for (auto& p : impl_->items) p.age = std::min(static_cast<double>(p.style.lifetime), p.age + seconds);
    std::erase_if(impl_->items, [](const auto& p) { return p.age >= p.style.lifetime; });
    return Result<void>::success();
}
void DamageNumbers::clear() {
    impl_->items.clear();
    impl_->sequence = 0;
}
std::size_t        DamageNumbers::count() const { return impl_->items.size(); }
DamageNumberSample DamageNumbers::Impl::sample(const Popup& p) {
    const auto&  s       = p.style;
    const float  t       = static_cast<float>(p.age);
    const double fadeIn  = s.fadeIn > 0.0 ? std::min(1.0, p.age / s.fadeIn) : 1.0;
    const double fadeOut = s.fadeOut > 0.0 ? std::min(1.0, (s.lifetime - p.age) / s.fadeOut) : 1.0;
    const float  pulse   = std::max(0.f, 1.f - t / 0.18f);
    const float  shake   = std::sin(t * 110.f) * s.shake * std::max(0.f, 1.f - t / 0.24f);
    return {p.text,
            p.request.x + p.lane + p.request.velocityX * t + shake,
            p.request.y + s.velocityY * t + 0.5f * s.gravity * t * t,
            s.scale * (1.f + s.pop * pulse),
            static_cast<float>(fadeIn * fadeOut),
            s.r,
            s.g,
            s.b,
            s.outline};
}
std::vector<DamageNumberSample> DamageNumbers::snapshot() const {
    std::vector<DamageNumberSample> samples;
    samples.reserve(count());
    for (const auto& p : impl_->items) samples.push_back(Impl::sample(p));
    return samples;
}
}  // namespace eve::graphics
