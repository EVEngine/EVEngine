// Cold configuration codec. No Value trees or field-name lookup enter frame rendering.
#include <limits>
#include <type_traits>
#include "common/Utf8Validation.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Font.h"
#include "stylize/pixelfx/Math.h"

namespace eve::stylize::pixelfx {
namespace {
template <class S, class M>
auto& field(S& s, M member) {
    if constexpr (std::is_same_v<M, double PixelFxFireSettings::*> ||
                  std::is_same_v<M, std::vector<uint32_t> PixelFxFireSettings::*> ||
                  std::is_same_v<M, uint32_t PixelFxFireSettings::*>)
        return s.fire.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxProjectileSettings::*> ||
                       std::is_same_v<M, uint32_t PixelFxProjectileSettings::*> ||
                       std::is_same_v<M, double PixelFxProjectileSettings::*> ||
                       std::is_same_v<M, int PixelFxProjectileSettings::*>)
        return s.projectile.*member;
    else if constexpr (std::is_same_v<M, double PixelFxExplosionSettings::*> ||
                       std::is_same_v<M, uint32_t PixelFxExplosionSettings::*> ||
                       std::is_same_v<M, std::vector<uint32_t> PixelFxExplosionSettings::*> ||
                       std::is_same_v<M, std::string PixelFxExplosionSettings::*>)
        return s.explosion.*member;
    else if constexpr (std::is_same_v<M, double PixelFxWaterSettings::*> ||
                       std::is_same_v<M, bool PixelFxWaterSettings::*>)
        return s.water.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxWeatherSettings::*> ||
                       std::is_same_v<M, double PixelFxWeatherSettings::*>)
        return s.weather.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxLightningSettings::*> ||
                       std::is_same_v<M, int PixelFxLightningSettings::*> ||
                       std::is_same_v<M, double PixelFxLightningSettings::*>)
        return s.lightning.*member;
    else if constexpr (std::is_same_v<M, double PixelFxIceSettings::*> || std::is_same_v<M, int PixelFxIceSettings::*>)
        return s.ice.*member;
    else if constexpr (std::is_same_v<M, std::vector<int64_t> PixelFxPickupSettings::*> ||
                       std::is_same_v<M, int PixelFxPickupSettings::*>)
        return s.pickup.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxCritterSettings::*> ||
                       std::is_same_v<M, double PixelFxCritterSettings::*> ||
                       std::is_same_v<M, bool PixelFxCritterSettings::*>)
        return s.critter.*member;
    else if constexpr ((std::is_same_v<M, uint32_t PixelFxHolySettings::*> ||
                        std::is_same_v<M, double PixelFxHolySettings::*> ||
                        std::is_same_v<M, int PixelFxHolySettings::*> ||
                        std::is_same_v<M, std::vector<uint32_t> PixelFxHolySettings::*>))
        return s.holy.*member;
    else if constexpr (std::is_same_v<M, uint32_t PixelFxImpactSettings::*> ||
                       std::is_same_v<M, double PixelFxImpactSettings::*> ||
                       std::is_same_v<M, int PixelFxImpactSettings::*> ||
                       std::is_same_v<M, std::vector<uint32_t> PixelFxImpactSettings::*>)
        return s.impact.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxScifiSettings::*> ||
                       std::is_same_v<M, uint32_t PixelFxScifiSettings::*> ||
                       std::is_same_v<M, double PixelFxScifiSettings::*>)
        return s.scifi.*member;
    else if constexpr (std::is_same_v<M, double PixelFxArcaneSettings::*>)
        return s.arcane.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxShadowSettings::*> ||
                       std::is_same_v<M, uint32_t PixelFxShadowSettings::*>)
        return s.shadow.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxNatureSettings::*> ||
                       std::is_same_v<M, double PixelFxNatureSettings::*> ||
                       std::is_same_v<M, int PixelFxNatureSettings::*>)
        return s.nature.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxSpawnSettings::*> ||
                       std::is_same_v<M, int PixelFxSpawnSettings::*> ||
                       std::is_same_v<M, double PixelFxSpawnSettings::*> ||
                       std::is_same_v<M, int64_t PixelFxSpawnSettings::*>)
        return s.spawn.*member;
    else if constexpr (std::is_same_v<M, uint32_t PixelFxStatusSettings::*> ||
                       std::is_same_v<M, std::vector<uint32_t> PixelFxStatusSettings::*>)
        return s.status.*member;
    else if constexpr (std::is_same_v<M, std::vector<uint32_t> PixelFxMeleeSettings::*> ||
                       std::is_same_v<M, uint32_t PixelFxMeleeSettings::*> ||
                       std::is_same_v<M, double PixelFxMeleeSettings::*>)
        return s.melee.*member;
    else if constexpr (std::is_same_v<M, std::string PixelFxUiSettings::*> ||
                       std::is_same_v<M, double PixelFxUiSettings::*> ||
                       std::is_same_v<M, std::vector<uint32_t> PixelFxUiSettings::*>)
        return s.ui.*member;
    else if constexpr (std::is_same_v<M, uint32_t PixelFxTransitionSettings::*> ||
                       std::is_same_v<M, std::array<double, 2> PixelFxTransitionSettings::*> ||
                       std::is_same_v<M, std::vector<uint32_t> PixelFxTransitionSettings::*>)
        return s.transition.*member;
    else
        return s.*member;
}
Diagnostic invalid(std::string text, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(text), std::move(path));
}
}  // namespace
std::span<const Option> commonOptions() {
    static const std::array<Option, 4> options{{{"seed", 0, 4294967295.0, false, 0, &PixelFxSettings::seed},
                                                {"playbackSpeed", 0, 64, true, 0, &PixelFxSettings::playbackSpeed},
                                                {"lightGain", 0, 16, false, 0, &PixelFxSettings::lightGain},
                                                {"palette", 1, 64, false, 0, &PixelFxSettings::palette}}};
    return options;
}
Value readOption(const PixelFxSettings& settings, const Option& option) {
    return std::visit(
        [&](auto member) -> Value {
            const auto& value = field(settings, member);
            using T           = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PixelFxActor>) {
                return encodeActor(value);
            } else if constexpr (std::is_same_v<T, std::vector<uint32_t>> || std::is_same_v<T, std::vector<int64_t>>) {
                Value::Array colors;
                colors.reserve(value.size());
                for (auto color : value) colors.emplace_back(int64_t(color));
                return Value::array(std::move(colors));
            } else if constexpr (std::is_same_v<T, std::array<double, 2>>) {
                return Value::array({Value(value[0]), Value(value[1])});
            } else if constexpr (std::is_same_v<T, bool>)
                return Value(value);
            else if constexpr (std::is_integral_v<T>)
                return Value(int64_t(value));
            else
                return Value(value);
        },
        option.member);
}
Result<void> writeOption(PixelFxSettings& settings, const Option& option, const Value& input) {
    const std::string path = "/options/" + std::string(option.name);
    return std::visit(
        [&](auto member) -> Result<void> {
            auto& value = field(settings, member);
            using T     = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<T, PixelFxActor>) {
                auto decoded = decodeActor(input);
                if (!decoded) return Result<void>::failure(decoded.status());
                value = std::move(decoded).takeValue();
            } else if constexpr (std::is_same_v<T, std::array<double, 2>>) {
                const auto* array = input.getIf<Value::Array>();
                if (!array || array->size() != 2)
                    return Result<void>::failure(invalid("Expected exactly two focus coordinates", path));
                T point{};
                for (size_t i = 0; i < point.size(); ++i) {
                    const auto& component     = (*array)[i];
                    const auto  componentPath = path + "/" + std::to_string(i);
                    if (!component.isInt64() && !component.isDouble())
                        return Result<void>::failure(invalid("Expected a numeric coordinate", componentPath));
                    const double number = component.isInt64() ? double(component.asInt()) : component.asDouble();
                    if (!std::isfinite(number) || number < option.minimum || number > option.maximum)
                        return Result<void>::failure(
                            invalid("Coordinate is outside its declared finite range", componentPath));
                    point[i] = number;
                }
                value = point;
            } else if constexpr (std::is_same_v<T, bool>) {
                const auto* flag = input.getIf<bool>();
                if (!flag) return Result<void>::failure(invalid("Expected a boolean", path));
                value = *flag;
            } else if constexpr (std::is_same_v<T, std::string>) {
                const auto* text = input.getIf<std::string>();
                if (!text || text->size() > size_t(option.maximum) * 4 || !eve::isValidUtf8(*text))
                    return Result<void>::failure(invalid("Expected bounded valid UTF-8 text", path));
                size_t count = 0;
                eachCodepoint(*text, [&](uint32_t) { ++count; });
                if (count < size_t(option.minimum) || count > size_t(option.maximum))
                    return Result<void>::failure(invalid("Text length is outside its Unicode scalar range", path));
                value = *text;
            } else if constexpr (std::is_same_v<T, std::vector<uint32_t>> || std::is_same_v<T, std::vector<int64_t>>) {
                const auto* array = input.getIf<Value::Array>();  // Synchronous borrowed Value nodes only.
                if (!array || array->size() < size_t(option.minimum) || array->size() > size_t(option.maximum))
                    return Result<void>::failure(
                        invalid("Expected opaque packed RGBA colors within the declared palette size range", path));
                T colors;
                colors.reserve(array->size());
                for (size_t i = 0; i < array->size(); ++i) {
                    const auto*       color = (*array)[i].getIf<int64_t>();
                    constexpr int64_t minimum =
                        std::is_same_v<T, std::vector<int64_t>> ? std::numeric_limits<int32_t>::min() : 0;
                    if (!color || *color < minimum || *color > int64_t(std::numeric_limits<uint32_t>::max()) ||
                        (uint32_t(*color) >> 24) != 255u)
                        return Result<void>::failure(invalid("Palette color must be an opaque 0xAABBGGRR integer",
                                                             path + "/" + std::to_string(i)));
                    colors.push_back(typename T::value_type(*color));
                }
                value = std::move(colors);
            } else {
                double number = 0;
                if constexpr (std::is_integral_v<T>) {
                    const auto* integer = input.getIf<int64_t>();
                    if (!integer) return Result<void>::failure(invalid("Expected an integer", path));
                    number = double(*integer);
                } else {
                    if (input.isInt64())
                        number = double(input.asInt());
                    else if (input.isDouble())
                        number = input.asDouble();
                    else
                        return Result<void>::failure(invalid("Expected a number", path));
                }
                if (!std::isfinite(number) || number < option.minimum || number > option.maximum ||
                    (option.exclusiveMinimum && number == option.minimum))
                    return Result<void>::failure(invalid("Parameter is outside its declared finite range", path));
                value = T(number);
            }
            return Result<void>::success();
        },
        option.member);
}
Result<PixelFxSettings> normalizeOptions(const Descriptor& descriptor, const PixelFxSettings& settings) {
    auto candidate = settings;
    // Only an active source declaration may publish actor geometry for drawActor.
    candidate.actor = {};
    for (auto options : {commonOptions(), descriptor.options})
        for (const auto& option : options) {
            auto valid = writeOption(candidate, option, readOption(settings, option));
            if (!valid) return Result<PixelFxSettings>::failure(valid.status());
            std::visit(
                [&](auto member) {
                    auto& value = field(candidate, member);
                    using T     = std::remove_cvref_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::vector<uint32_t>>)
                        value = fit(value, option.rampSteps ? option.rampSteps : descriptor.paletteSteps);
                    else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
                        const int steps = option.rampSteps ? option.rampSteps : descriptor.paletteSteps;
                        T         normalized(size_t(steps), 0);
                        for (int i = 0; i < steps; ++i) {
                            const auto c =
                                value[size_t(jsRound(double(i) * double(value.size() - 1) / std::max(1, steps - 1)))];
                            // Source resolveOptions converts resized ramps to hex strings before create().
                            normalized[size_t(i)] =
                                value.size() == size_t(steps) ? c : int64_t(uint32_t(c)) - 4294967296LL;
                        }
                        value = std::move(normalized);
                    }
                },
                option.member);
        }
    if ((descriptor.actorRequired || descriptor.minimumActorCells > 0) &&
        candidate.actor.source == PixelFxActorSource::None)
        return Result<PixelFxSettings>::failure(invalid("Effect requires an actor snapshot", "/options/actor/source"));
    if (std::count(candidate.actor.mask.begin(), candidate.actor.mask.end(), uint8_t(1)) < descriptor.minimumActorCells)
        return Result<PixelFxSettings>::failure(
            invalid("Effect requires occupied actor mask cells", "/options/actor/mask"));
    if (candidate.actor.source != PixelFxActorSource::None && candidate.actor.height < descriptor.minimumActorHeight)
        return Result<PixelFxSettings>::failure(
            invalid("Actor height is below this effect's source row-sampling minimum", "/options/actor/height"));
    if (descriptor.actorRule) {
        auto validated = descriptor.actorRule->get().validate(candidate.actor);
        if (!validated) return Result<PixelFxSettings>::failure(validated.status());
    }
    return Result<PixelFxSettings>::success(std::move(candidate));
}
}  // namespace eve::stylize::pixelfx
