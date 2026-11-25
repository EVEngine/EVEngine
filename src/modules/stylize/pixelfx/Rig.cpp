// eve-hot-path
// Runtime sampling/raster uses typed state; Value codecs below run only during cold preparation.
// Native pose solver/raster adapted from pixelfx core/rig.js, MIT. Copyright (c) 2026 Patch.
// Track interpolation adapts core/act.js and Hollow Crown held-pose timing, MIT (c) 2026 rehan-remade.
#include "stylize/pixelfx/Rig.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <type_traits>
#include <variant>
#include "common/Assert.h"
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx::rig {
namespace {
constexpr double DEG = PI / 180, baseTurn = 36 * DEG;
Vec              add(Vec a, Vec b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
Vec              sub(Vec a, Vec b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
Vec              mul(Vec a, double s) { return {a[0] * s, a[1] * s, a[2] * s}; }
double           dot(Vec a, Vec b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec    cross(Vec a, Vec b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
double len(Vec a) { return jsHypot3(a[0], a[1], a[2]); }

Vec unit(Vec a) {
    const double n = len(a);
    return mul(a, 1 / (n == 0 ? 1e-6 : n));
}
Mat rotY(double t) {
    const double c = std::cos(t), s = std::sin(t);
    return {c, 0, s, 0, 1, 0, -s, 0, c};
}
Mat rotZ(double t) {
    const double c = std::cos(t), s = std::sin(t);
    return {c, -s, 0, s, c, 0, 0, 0, 1};
}
Mat mm(const Mat& A, const Mat& B) {
    Mat o{};
    for (size_t r = 0; r < 3; ++r)
        for (size_t c = 0; c < 3; ++c)
            o[r * 3 + c] = A[r * 3] * B[c] + A[r * 3 + 1] * B[3 + c] + A[r * 3 + 2] * B[6 + c];
    return o;
}
Vec mv(const Mat& M, Vec v) {
    return {M[0] * v[0] + M[1] * v[1] + M[2] * v[2], M[3] * v[0] + M[4] * v[1] + M[5] * v[2],
            M[6] * v[0] + M[7] * v[1] + M[8] * v[2]};
}
Limb ik(Vec a, Vec target, double L1, double L2, Vec pole) {
    Vec          to    = sub(target, a);
    double       d     = len(to);
    const double reach = L1 + L2 - .02;
    if (d > reach) {
        to = mul(to, reach / d);
        d  = reach;
    }
    if (d == 0) {
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        return {{nan, nan, nan}, {nan, nan, nan}, {}, {}};
    }
    const auto   u     = unit(to);
    const double along = (L1 * L1 - L2 * L2 + d * d) / (2 * d), h = std::sqrt(std::max(0., L1 * L1 - along * along));
    auto         v = sub(pole, mul(u, dot(pole, u)));
    v              = len(v) < 1e-4 ? unit(cross(u, {0, 0, 1})) : unit(v);
    return {add(a, add(mul(u, along), mul(v, h))), add(a, to), u, v};
}
Frame boneFrame(Vec a, Vec b, Vec pole) {
    const auto x = unit(sub(b, a));
    auto       y = sub(pole, mul(x, dot(pole, x)));
    y            = len(y) < 1e-4 ? unit(cross(x, {0, 0, 1})) : unit(y);
    const auto z = cross(x, y);
    return {a, {x[0], y[0], z[0], x[1], y[1], z[1], x[2], y[2], z[2]}};
}
double grip(Weapon w) {
    switch (w) {
        case Weapon::Sword: return -4.4;
        case Weapon::Greatsword: return -4.8;
        case Weapon::Spear: return -12;
        case Weapon::Hammer: return -7;
        case Weapon::Scythe: return -11;
        case Weapon::Launcher: return -9;
        default: return 0;
    }
}
bool       across(Weapon w) { return w == Weapon::Bow || w == Weapon::Pistol; }
Diagnostic invalid(std::string text, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(text), "/pose" + path);
}
using Member = std::variant<double Pose::*, std::optional<double> Pose::*, Weapon Pose::*, bool Pose::*, int Pose::*>;
struct Field {
    PoseField             id;
    const char*           name;
    Member                member;
    bool                  stand   = false;
    std::optional<double> release = std::nullopt;
};
constexpr Field                            fields[]{{PoseField::Lean, "lean", &Pose::lean, true},
                                                    {PoseField::Crouch, "crouch", &Pose::crouch, true},
                                                    {PoseField::Yaw, "yaw", &Pose::yaw, true},
                                                    {PoseField::Fx, "fx", &Pose::fx, true},
                                                    {PoseField::Fy, "fy", &Pose::fy, true},
                                                    {PoseField::Bx, "bx", &Pose::bx, true},
                                                    {PoseField::By, "by", &Pose::by, true},
                                                    {PoseField::Ffx, "ffx", &Pose::ffx, true},
                                                    {PoseField::Ffy, "ffy", &Pose::ffy, true},
                                                    {PoseField::Fbx, "fbx", &Pose::fbx, true},
                                                    {PoseField::Fby, "fby", &Pose::fby, true},
                                                    {PoseField::X, "x", &Pose::x},
                                                    {PoseField::Y, "y", &Pose::y},
                                                    {PoseField::Sa, "sa", &Pose::sa},
                                                    {PoseField::Wl, "wl", &Pose::wl, false, 1},
                                                    {PoseField::Wz, "wz", &Pose::wz},
                                                    {PoseField::Twist, "twist", &Pose::twist},
                                                    {PoseField::Hy, "hy", &Pose::hy, false, 0},
                                                    {PoseField::Hx, "hx", &Pose::hx, false, 0},
                                                    {PoseField::Sb, "sb", &Pose::sb},
                                                    {PoseField::Turn, "turn", &Pose::turn},
                                                    {PoseField::Fz, "fz", &Pose::fz},
                                                    {PoseField::Bz, "bz", &Pose::bz},
                                                    {PoseField::Fzz, "fzz", &Pose::fzz},
                                                    {PoseField::Bzz, "bzz", &Pose::bzz},
                                                    {PoseField::Weapon, "weapon", &Pose::weapon},
                                                    {PoseField::OffWeapon, "offWeapon", &Pose::offWeapon},
                                                    {PoseField::Two, "two", &Pose::two},
                                                    {PoseField::ArmsFront, "armsFront", &Pose::armsFront},
                                                    {PoseField::ToeF, "toeF", &Pose::toeF},
                                                    {PoseField::ToeB, "toeB", &Pose::toeB},
                                                    {PoseField::Pull, "pull", &Pose::pull},
                                                    {PoseField::Nocked, "nocked", &Pose::nocked},
                                                    {PoseField::Ghost, "ghost", &Pose::ghost},
                                                    {PoseField::Face, "face", &Pose::face}};
constexpr std::array<std::string_view, 12> weaponNames{"none",   "sword", "greatsword", "dagger", "spear",    "hammer",
                                                       "scythe", "claw",  "bow",        "pistol", "launcher", "handle"};
std::string                                escape(std::string_view s) {
    std::string out;
    for (char c : s) out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
Result<void> validate(const Pose& q) {
    for (const auto& field : fields) {
        auto valid = std::visit(
            [&](auto member) -> Result<void> {
                const auto& value = q.*member;
                using T           = std::remove_cvref_t<decltype(value)>;
                double number     = 0;
                bool   numeric    = false;
                if constexpr (std::is_same_v<T, double>) {
                    number  = value;
                    numeric = true;
                } else if constexpr (std::is_same_v<T, std::optional<double>>) {
                    if (value) {
                        number  = *value;
                        numeric = true;
                    }
                } else if constexpr (std::is_same_v<T, Weapon>) {
                    if (int(value) < 0 || int(value) >= int(weaponNames.size()))
                        return Result<void>::failure(invalid("Unknown pose weapon", "/" + std::string(field.name)));
                } else if constexpr (std::is_same_v<T, int>) {
                    if (value != 1 && value != -1)
                        return Result<void>::failure(invalid("Pose face must be -1 or 1", "/face"));
                }
                if (numeric && (!std::isfinite(number) || std::abs(number) > 4096))
                    return Result<void>::failure(
                        invalid("Pose value must be finite within -4096..4096", "/" + std::string(field.name)));
                return Result<void>::success();
            },
            field.member);
        if (!valid) return valid;
    }
    if (q.two && q.weapon != Weapon::None && grip(q.weapon) == 0)
        return Result<void>::failure(invalid("Selected weapon has no two-hand grip", "/two"));
    if (q.wl < 0 || q.wl > 1) return Result<void>::failure(invalid("Weapon projection must be within 0..1", "/wl"));
    return Result<void>::success();
}
}  // namespace
std::span<const std::string_view> poseFieldNames() {
    static const auto names = [] {
        std::array<std::string_view, size_t(PoseField::Count)> out{};
        for (size_t i = 0; i < out.size(); ++i) {
            EV_ASSERT(size_t(fields[i].id) == i, "Pose field identities must match their sole binding table");
            out[i] = fields[i].name;
        }
        return out;
    }();
    return names;
}
static_assert(std::size(fields) == size_t(PoseField::Count) && size_t(PoseField::Count) < 64);
static_assert([] {
    for (size_t i = 0; i < std::size(fields); ++i)
        if (size_t(fields[i].id) != i) return false;
    return true;
}());
AuthoredPose rasterCacheKey(const AuthoredPose& input) {
    AuthoredPose out;
    out.presence = 0;
    out.pose.y   = input.contains(PoseField::Y) && input.pose.y < -.5 ? -1 : 0;
    for (const auto& f : fields) {
        if (f.id == PoseField::X || f.id == PoseField::Y || f.id == PoseField::Ghost || !input.contains(f.id)) continue;
        out.presence |= uint64_t(1) << size_t(f.id);
        const double scale = f.id == PoseField::Sa || f.id == PoseField::Sb || f.id == PoseField::Wl ||
                                     f.id == PoseField::Yaw || f.id == PoseField::Turn
                                 ? 20
                                 : 1;
        std::visit(
            [&](auto member) {
                using M           = decltype(input.pose.*member);
                const auto& value = input.pose.*member;
                if constexpr (std::is_same_v<std::remove_cvref_t<M>, double>)
                    out.pose.*member = std::floor(value * scale + .5);
                else if constexpr (std::is_same_v<std::remove_cvref_t<M>, std::optional<double>>) {
                    if (value)
                        out.pose.*member = std::floor(*value * scale + .5);
                    else
                        out.presence &= ~(uint64_t(1) << size_t(f.id));
                } else
                    out.pose.*member = value;
            },
            f.member);
    }
    return out;
}
AuthoredPose standPose() {
    uint64_t mask = 0;
    for (size_t i = 0; i < std::size(fields); ++i)
        if (fields[i].stand) mask |= uint64_t(1) << i;
    return {Pose{}, mask};
}
struct PoseTrack::State {
    enum class Ease { Smooth, Out, In, Line, Snap };
    struct Key {
        double   time;
        Pose     pose;
        uint64_t authored = 0;
        bool     present(size_t index) const { return fields[index].stand || (authored & (uint64_t(1) << index)); }
        Ease     arrival = Ease::Smooth;
    };
    std::vector<Key> keys;
};
PoseTrack::PoseTrack(std::unique_ptr<const State> state) : state_(std::move(state)) {}
PoseTrack::~PoseTrack()                                      = default;
PoseTrack::PoseTrack(PoseTrack&&) noexcept                   = default;
PoseTrack&        PoseTrack::operator=(PoseTrack&&) noexcept = default;
Result<PoseTrack> PoseTrack::prepare(const Value& document) {
    using R            = Result<PoseTrack>;
    const auto failure = [](std::string text, std::string path) {
        return R::failure(
            Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(text), "/track" + path));
    };
    const auto* root = document.getIf<Value::Object>();
    if (!root) return failure("Expected a versioned pose track", "");
    for (const auto& [key, value] : *root)
        if (key != "schema" && key != "version" && key != "keys")
            return failure("Unknown track document field", "/" + escape(key));
    const auto* schema = document.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.pixelfx.pose-track")
        return failure("Expected eve.pixelfx.pose-track schema", "/schema");
    const auto* version = document.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return R::failure(Diagnostic(DiagnosticCode::UnknownVersion, Severity::Error, "Unsupported pose-track version",
                                     "/track/version"));
    const auto* keys    = document.find("keys");
    const auto* entries = keys ? keys->getIf<Value::Array>() : nullptr;
    if (!entries || entries->empty() || entries->size() > 4096) return failure("Expected 1..4096 pose keys", "/keys");
    auto state = std::make_unique<State>();
    state->keys.reserve(entries->size());
    for (size_t i = 0; i < entries->size(); ++i) {
        const auto  path = "/keys/" + std::to_string(i);
        const auto* row  = (*entries)[i].getIf<Value::Array>();
        if (!row || (row->size() != 2 && row->size() != 3))
            return failure("Expected [time, fields, arrival?] key", path);
        const auto& t = (*row)[0];
        if (!t.isDouble() && !t.isInt64()) return failure("Expected numeric key time", path + "/0");
        const double time = t.isDouble() ? t.asDouble() : double(t.asInt());
        if (!std::isfinite(time) || std::abs(time) > 1e6 || (!state->keys.empty() && time < state->keys.back().time))
            return failure("Key times must be finite within -1e6..1e6 and nondecreasing", path + "/0");
        const auto* object = (*row)[1].getIf<Value::Object>();
        if (!object) return failure("Expected authored pose fields", path + "/1");
        const auto poseDoc = Value::object(
            {{"schema", Value("eve.pixelfx.pose")}, {"version", Value(int64_t(1))}, {"fields", (*row)[1]}});
        auto decoded = decodePose(poseDoc);
        if (!decoded) {
            const auto&       d      = decoded.status().diagnostics().front();
            const std::string suffix = d.path().starts_with("/pose/fields") ? d.path().substr(12)
                                       : d.path().starts_with("/pose")      ? d.path().substr(5)
                                                                            : d.path();
            return R::failure(Diagnostic(d.code(), d.severity(), d.message(), "/track" + path + "/1" + suffix));
        }
        State::Key key;
        key.time = time;
        key.pose = std::move(decoded).takeValue();
        for (size_t f = 0; f < std::size(fields); ++f)
            if (object->contains(fields[f].name)) key.authored |= uint64_t(1) << f;
        if (row->size() == 3 && !(*row)[2].isNull()) {
            const auto& ease = (*row)[2];
            if (!ease.isString()) return failure("Expected arrival easing name", path + "/2");
            const auto& how = ease.asString();
            if (how == "smooth")
                key.arrival = State::Ease::Smooth;
            else if (how == "out")
                key.arrival = State::Ease::Out;
            else if (how == "in")
                key.arrival = State::Ease::In;
            else if (how == "line")
                key.arrival = State::Ease::Line;
            else if (how == "snap")
                key.arrival = State::Ease::Snap;
            else
                return failure("Unknown arrival easing", path + "/2");
        }
        state->keys.push_back(std::move(key));
    }
    return R::success(PoseTrack(std::move(state)));
}
Result<Pose> PoseTrack::sample(double seconds) const {
    auto sampled = sampleAuthored(seconds);
    if (!sampled) return Result<Pose>::failure(sampled.status());
    return Result<Pose>::success(std::move(sampled).takeValue().pose);
}
Result<AuthoredPose> PoseTrack::sampleAuthored(double seconds) const {
    using R = Result<AuthoredPose>;
    if (!std::isfinite(seconds) || std::abs(seconds) > 1e12)
        return R::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                     "Track time must be finite within -1e12..1e12", "/track/time"));
    const auto& keys = state_->keys;
    if (seconds <= keys.front().time) return R::success(AuthoredPose{keys.front().pose, keys.front().authored});
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (seconds >= b.time) continue;
        if (b.arrival == State::Ease::Snap) return R::success(AuthoredPose{a.pose, a.authored});
        const auto ease = [&](double u) {
            switch (b.arrival) {
                case State::Ease::Out: return 1 - (1 - u) * (1 - u);
                case State::Ease::In: return u * u;
                case State::Ease::Line: return u;
                default: return u * u * (3 - 2 * u);
            }
        };
        const double stepped  = a.time + std::floor((seconds - a.time) * 12 + 1e-6) / 12,
                     u        = ease((stepped - a.time) / (b.time - a.time));
        Pose         out      = a.pose;
        uint64_t     presence = a.authored;
        for (size_t f = 0; f < std::size(fields); ++f)
            if (fields[f].stand) presence |= uint64_t(1) << f;
        for (size_t f = 0; f < std::size(fields); ++f) {
            const auto& field = fields[f];
            std::visit(
                [&](auto member) {
                    auto&       value  = out.*member;
                    const auto& target = b.pose.*member;
                    using T            = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, double>) {
                        if (b.present(f)) presence |= uint64_t(1) << f;
                        if (b.present(f))
                            value = a.present(f) ? value + (target - value) * u : target;
                        else if (a.present(f) && field.release)
                            value = value + (*field.release - value) * u;
                    } else if constexpr (std::is_same_v<T, std::optional<double>>) {
                        if (b.present(f)) presence |= uint64_t(1) << f;
                        if (b.present(f)) value = a.present(f) ? *value + (*target - *value) * u : target;
                    } else if constexpr (std::is_same_v<T, int>) {
                        if (b.present(f)) presence |= uint64_t(1) << f;
                        if (b.present(f)) {
                            const double v =
                                a.present(f) ? double(value) + (double(target) - double(value)) * u : double(target);
                            value = v == -1 ? -1 : 1;
                        }
                    }
                },
                field.member);
        }
        const double smooth = ease((seconds - a.time) / (b.time - a.time));
        out.x               = a.pose.x + (b.pose.x - a.pose.x) * smooth;
        out.y               = a.pose.y + (b.pose.y - a.pose.y) * smooth;
        presence |= (uint64_t(1) << size_t(PoseField::X)) | (uint64_t(1) << size_t(PoseField::Y));
        return R::success(AuthoredPose{std::move(out), presence});
    }
    return R::success(AuthoredPose{keys.back().pose, keys.back().authored});
}
Result<bool> PoseTrack::isHeld(double seconds) const {
    if (!std::isfinite(seconds) || std::abs(seconds) > 1e12)
        return Result<bool>::failure(Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error,
                                                "Track time must be finite within -1e12..1e12", "/track/time"));
    const auto& keys = state_->keys;
    if (seconds >= keys.back().time || seconds < keys.front().time) return Result<bool>::success(true);
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        const auto& a = keys[i];
        const auto& b = keys[i + 1];
        if (seconds >= b.time) continue;
        const bool held = b.time - a.time > .4 && std::abs(a.pose.fx - b.pose.fx) < 1.5 &&
                          std::abs(a.pose.fy - b.pose.fy) < 1.5 && std::abs(a.pose.bx - b.pose.bx) < 1.5 &&
                          std::abs(a.pose.by - b.pose.by) < 1.5 && std::abs(a.pose.crouch - b.pose.crouch) < 1.5 &&
                          std::abs(a.pose.lean - b.pose.lean) < 1.5 && std::abs(a.pose.x - b.pose.x) < 1.5 &&
                          std::abs(a.pose.y - b.pose.y) < 1.5;
        return Result<bool>::success(held);
    }
    return Result<bool>::success(true);
}
Value encodePose(const Pose& pose) {
    Value::Object values;
    for (const auto& field : fields)
        std::visit(
            [&](auto member) {
                const auto& v = pose.*member;
                using T       = std::remove_cvref_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::optional<double>>) {
                    if (v) values.emplace(field.name, Value(*v));
                } else if constexpr (std::is_same_v<T, Weapon>) {
                    values.emplace(field.name, Value(int(v) >= 0 && int(v) < int(weaponNames.size())
                                                         ? std::string(weaponNames[size_t(v)])
                                                         : "invalid"));
                } else if constexpr (std::is_same_v<T, int>)
                    values.emplace(field.name, Value(int64_t(v)));
                else
                    values.emplace(field.name, Value(v));
            },
            field.member);
    return Value::object({{"schema", Value("eve.pixelfx.pose")},
                          {"version", Value(int64_t(1))},
                          {"fields", Value::object(std::move(values))}});
}
Result<Pose> decodePose(const Value& document) {
    using R          = Result<Pose>;
    const auto* root = document.getIf<Value::Object>();
    if (!root) return R::failure(invalid("Expected a versioned pose object", ""));
    for (const auto& [key, value] : *root)
        if (key != "schema" && key != "version" && key != "fields")
            return R::failure(invalid("Unknown pose document field", "/" + escape(key)));
    const auto* schema = document.find("schema");
    if (!schema || !schema->isString() || schema->asString() != "eve.pixelfx.pose")
        return R::failure(invalid("Expected eve.pixelfx.pose schema", "/schema"));
    const auto* version = document.find("version");
    if (!version || !version->isInt64() || version->asInt() != 1)
        return R::failure(
            Diagnostic(DiagnosticCode::UnknownVersion, Severity::Error, "Unsupported pose version", "/pose/version"));
    const auto* input = document.find("fields");
    if (!input || !input->isObject()) return R::failure(invalid("Expected pose fields object", "/fields"));
    Pose pose;
    for (const auto& [key, value] : *input->getIf<Value::Object>()) {
        const Field* binding = nullptr;
        for (const auto& field : fields)
            if (key == field.name) binding = &field;
        if (!binding) return R::failure(invalid("Unknown pose field", "/fields/" + escape(key)));
        auto result = std::visit(
            [&](auto member) -> Result<void> {
                auto& v            = pose.*member;
                using T            = std::remove_cvref_t<decltype(v)>;
                const auto failure = [&]() {
                    return Result<void>::failure(invalid("Unexpected pose field type", "/fields/" + escape(key)));
                };
                if constexpr (std::is_same_v<T, Weapon>) {
                    if (!value.isString()) return failure();
                    const auto it = std::find(weaponNames.begin(), weaponNames.end(), value.asString());
                    if (it == weaponNames.end()) return failure();
                    v = Weapon(it - weaponNames.begin());
                } else if constexpr (std::is_same_v<T, bool>) {
                    if (!value.isBool()) return failure();
                    v = value.asBool();
                } else if constexpr (std::is_same_v<T, int>) {
                    if (!value.isInt64() || (value.asInt() != 1 && value.asInt() != -1)) return failure();
                    v = int(value.asInt());
                } else {
                    if (!value.isInt64() && !value.isDouble()) return failure();
                    const double n = value.isInt64() ? double(value.asInt()) : value.asDouble();
                    v              = n;
                }
                return Result<void>::success();
            },
            binding->member);
        if (!result) return R::failure(result.status());
    }
    auto valid = validate(pose);
    if (!valid) return R::failure(valid.status());
    return R::success(std::move(pose));
}
Result<Solved> solve(const Pose& q) {
    auto valid = validate(q);
    if (!valid) return Result<Solved>::failure(valid.status());
    Solved S;
    S.pose         = q;
    const bool   W = q.weapon != Weapon::None, OW = q.offWeapon != Weapon::None, two = q.two && W;
    const double axisZ = std::sqrt(std::max(0., 1 - q.wl * q.wl)) * q.wz;
    S.axis             = {std::cos(q.sa) * q.wl, -std::sin(q.sa) * q.wl, axisZ};
    S.side             = {-std::sin(q.sa), -std::cos(q.sa), 0};
    const double nearZ = q.fz.value_or(two ? 3 : 4.5), fwd = (q.fx + 3) / 24, farFwd = two ? 0 : (q.bx - 5) / 22;
    double       turn = baseTurn + (fwd >= 0 ? -44 * DEG * std::min(1., fwd) : 28 * DEG * std::min(1., -fwd * 1.8));
    turn += (farFwd > 0 ? 34 * DEG * std::min(1., farFwd) : 0) + q.twist * 4 * DEG;
    if (q.turn) turn = *q.turn;
    S.turn = turn;
    // The denominator is always positive: atan(lean/13) is the equivalent principal angle.
    // It avoids MSVC atan2's measured one-ULP bias at the source crouch pose's depth boundary.
    const double hipTurn = baseTurn + (turn - baseTurn) * .45, tilt = std::atan(q.lean / 13);
    const auto   Rp = mm(rotZ(-tilt * .25), rotY(-hipTurn)), Rc = mm(rotZ(-tilt), rotY(-turn));
    const Vec    ankleN{q.ffx, -q.ffy - 1.8, q.fzz.value_or(2.4)}, ankleF{q.fbx, -q.fby - 1.8, q.bzz.value_or(-2.4)};
    Vec          pelvis{q.lean * .2, 19.7 - q.crouch, 0};
    const auto   hipAt  = [&](double side) { return add(pelvis, mv(Rp, {0, -1.1, side * 2.8})); };
    const double maxLeg = (8.6 + 8.3) * .995;
    for (const auto& [side, a] : {std::pair{1., ankleN}, std::pair{-1., ankleF}}) {
        const auto   h  = hipAt(side);
        const double dx = h[0] - a[0], dz = h[2] - a[2], flat = dx * dx + dz * dz;
        if (flat < maxLeg * maxLeg) {
            const double top = a[1] + std::sqrt(maxLeg * maxLeg - flat);
            if (h[1] > top) pelvis = {pelvis[0], pelvis[1] - (h[1] - top), pelvis[2]};
        }
    }
    const auto   waist    = add(pelvis, {std::sin(tilt * .45) * 2.8, std::cos(tilt * .45) * 2.8, 0});
    const auto   chest    = add(waist, {std::sin(tilt) * 4.6, std::cos(tilt) * 4.6, 0}),
                 neck     = add(chest, mv(Rc, {0, 4, 0}));
    const double headTurn = baseTurn * .9 + (turn - baseTurn) * .35, nod = tilt * .35 + q.hy * 7 * DEG,
                 roll = q.hx * 7 * DEG;
    const auto   Rh   = mm(rotZ(-nod - roll), rotY(-headTurn));
    const auto   head = add(neck, mv(Rh, {.15, 3.1, 0}));
    const auto   pick = [&](double side, Vec target) {
        const auto base = add(chest, mv(Rc, {0, 2.5, side * 5.9})), to = sub(target, base);
        return add(base, mul(unit(to), std::min(1.8, std::max(0., len(to) - 11) * .35)));
    };
    const Vec nearT{q.fx, -q.fy, nearZ},
        farT        = two ? Vec{q.fx + S.axis[0] * grip(q.weapon), -q.fy + S.axis[1] * grip(q.weapon),
                                nearZ + S.axis[2] * grip(q.weapon)}
                          : Vec{q.bx, -q.by, q.bz.value_or(q.armsFront ? 5.5 : -4.5)};
    S.shoulderN     = pick(1, nearT);
    S.shoulderF     = pick(-1, farT);
    const auto pole = [&](double side, Vec target, Vec shoulder, std::optional<Vec> hold) {
        const double up = std::clamp((target[1] - shoulder[1] + 2) / 10, 0., 1.);
        const Vec    base{-.55 + up * 1.1, -1 + up * .9, side * (.45 + up * .5)};
        if (!hold) return base;
        const auto u     = unit(sub(target, shoulder));
        auto       best  = base;
        double     score = std::numeric_limits<double>::infinity();
        for (int i = 0; i < 16; ++i) {
            const double a = double(i) / 16 * PI * 2;
            const auto   x = unit(cross(u, {.3, 1, .2})), y = cross(u, x),
                         v    = add(mul(x, std::cos(a)), mul(y, std::sin(a)));
            const auto   A    = ik(shoulder, target, 7.6, 7.4, v);
            const auto   f    = unit(sub(A.end, A.joint));
            const double cost = std::abs(dot(f, *hold)) * .7 + (1 - dot(unit(base), v)) * .55;
            if (cost < score) {
                score = cost;
                best  = v;
            }
        }
        return best;
    };
    const std::optional<Vec> holdN = W ? std::optional<Vec>(across(q.weapon) ? S.side : S.axis) : std::nullopt;
    const std::optional<Vec> holdF = two  ? holdN
                                     : OW ? std::optional<Vec>(Vec{std::cos(q.sb), -std::sin(q.sb), 0})
                                          : std::nullopt;
    S.armN                         = ik(S.shoulderN, nearT, 7.6, 7.4, pole(1, nearT, S.shoulderN, holdN));
    S.armF                         = ik(S.shoulderF, farT, 7.6, 7.4, pole(-1, farT, S.shoulderF, holdF));
    S.hipN                         = hipAt(1);
    S.hipF                         = hipAt(-1);
    S.legN                         = ik(S.hipN, ankleN, 8.6, 8.3, mv(Rp, {1, 0, .35}));
    S.legF                         = ik(S.hipF, ankleF, 8.6, 8.3, mv(Rp, {1, 0, -.35}));
    auto&      B                   = S.frames;
    const auto set                 = [&](Bone name, Frame f) { B[size_t(name)] = f; };
    set(Bone::Pelvis, {pelvis, Rp});
    set(Bone::Waist, {waist, mm(rotZ(-tilt * .6), rotY(-(turn + hipTurn) / 2))});
    set(Bone::Chest, {chest, Rc});
    set(Bone::Head, {head, Rh});
    set(Bone::Neck, {neck, Rh});
    set(Bone::UpperN, boneFrame(S.shoulderN, S.armN.joint, S.armN.v));
    set(Bone::ForeN, boneFrame(S.armN.joint, S.armN.end, S.armN.v));
    set(Bone::UpperF, boneFrame(S.shoulderF, S.armF.joint, S.armF.v));
    set(Bone::ForeF, boneFrame(S.armF.joint, S.armF.end, S.armF.v));
    set(Bone::ThighN, boneFrame(S.hipN, S.legN.joint, S.legN.v));
    set(Bone::ShinN, boneFrame(S.legN.joint, S.legN.end, S.legN.v));
    set(Bone::ThighF, boneFrame(S.hipF, S.legF.joint, S.legF.v));
    set(Bone::ShinF, boneFrame(S.legF.joint, S.legF.end, S.legF.v));
    const auto footRotation = [&](double side, bool toe) {
        const double t = hipTurn * .6 + side * 8 * DEG;
        return mm(rotY(-std::atan2(std::sin(t), std::cos(t))), rotZ(toe ? -55 * DEG : 0));
    };
    set(Bone::FootN, {S.legN.end, footRotation(1, q.toeF)});
    set(Bone::FootF, {S.legF.end, footRotation(-1, q.toeB)});
    for (const auto& f : B) {
        for (auto v : f.t)
            if (!std::isfinite(v)) return Result<Solved>::failure(invalid("Pose has a singular limb target", ""));
        for (auto v : f.R)
            if (!std::isfinite(v)) return Result<Solved>::failure(invalid("Pose has a singular limb target", ""));
    }
    return Result<Solved>::success(std::move(S));
}
void turnAll(Solved& S, double angle) {
    EV_PARAM_CHECK(std::isfinite(angle));
    const auto R = rotY(-angle);
    const auto P = [&](Vec v) { return mv(R, v); };
    for (auto& f : S.frames) {
        f.t = P(f.t);
        f.R = mm(R, f.R);
    }
    for (auto* limb : {&S.armN, &S.armF, &S.legN, &S.legF}) {
        limb->joint = P(limb->joint);
        limb->end   = P(limb->end);
    }
    S.shoulderN = P(S.shoulderN);
    S.shoulderF = P(S.shoulderF);
    S.hipN      = P(S.hipN);
    S.hipF      = P(S.hipF);
    S.axis      = P(S.axis);
    S.side      = P(S.side);
}
std::span<const std::string_view> jointNames() {
    static constexpr std::array<std::string_view, 20> names{
        "pelvis", "waist", "chest", "neck",  "head",   "crown", "shoulderN", "elbowN", "handN",  "shoulderF",
        "elbowF", "handF", "hipN",  "kneeN", "ankleN", "toeN",  "hipF",      "kneeF",  "ankleF", "toeF"};
    return names;
}
Result<Skeleton> skeleton(const Pose& pose) {
    auto solved = solve(pose);
    if (!solved) return Result<Skeleton>::failure(solved.status());
    auto         S     = std::move(solved).takeValue();
    const double whole = pose.yaw + (pose.face == -1 ? PI : 0);
    if (whole != 0) turnAll(S, whole);
    const auto F  = [&](Bone b) -> const Frame& { return S.frames[size_t(b)]; };
    const auto at = [&](Bone b, Vec v) {
        const auto& f = F(b);
        return add(f.t, mv(f.R, v));
    };
    Skeleton out;
    out.joints = {F(Bone::Pelvis).t,
                  F(Bone::Waist).t,
                  F(Bone::Chest).t,
                  F(Bone::Neck).t,
                  F(Bone::Head).t,
                  at(Bone::Head, {0, 3.3, 0}),
                  S.shoulderN,
                  S.armN.joint,
                  S.armN.end,
                  S.shoulderF,
                  S.armF.joint,
                  S.armF.end,
                  S.hipN,
                  S.legN.joint,
                  S.legN.end,
                  at(Bone::FootN, {4.6, -1.1, 0}),
                  S.hipF,
                  S.legF.joint,
                  S.legF.end,
                  at(Bone::FootF, {4.6, -1.1, 0})};
    for (auto& joint : out.joints) joint[1] = -joint[1];
    return Result<Skeleton>::success(std::move(out));
}
double shortfall(const Solved& S) {
    const auto&  q    = S.pose;
    const double near = len(sub(S.armN.end, {q.fx, -q.fy, S.armN.end[2]})),
                 far  = q.two ? 0 : len(sub(S.armF.end, {q.bx, -q.by, S.armF.end[2]}));
    return std::max(near, far);
}
}  // namespace eve::stylize::pixelfx::rig

namespace eve::stylize::pixelfx::rig {
namespace {
constexpr int                 BW = 300, BH = 240, OX = 150, OY = 180, N = BW * BH;
const uint32_t                INK = hex(0x0e0c14), SEAM = hex(0x2a2b33), MARK = hex(0xf53cd1);
const std::array<uint32_t, 4> SHELL{hex(0xe4e6ec), hex(0xb6b9c3), hex(0x868995), hex(0x5c5e6a)},
    CORE{hex(0x6e707c), hex(0x4b4d58), hex(0x34353f), hex(0x34353f)};
const Vec      LIGHT = unit({-.55, .72, .5});
const uint32_t HI = hex(0xffffff), A = hex(0xe8edf6), B = hex(0xa4afc6), D = hex(0x68748f), GOLD = hex(0xffd84a),
               GOLDD = hex(0xb8862a), GRIP = hex(0x7a3a2a), GRIPD = hex(0x4a2018), WOOD = hex(0x8a5a32),
               WOODD = hex(0x5a3820), DARK = hex(0x3a3448), DARKD = hex(0x262033), OLIVE = hex(0x58663f),
               OLIVEA = hex(0x7d8c58), OLIVED = hex(0x3a4529);
Vec            tv(const Mat& M, Vec v) {
    return {M[0] * v[0] + M[3] * v[1] + M[6] * v[2], M[1] * v[0] + M[4] * v[1] + M[7] * v[2],
            M[2] * v[0] + M[5] * v[1] + M[8] * v[2]};
}
}  // namespace
struct Rasterizer::State {
    std::vector<float>    depth = std::vector<float>(N), tone = std::vector<float>(N);
    std::vector<int8_t>   part = std::vector<int8_t>(N), material = std::vector<int8_t>(N),
                          frame = std::vector<int8_t>(N);
    std::vector<uint32_t> color = std::vector<uint32_t>(N), marks = std::vector<uint32_t>(N);
    std::vector<int>      touched, ring, ringFrom = std::vector<int>(N, -1), tones = std::vector<int>(N, -1);
    std::array<Frame, size_t(Bone::Count)> rest{};
    bool                                   clipBody = false, clipWeapon = false, outside = false;
    int                                    currentFrame = 0;
    int                                    onlyPart     = 0;
    State() {
        auto standing = solve(Pose{});
        EV_ASSERT(standing.ok());
        rest = standing.value().frames;
        touched.reserve(2048);
        ring.reserve(1024);
    }
    void reset() {
        for (int i : touched) {
            part[size_t(i)]  = 0;
            tones[size_t(i)] = -1;
            marks[size_t(i)] = 0;
        }
        for (int i : ring) ringFrom[size_t(i)] = -1;
        touched.clear();
        ring.clear();
        outside = false;
    }
    void plot(int i, double z, double t, int p, int mat, int wx, int wy, uint32_t c) {
        if (onlyPart && p != onlyPart) return;
        if (i < 0 || i >= N || wx < -OX || wx >= BW - OX || wy > OY || wy <= OY - BH) {
            outside = true;
            return;
        }
        const auto at = size_t(i);
        if (part[at] == 0)
            touched.push_back(i);
        else if (z <= depth[at])
            return;
        depth[at]    = float(z);
        tone[at]     = float(t);
        part[at]     = int8_t(p);
        material[at] = int8_t(mat);
        frame[at]    = int8_t(currentFrame);
        color[at]    = c;
    }
    void sphere(Vec c, double r, int p, int mat) {
        const int x0 = int(std::floor(c[0] - r)), x1 = int(std::ceil(c[0] + r)), y0 = int(std::floor(c[1] - r)),
                  y1 = int(std::ceil(c[1] + r));
        for (int wy = y0; wy <= y1; ++wy)
            for (int wx = x0; wx <= x1; ++wx) {
                const double dx = wx - c[0], dy = wy - c[1], d2 = dx * dx + dy * dy;
                if (d2 > r * r || (clipBody && wy < 1.5)) continue;
                const double dz = std::sqrt(r * r - d2);
                plot((OY - wy) * BW + (OX + wx), c[2] + dz, dot({dx / r, dy / r, dz / r}, LIGHT), p, mat, wx, wy, 0);
            }
    }
    void sweep(Vec a, Vec b, double ra, double rb, int p, int mat, double bulge = 0) {
        const int n = std::max(2, int(std::ceil(len(sub(b, a)) / .4)));
        for (int k = 0; k <= n; ++k) {
            const double u = double(k) / n;
            sphere(add(a, mul(sub(b, a), u)), ra + (rb - ra) * u + bulge * std::sin(u * PI), p, mat);
        }
    }
    void ellipsoid(Vec c, const Mat& R, Vec r, int p, int mat) {
        const double big = std::max({r[0], r[1], r[2]});
        const int x0 = int(std::floor(c[0] - big)), x1 = int(std::ceil(c[0] + big)), y0 = int(std::floor(c[1] - big)),
                  y1 = int(std::ceil(c[1] + big));
        const Vec diag{1 / (r[0] * r[0]), 1 / (r[1] * r[1]), 1 / (r[2] * r[2])};
        Mat       matrix{};
        for (size_t i = 0; i < 3; ++i)
            for (size_t j = 0; j < 3; ++j)
                matrix[i * 3 + j] = R[i * 3] * diag[0] * R[j * 3] + R[i * 3 + 1] * diag[1] * R[j * 3 + 1] +
                                    R[i * 3 + 2] * diag[2] * R[j * 3 + 2];
        for (int wy = y0; wy <= y1; ++wy)
            for (int wx = x0; wx <= x1; ++wx) {
                if (clipBody && wy < 1.5) continue;
                const double dx = wx - c[0], dy = wy - c[1], qa = matrix[8], qb = 2 * (matrix[2] * dx + matrix[5] * dy),
                             qc   = matrix[0] * dx * dx + 2 * matrix[1] * dx * dy + matrix[4] * dy * dy - 1,
                             disc = qb * qb - 4 * qa * qc;
                if (disc < 0) continue;
                const double dz     = (-qb + std::sqrt(disc)) / (2 * qa);
                const auto   normal = unit({matrix[0] * dx + matrix[1] * dy + matrix[2] * dz,
                                            matrix[3] * dx + matrix[4] * dy + matrix[5] * dz,
                                            matrix[6] * dx + matrix[7] * dy + matrix[8] * dz});
                plot((OY - wy) * BW + (OX + wx), c[2] + dz, dot(normal, LIGHT), p, mat, wx, wy, 0);
            }
    }
    void use(Bone f) { currentFrame = int(f); }
    void body(const Solved& S) {
        const auto F = [&](Bone b) -> const Frame& { return S.frames[size_t(b)]; };
        use(Bone::Pelvis);
        ellipsoid(add(F(Bone::Pelvis).t, mv(F(Bone::Pelvis).R, {0, -.2, 0})), F(Bone::Pelvis).R, {2.9, 2.6, 4.2}, 1, 0);
        use(Bone::Waist);
        sweep(F(Bone::Pelvis).t, F(Bone::Chest).t, 3, 3.3, 2, 1);
        use(Bone::Chest);
        ellipsoid(add(F(Bone::Chest).t, mv(F(Bone::Chest).R, {.2, .9, 0})), F(Bone::Chest).R, {3.4, 4.2, 5.5}, 3, 0);
        use(Bone::Neck);
        sweep(F(Bone::Neck).t, add(F(Bone::Neck).t, mv(F(Bone::Head).R, {0, 1.6, 0})), 1.25, 1.15, 4, 1);
        use(Bone::Head);
        ellipsoid(F(Bone::Head).t, F(Bone::Head).R, {3, 3.3, 2.8}, 5, 0);
        ellipsoid(add(F(Bone::Head).t, mv(F(Bone::Head).R, {.85, -.45, 0})), F(Bone::Head).R, {2.4, 2.7, 2.35}, 6, 0);
        const auto arm = [&](const Limb& limb, Vec shoulder, int up, int el, int fore, int hand, Bone U, Bone L) {
            use(U);
            sphere(shoulder, 2.5, up, 0);
            sweep(shoulder, limb.joint, 2.1, 1.6, up, 0);
            sphere(limb.joint, 1.45, el, 1);
            use(L);
            sweep(limb.joint, limb.end, 1.6, 1.2, fore, 0, .15);
            sphere(limb.end, 1.9, hand, 0);
        };
        arm(S.armN, S.shoulderN, 7, 8, 9, 10, Bone::UpperN, Bone::ForeN);
        arm(S.armF, S.shoulderF, 11, 12, 13, 14, Bone::UpperF, Bone::ForeF);
        const auto leg = [&](const Limb& limb, Vec hip, int thigh, int knee, int shin, int foot, Bone T, Bone L,
                             Bone FT) {
            use(T);
            sweep(hip, limb.joint, 3.2, 2.2, thigh, 0);
            sphere(limb.joint, 1.9, knee, 1);
            use(L);
            sweep(limb.joint, limb.end, 2.15, 1.4, shin, 0, .4);
            use(FT);
            const auto& f = F(FT);
            sweep(add(f.t, mv(f.R, {-1.5, -.6, 0})), add(f.t, mv(f.R, {4.6, -1.1, 0})), 1.7, 1.25, foot, 0);
        };
        leg(S.legN, S.hipN, 15, 16, 17, 18, Bone::ThighN, Bone::ShinN, Bone::FootN);
        leg(S.legF, S.hipF, 19, 20, 21, 22, Bone::ThighF, Bone::ShinF, Bone::FootF);
    }
    struct Along {
        State& s;
        Vec    H, axis, side;
        int    part;
        Vec    at(double i, double k) const {
            return {H[0] + axis[0] * i + side[0] * k, H[1] + axis[1] * i + side[1] * k,
                    H[2] + axis[2] * i + side[2] * k};
        }
        void operator()(double i, double k, uint32_t c, double lift = 0) const {
            const auto n = cross(axis, side);
            const Vec  P{H[0] + axis[0] * i + side[0] * k + n[0] * lift, H[1] + axis[1] * i + side[1] * k + n[1] * lift,
                         H[2] + axis[2] * i + side[2] * k + n[2] * lift};
            const int  wx = int(jsRound(P[0])), wy = int(jsRound(P[1]));
            if (s.clipWeapon && wy < 0) return;
            const int index = (OY - wy) * BW + OX + wx;
            if (index < 0 || index >= N) return;
            s.currentFrame = -1;
            s.plot(index, P[2] + .05, 2, part, 2, wx, wy, c);
        }
    };
    void haft(const Along& T, double from, double to, uint32_t a, uint32_t d, double r = .8) {
        for (double i = from; i <= to; i += .4)
            for (double k = -r; k <= r + .01; k += .4) T(i, k, k > .1 ? d : a);
    }
    void blade(const Along& T, double from, double length, double w) {
        for (double i = from; i <= from + length + .01; i += .4) {
            const double r = i - from, wd = r > length - 5 ? std::max(0., (length - r) / 5 * w) : w;
            for (double k = -wd; k <= wd + .01; k += .4) T(i, k, k < -w * .4 ? HI : k > w * .4 ? D : A);
        }
    }
    void weapon(Weapon kind, const Along& T, const Pose& q, Vec off) {
        switch (kind) {
            case Weapon::None: break;
            case Weapon::Sword: {
                const double end = q.two ? -6.5 : -2.6;
                haft(T, end, 2.2, GRIP, GRIPD);
                for (double k = -1.4; k <= 1.4; k += .4) T(end - .4, k, GOLD);
                for (double k = -4.6; k <= 4.6; k += .4) {
                    T(2.6, k, std::abs(k) > 3.8 ? GOLDD : GOLD);
                    T(3, k, GOLDD);
                }
                blade(T, 3.2, 25, 2.2);
                break;
            }
            case Weapon::Greatsword:
                haft(T, -7.2, 2.2, GRIP, GRIPD);
                for (double k = -1.8; k <= 1.8; k += .4) T(-7.6, k, GOLD);
                for (double k = -6.6; k <= 6.6; k += .4) {
                    T(2.6, k, std::abs(k) > 5.6 ? GOLDD : GOLD);
                    T(3, k, GOLDD);
                }
                blade(T, 3.2, 31, 3);
                for (double i = 4; i <= 28; i += .4) T(i, 0, B);
                break;
            case Weapon::Dagger:
                haft(T, -2.4, 1.8, GRIP, GRIPD);
                for (double k = -2.6; k <= 2.6; k += .4) T(2.2, k, GOLDD);
                blade(T, 2.6, 10, 1.3);
                break;
            case Weapon::Spear:
                haft(T, -18, 36, WOOD, WOODD, .5);
                blade(T, 36, 11, 2);
                for (double k = -2.4; k <= 2.4; k += .4) T(36, k, GOLDD);
                break;
            case Weapon::Hammer:
                for (double lift : {-.8, 0., .8})
                    for (double i = -10; i <= 18; i += .4)
                        for (double k = -.8; k <= .81; k += .4) T(i, k, k > .1 ? WOODD : WOOD, lift);
                for (double i = 18; i <= 28; i += .4)
                    for (double k = -9; k <= 9; k += .4) {
                        const auto face = std::abs(k) > 7.4 ? (k < 0 ? A : D)
                                          : i < 20          ? HI
                                          : i > 26.4        ? D
                                          : std::abs(k) < 2 ? GOLD
                                                            : B;
                        T(i, k, face, 4.5);
                        T(i, k, face, -4.5);
                        if (i < 18.4 || i > 27.6 || std::abs(k) > 8.6)
                            for (double lift = -4; lift <= 4; lift += .4) T(i, k, k < 0 ? A : i > 27.6 ? D : B, lift);
                    }
                break;
            case Weapon::Scythe:
                haft(T, -16, 31, DARK, DARKD, .5);
                for (double j = 0; j <= 28; j += .4) {
                    const double u = j / 28, w = 3.2 * (1 - u) + .3, b = 31.5 - u * u * 12;
                    for (double k = -w; k <= w; k += .4) T(b + k, j + .8, k > w * .3 ? HI : k < -w * .4 ? D : B);
                }
                break;
            case Weapon::Claw:
                for (double q0 : {-2.4, 0., 2.4})
                    for (double i = 2; i <= 9.5; i += .4) {
                        const double u = (i - 2) / 7.5, k = q0 + u * u * 1.8;
                        T(i, k, u > .55 ? HI : A);
                        if (u < .6) T(i, k + .8, D);
                    }
                break;
            case Weapon::Bow: {
                for (double u = -1; u <= 1.001; u += .025) {
                    const double k = u * 17, i = 6 * (1 - u * u) - 1;
                    T(i, k, std::abs(u) > .85 ? GOLDD : WOOD);
                    T(i + .8, k, WOODD);
                }
                const auto tipA = T.at(-1, -17), tipB = T.at(-1, 17), mid = q.pull ? off : T.at(-1, 0);
                const auto line = [&](Vec start, Vec end, uint32_t c) {
                    const int n = std::max(2, int(std::ceil(len(sub(end, start)) / .4)));
                    for (int k = 0; k <= n; ++k) {
                        const auto P  = add(start, mul(sub(end, start), double(k) / n));
                        const int  wx = int(jsRound(P[0])), wy = int(jsRound(P[1]));
                        currentFrame = -1;
                        plot((OY - wy) * BW + OX + wx, P[2] + .05, 2, 23, 2, wx, wy, c);
                    }
                };
                line(tipA, mid, A);
                line(tipB, mid, A);
                if (q.nocked) {
                    line(mid, T.at(13, 0), WOOD);
                    for (double i = 9; i <= 13; i += .4)
                        for (double k = -(13 - i) * .45; k <= (13 - i) * .45; k += .4) T(i, k, HI);
                }
                break;
            }
            case Weapon::Pistol:
                for (double i = -1; i <= 9.5; i += .4)
                    for (double k = -4.2; k <= -2.4; k += .4) T(i, k, k < -3.6 ? A : k > -2.9 ? D : B);
                for (double i = -1.2; i <= 1.2; i += .4)
                    for (double k = -2.4; k <= 2.4; k += .4) T(i, k, GRIPD);
                T(9, -4.6, HI);
                break;
            case Weapon::Launcher:
                for (double i = -14; i <= 22; i += .4)
                    for (double k = -6; k <= -1; k += .4)
                        T(i, k, k < -4.8 ? OLIVEA : k > -2.2 ? OLIVED : std::abs(i - 3) < 1.4 ? GOLD : OLIVE);
                for (double k = -6.8; k <= -.2; k += .4) {
                    T(22.4, k, B);
                    T(-14.4, k, D);
                }
                break;
            case Weapon::Handle:
                haft(T, -2.6, 6, GRIP, GRIPD);
                for (double k = -1.6; k <= 1.6; k += .4) {
                    T(-3, k, GOLD);
                    T(6.4, k, GOLD);
                }
                break;
        }
    }
    Pixels finish(const Solved& S) {
        for (int i : touched) {
            const auto p     = part[size_t(i)];
            const int  dim   = (p >= 11 && p <= 14) || (p >= 19 && p <= 22) ? 1 : 0;
            const auto v     = tone[size_t(i)];
            tones[size_t(i)] = std::min(3, (v > .8 ? 0 : v > .42 ? 1 : v > .08 ? 2 : 3) + dim);
        }
        for (int i : touched) {
            if (material[size_t(i)] == 2) continue;
            const int          t = tones[size_t(i)];
            std::array<int, 4> votes{};
            bool               same = false;
            for (int d : {-1, 1, -BW, BW}) {
                const int j = i + d;
                if (j < 0 || j >= N || part[size_t(j)] != part[size_t(i)] || tones[size_t(j)] < 0) continue;
                const int u = tones[size_t(j)];
                ++votes[size_t(u)];
                if (u == t) same = true;
            }
            if (!same && *std::max_element(votes.begin(), votes.end()) > 0)
                tones[size_t(i)] = int(std::max_element(votes.begin(), votes.end()) - votes.begin());
        }
        for (int i : touched)
            if (material[size_t(i)] != 2)
                color[size_t(i)] = (material[size_t(i)] == 1 ? CORE : SHELL)[size_t(tones[size_t(i)])];
        for (int i : touched)
            for (int d : {1, BW}) {
                const int j = i + d;
                if (j < 0 || j >= N || part[size_t(j)] == 0 || part[size_t(j)] == part[size_t(i)]) continue;
                const int    far   = depth[size_t(i)] < depth[size_t(j)] ? i : j;
                const double dz    = std::abs(double(depth[size_t(i)]) - depth[size_t(j)]);
                const bool   steel = material[size_t(i)] == 2 || material[size_t(j)] == 2;
                if (material[size_t(far)] == 2 && dz < 1) continue;
                if (steel || dz > 2.2)
                    marks[size_t(far)] = INK;
                else if (marks[size_t(far)] == 0)
                    marks[size_t(far)] = SEAM;
            }
        for (int i : touched)
            if (marks[size_t(i)]) color[size_t(i)] = marks[size_t(i)];
        const auto& chest   = S.frames[size_t(Bone::Chest)];
        const auto  chestAt = add(chest.t, mv(chest.R, {3, 1.6, -1.8}));
        if (mv(chest.R, {1, 0, 0})[2] > .15) {
            const int i = (OY - int(jsRound(chestAt[1]))) * BW + OX + int(jsRound(chestAt[0]));
            if (i >= 0 && i < N && part[size_t(i)] == 3) color[size_t(i)] = MARK;
        }
        for (int i : touched)
            for (int d : {-1, 1, -BW, BW}) {
                const int j = i + d;
                if (j >= 0 && j < N && part[size_t(j)] == 0 && ringFrom[size_t(j)] < 0) {
                    ringFrom[size_t(j)] = i;
                    ring.push_back(j);
                }
            }
        Pixels       out;
        const size_t n = touched.size() + ring.size();
        out.xs.reserve(n);
        out.ys.reserve(n);
        out.src.reserve(n);
        out.color.reserve(n);
        out.ink.reserve(n);
        out.x0 = out.y0 = 1000000000;
        out.x1 = out.y1   = -1000000000;
        const auto srcFor = [&](int i) -> int16_t {
            const int f = frame[size_t(i)];
            if (f < 0) return -1;
            const auto& a = S.frames[size_t(f)];
            const auto& r = rest[size_t(f)];
            const Vec   P{double(i % BW - OX), double(OY - i / BW), depth[size_t(i)]};
            const auto  local = tv(a.R, sub(P, a.t)), rp = add(r.t, mv(r.R, local));
            const int   c = int(jsRound(rp[0])) + 12, row = 38 - int(jsRound(rp[1]));
            return c < 0 || row < 0 || c >= 24 || row >= 38 ? int16_t(-1) : int16_t(row * 24 + c);
        };
        const auto push = [&](int i, uint32_t c, int16_t src, bool ink) {
            const int x = i % BW - OX, y = i / BW - OY;
            out.xs.push_back(int16_t(x));
            out.ys.push_back(int16_t(y));
            out.color.push_back(c);
            out.src.push_back(src);
            out.ink.push_back(ink ? 1 : 0);
            out.x0 = std::min(out.x0, x);
            out.y0 = std::min(out.y0, y);
            out.x1 = std::max(out.x1, x);
            out.y1 = std::max(out.y1, y);
        };
        double z = 0;
        for (int i : touched) {
            push(i, color[size_t(i)], srcFor(i), color[size_t(i)] == INK);
            z += depth[size_t(i)];
        }
        for (int j : ring) push(j, INK, srcFor(ringFrom[size_t(j)]), true);
        out.z = touched.empty() ? 0 : z / double(touched.size());
        return out;
    }
};
Rasterizer::Rasterizer() : state_(std::make_unique<State>()) {}
Rasterizer::~Rasterizer()                                                    = default;
Rasterizer::Rasterizer(Rasterizer&&) noexcept                                = default;
Rasterizer&                     Rasterizer::operator=(Rasterizer&&) noexcept = default;
Result<Pixels>                  Rasterizer::render(const Pose& pose) { return renderSelected(pose, 0); }
Result<std::vector<PartPixels>> Rasterizer::renderParts(const Pose& pose) {
    static constexpr std::array<std::string_view, 24> names{
        "pelvis", "waist", "chest",  "neck",   "head",  "face",  "upperN", "elbowN",
        "foreN",  "handN", "upperF", "elbowF", "foreF", "handF", "thighN", "kneeN",
        "shinN",  "footN", "thighF", "kneeF",  "shinF", "footF", "weapon", "offWeapon"};
    std::vector<PartPixels> out;
    out.reserve(names.size());
    for (size_t i = 0; i < names.size(); ++i) {
        auto result = renderSelected(pose, int(i) + 1);
        if (!result) return Result<std::vector<PartPixels>>::failure(result.status());
        auto pixels = std::move(result).takeValue();
        if (pixels.color.empty()) continue;
        double x = 0, y = 0;
        for (size_t j = 0; j < pixels.color.size(); ++j) {
            x += pixels.xs[j];
            y += pixels.ys[j];
        }
        const double count = double(pixels.color.size());
        out.push_back({names[i], int(i) + 1, std::move(pixels), x / count, y / count});
    }
    return Result<std::vector<PartPixels>>::success(std::move(out));
}
Result<Pixels> Rasterizer::renderSelected(const Pose& pose, int selectedPart) {
    auto solved = solve(pose);
    if (!solved) return Result<Pixels>::failure(solved.status());
    auto         S     = std::move(solved).takeValue();
    const double whole = pose.yaw + (pose.face == -1 ? PI : 0);
    if (whole != 0) turnAll(S, whole);
    auto& r = *state_;
    r.reset();
    r.onlyPart   = selectedPart;
    r.clipBody   = pose.y >= -.5;
    r.clipWeapon = r.clipBody;
    r.body(S);
    r.clipBody = false;
    if (pose.weapon != Weapon::None)
        r.weapon(pose.weapon, State::Along{r, S.armN.end, S.axis, S.side, 23}, pose, S.armF.end);
    if (pose.offWeapon != Weapon::None) {
        Vec axis{std::cos(pose.sb), -std::sin(pose.sb), 0}, side{-std::sin(pose.sb), -std::cos(pose.sb), 0};
        if (whole != 0) {
            const auto R = rotY(-whole);
            axis         = mv(R, axis);
            side         = mv(R, side);
        }
        r.weapon(pose.offWeapon, State::Along{r, S.armF.end, axis, side, 24}, pose, S.armF.end);
    }
    r.clipWeapon = false;
    if (r.outside) return Result<Pixels>::failure(invalid("Pose exceeds the source raster scratch grid", ""));
    return Result<Pixels>::success(r.finish(S));
}
}  // namespace eve::stylize::pixelfx::rig
