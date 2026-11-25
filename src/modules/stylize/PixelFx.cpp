// eve-hot-path
#include "stylize/PixelFx.h"
#include <stdexcept>
#include <type_traits>
#include "common/Assert.h"
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"

namespace eve::stylize {
namespace {
using Factory = const pixelfx::Descriptor& (*)();
// Composition only: metadata, defaults and algorithms belong to the defining source.
constexpr Factory          factories[] = {pixelfx::haloDescriptor,
                                          pixelfx::sunburstDescriptor,
                                          pixelfx::laserBoltDescriptor,
                                          pixelfx::impactFrameDescriptor,
                                          pixelfx::fireBreathDescriptor,
                                          pixelfx::flamethrowerDescriptor,
                                          pixelfx::fireTornadoDescriptor,
                                          pixelfx::fireballDescriptor,
                                          pixelfx::spiritFlameDescriptor,
                                          pixelfx::torchDescriptor,
                                          pixelfx::candleDescriptor,
                                          pixelfx::brazierDescriptor,
                                          pixelfx::flamePillarDescriptor,
                                          pixelfx::campfireDescriptor,
                                          pixelfx::fireRingDescriptor,
                                          pixelfx::arrowDescriptor,
                                          pixelfx::energyDiscDescriptor,
                                          pixelfx::bulletTracerDescriptor,
                                          pixelfx::muzzlePistolDescriptor,
                                          pixelfx::muzzleShotgunDescriptor,
                                          pixelfx::muzzleSmgDescriptor,
                                          pixelfx::plasmaShotDescriptor,
                                          pixelfx::magicMissileDescriptor,
                                          pixelfx::cannonballDescriptor,
                                          pixelfx::rocketDescriptor,
                                          pixelfx::homingMissileDescriptor,
                                          pixelfx::signalFlareDescriptor,
                                          pixelfx::smallExplosionDescriptor,
                                          pixelfx::bigExplosionDescriptor,
                                          pixelfx::barrelExplosionDescriptor,
                                          pixelfx::dirtExplosionDescriptor,
                                          pixelfx::implosionDescriptor,
                                          pixelfx::fireworkBurstDescriptor,
                                          pixelfx::fireworkRocketDescriptor,
                                          pixelfx::plasmaExplosionDescriptor,
                                          pixelfx::cartoonPowDescriptor,
                                          pixelfx::grenadeDescriptor,
                                          pixelfx::smokeBombDescriptor,
                                          pixelfx::nukeDescriptor,
                                          pixelfx::ripplesDescriptor,
                                          pixelfx::rainSplashDescriptor,
                                          pixelfx::dripDescriptor,
                                          pixelfx::bubblesDescriptor,
                                          pixelfx::waterSplashDescriptor,
                                          pixelfx::fountainDescriptor,
                                          pixelfx::waterOrbDescriptor,
                                          pixelfx::whirlpoolDescriptor,
                                          pixelfx::waterJetDescriptor,
                                          pixelfx::waterfallDescriptor,
                                          pixelfx::waveCrashDescriptor,
                                          pixelfx::geyserDescriptor,
                                          pixelfx::meteorDescriptor,
                                          pixelfx::snowDescriptor,
                                          pixelfx::fogDescriptor,
                                          pixelfx::cloudsDescriptor,
                                          pixelfx::godRaysDescriptor,
                                          pixelfx::auroraDescriptor,
                                          pixelfx::rainDescriptor,
                                          pixelfx::blizzardDescriptor,
                                          pixelfx::shootingStarsDescriptor,
                                          pixelfx::fallingLeavesDescriptor,
                                          pixelfx::cherryBlossomsDescriptor,
                                          pixelfx::firefliesDescriptor,
                                          pixelfx::stormDescriptor,
                                          pixelfx::sparkArcDescriptor,
                                          pixelfx::ballLightningDescriptor,
                                          pixelfx::plasmaBallDescriptor,
                                          pixelfx::thunderclapDescriptor,
                                          pixelfx::zapHitDescriptor,
                                          pixelfx::lightningStrikeDescriptor,
                                          pixelfx::teslaCoilDescriptor,
                                          pixelfx::stormCloudDescriptor,
                                          pixelfx::electrifiedDescriptor,
                                          pixelfx::chargeUpDescriptor,
                                          pixelfx::chainLightningDescriptor,
                                          pixelfx::iceCrystalDescriptor,
                                          pixelfx::snowflakeBurstDescriptor,
                                          pixelfx::iceLanceDescriptor,
                                          pixelfx::iceSpikesDescriptor,
                                          pixelfx::icicleFallDescriptor,
                                          pixelfx::iceShatterDescriptor,
                                          pixelfx::freezeDescriptor,
                                          pixelfx::frostNovaDescriptor,
                                          pixelfx::frostBreathDescriptor,
                                          pixelfx::blizzardVortexDescriptor,
                                          pixelfx::coinSpinDescriptor,
                                          pixelfx::gemSpinDescriptor,
                                          pixelfx::crystalShardDescriptor,
                                          pixelfx::xpOrbDescriptor,
                                          pixelfx::lootBeamDescriptor,
                                          pixelfx::powerStarDescriptor,
                                          pixelfx::keyGlintDescriptor,
                                          pixelfx::heartPickupDescriptor,
                                          pixelfx::potionDescriptor,
                                          pixelfx::collectBurstDescriptor,
                                          pixelfx::chestOpenDescriptor,
                                          pixelfx::butterflyDescriptor,
                                          pixelfx::birdDescriptor,
                                          pixelfx::batDescriptor,
                                          pixelfx::eyeballDescriptor,
                                          pixelfx::fishSchoolDescriptor,
                                          pixelfx::flyingSkullDescriptor,
                                          pixelfx::frogDescriptor,
                                          pixelfx::ghostDescriptor,
                                          pixelfx::wispDescriptor,
                                          pixelfx::snakeDescriptor,
                                          pixelfx::jellyfishDescriptor,
                                          pixelfx::slimeDescriptor,
                                          pixelfx::blessingDescriptor,
                                          pixelfx::featherBurstDescriptor,
                                          pixelfx::divineShieldDescriptor,
                                          pixelfx::healDescriptor,
                                          pixelfx::healAoeDescriptor,
                                          pixelfx::holyPillarDescriptor,
                                          pixelfx::smiteDescriptor,
                                          pixelfx::resurrectionDescriptor,
                                          pixelfx::bluntHitDescriptor,
                                          pixelfx::bulletImpactDescriptor,
                                          pixelfx::critHitDescriptor,
                                          pixelfx::hitSparkDescriptor,
                                          pixelfx::hitFlashDescriptor,
                                          pixelfx::parryDescriptor,
                                          pixelfx::ricochetDescriptor,
                                          pixelfx::shieldBlockDescriptor,
                                          pixelfx::heavyLandingDescriptor,
                                          pixelfx::groundSlamDescriptor,
                                          pixelfx::jetpackDescriptor,
                                          pixelfx::teleporterPadDescriptor,
                                          pixelfx::hologramDescriptor,
                                          pixelfx::laserGridDescriptor,
                                          pixelfx::energyCoreDescriptor,
                                          pixelfx::scannerDescriptor,
                                          pixelfx::forceFieldDescriptor,
                                          pixelfx::tractorBeamDescriptor,
                                          pixelfx::warpOutDescriptor,
                                          pixelfx::warpInDescriptor,
                                          pixelfx::empBlastDescriptor,
                                          pixelfx::glitchDescriptor,
                                          pixelfx::runeCircleDescriptor,
                                          pixelfx::starFallDescriptor,
                                          pixelfx::manaBurstDescriptor,
                                          pixelfx::spellChargeDescriptor,
                                          pixelfx::arcaneOrbDescriptor,
                                          pixelfx::arcaneBeamDescriptor,
                                          pixelfx::arcaneMissilesDescriptor,
                                          pixelfx::blinkInDescriptor,
                                          pixelfx::blinkOutDescriptor,
                                          pixelfx::magicCircleCastDescriptor,
                                          pixelfx::portalDescriptor,
                                          pixelfx::timeStopDescriptor,
                                          pixelfx::blackHoleDescriptor,
                                          pixelfx::curseDescriptor,
                                          pixelfx::darkExplosionDescriptor,
                                          pixelfx::eclipseDescriptor,
                                          pixelfx::graspingHandsDescriptor,
                                          pixelfx::hexSigilDescriptor,
                                          pixelfx::lifeStealDescriptor,
                                          pixelfx::necroSkullsDescriptor,
                                          pixelfx::shadowFlamesDescriptor,
                                          pixelfx::shadowTendrilsDescriptor,
                                          pixelfx::soulDrainDescriptor,
                                          pixelfx::voidOrbDescriptor,
                                          pixelfx::acidSplashDescriptor,
                                          pixelfx::boulderDescriptor,
                                          pixelfx::earthSpikeDescriptor,
                                          pixelfx::healingBloomDescriptor,
                                          pixelfx::leafStormDescriptor,
                                          pixelfx::poisonCloudDescriptor,
                                          pixelfx::quakeDescriptor,
                                          pixelfx::rootSnareDescriptor,
                                          pixelfx::sporeBurstDescriptor,
                                          pixelfx::thornsDescriptor,
                                          pixelfx::tornadoDescriptor,
                                          pixelfx::toxicPuddleDescriptor,
                                          pixelfx::vineWhipDescriptor,
                                          pixelfx::coinDropDescriptor,
                                          pixelfx::disintegrateDescriptor,
                                          pixelfx::dissolveBurnDescriptor,
                                          pixelfx::eggHatchDescriptor,
                                          pixelfx::poofDescriptor,
                                          pixelfx::respawnBeamDescriptor,
                                          pixelfx::soulRiseDescriptor,
                                          pixelfx::shatterDeathDescriptor,
                                          pixelfx::spawnPortalDescriptor,
                                          pixelfx::summonRiseDescriptor,
                                          pixelfx::hasteDescriptor,
                                          pixelfx::slowedDescriptor,
                                          pixelfx::bleedingDescriptor,
                                          pixelfx::stunnedDescriptor,
                                          pixelfx::sleepDescriptor,
                                          pixelfx::confusedDescriptor,
                                          pixelfx::charmedDescriptor,
                                          pixelfx::cursedDescriptor,
                                          pixelfx::burningDescriptor,
                                          pixelfx::poisonedDescriptor,
                                          pixelfx::rageDescriptor,
                                          pixelfx::petrifiedDescriptor,
                                          pixelfx::frozenDescriptor,
                                          pixelfx::clawSwipeDescriptor,
                                          pixelfx::spearSweepDescriptor,
                                          pixelfx::thrustDescriptor,
                                          pixelfx::swordSlashDescriptor,
                                          pixelfx::doubleSlashDescriptor,
                                          pixelfx::daggerFlurryDescriptor,
                                          pixelfx::uppercutDescriptor,
                                          pixelfx::spinSlashDescriptor,
                                          pixelfx::whipCrackDescriptor,
                                          pixelfx::scytheReapDescriptor,
                                          pixelfx::heavyCleaveDescriptor,
                                          pixelfx::hammerSmashDescriptor,
                                          pixelfx::damageNumberDescriptor,
                                          pixelfx::comboCounterDescriptor,
                                          pixelfx::exclamationDescriptor,
                                          pixelfx::critNumberDescriptor,
                                          pixelfx::healthBarHitDescriptor,
                                          pixelfx::heartLostDescriptor,
                                          pixelfx::countdownDescriptor,
                                          pixelfx::coinCollectDescriptor,
                                          pixelfx::confettiDescriptor,
                                          pixelfx::achievementDescriptor,
                                          pixelfx::questCompleteDescriptor,
                                          pixelfx::levelUpDescriptor,
                                          pixelfx::blindsDescriptor,
                                          pixelfx::crtOffDescriptor,
                                          pixelfx::pixelDissolveDescriptor,
                                          pixelfx::irisDescriptor,
                                          pixelfx::starIrisDescriptor,
                                          pixelfx::diamondWipeDescriptor,
                                          pixelfx::glitchWipeDescriptor,
                                          pixelfx::slashWipeDescriptor,
                                          pixelfx::burnAwayDescriptor,
                                          pixelfx::shatterWipeDescriptor,
                                          pixelfx::arrowShotDescriptor,
                                          pixelfx::pistolShotsDescriptor,
                                          pixelfx::fireballThrowDescriptor,
                                          pixelfx::iceLanceThrowDescriptor,
                                          pixelfx::rocketLauncherDescriptor,
                                          pixelfx::lightningCallDescriptor,
                                          pixelfx::meteorCallDescriptor,
                                          pixelfx::holySmiteDescriptor,
                                          pixelfx::dashStrikeDescriptor,
                                          pixelfx::earthStompDescriptor,
                                          pixelfx::groundPoundDescriptor,
                                          pixelfx::swordComboDescriptor};
const pixelfx::Descriptor& findDescriptor(PixelFxKind kind) {
    for (auto factory : factories)
        if (factory().info.kind == kind) return factory();
    throw std::invalid_argument("PixelFx: unknown effect kind");
}
Diagnostic configDiagnostic(std::string message, std::string path) {
    return Diagnostic(DiagnosticCode::InvalidArgument, Severity::Error, std::move(message), std::move(path));
}
}  // namespace
namespace pixelfx {
const Descriptor& descriptorFor(PixelFxKind kind) { return findDescriptor(kind); }
}  // namespace pixelfx
struct PixelFxPreparedConfig::State {
    std::shared_ptr<const pixelfx::Program> program;
    std::unique_ptr<pixelfx::PlaybackState> playback;
};
PixelFxPreparedConfig::PixelFxPreparedConfig(PixelFxKind kind, PixelFxSettings settings) {
    const auto& descriptor = findDescriptor(kind);
    auto        program    = pixelfx::makeProgram(descriptor, std::move(settings));
    auto        playback   = pixelfx::makePlayback(*program);
    state_                 = std::make_unique<State>(State{std::move(program), std::move(playback)});
}
struct PixelFxPreparedSeek::State {
    std::shared_ptr<const pixelfx::Program> program;
    std::unique_ptr<pixelfx::PlaybackState> playback;
};
PixelFxPreparedSeek::PixelFxPreparedSeek(std::shared_ptr<const pixelfx::Program> program,
                                         std::unique_ptr<pixelfx::PlaybackState> playback)
    : state_(std::make_unique<State>(State{std::move(program), std::move(playback)})) {}
PixelFxPreparedSeek::~PixelFxPreparedSeek()                                               = default;
PixelFxPreparedSeek::PixelFxPreparedSeek(PixelFxPreparedSeek&&) noexcept                  = default;
PixelFxPreparedSeek& PixelFxPreparedSeek::operator=(PixelFxPreparedSeek&&) noexcept       = default;
PixelFxPreparedConfig::~PixelFxPreparedConfig()                                           = default;
PixelFxPreparedConfig::PixelFxPreparedConfig(PixelFxPreparedConfig&&) noexcept            = default;
PixelFxPreparedConfig& PixelFxPreparedConfig::operator=(PixelFxPreparedConfig&&) noexcept = default;

std::span<const PixelFxInfo> PixelFx::catalogue() {
    static const auto infos = [] {
        std::array<PixelFxInfo, std::size(factories)> result{};
        for (size_t i = 0; i < result.size(); ++i) result[i] = factories[i]().info;
        return result;
    }();
    return infos;
}
PixelFxSettings PixelFx::defaults(PixelFxKind kind) {
    PixelFxSettings settings;
    findDescriptor(kind).defaults(settings);
    return settings;
}
PixelFx::PixelFx(PixelFxKind kind)
    : descriptor_(&findDescriptor(kind)),
      info_(descriptor_->info),
      color_(size_t(info_.width) * size_t(info_.height)),
      light_(color_.size()) {
    auto prepared = prepareConfiguration(kind, defaults(kind));
    if (!prepared) throw std::invalid_argument(prepared.status().describe());
    auto applied = commitConfiguration(std::move(prepared).takeValue());
    if (!applied) throw std::invalid_argument(applied.status().describe());
}
PixelFx::~PixelFx()                                    = default;
PixelFx::PixelFx(PixelFx&&) noexcept                   = default;
PixelFx&        PixelFx::operator=(PixelFx&&) noexcept = default;
PixelFxSettings PixelFx::settings() const { return program_->settings; }

Result<PixelFxPreparedConfig> PixelFx::prepareConfiguration(PixelFxKind kind, const PixelFxSettings& s) {
    using PreparedResult                  = Result<PixelFxPreparedConfig>;
    const pixelfx::Descriptor* descriptor = nullptr;
    for (auto factory : factories)
        if (factory().info.kind == kind) {
            descriptor = &factory();
            break;
        }
    if (!descriptor)
        return PreparedResult::failure(
            Diagnostic(DiagnosticCode::NotFound, Severity::Error, "Unknown PixelFx effect kind", "/effect"));
    auto normalized = pixelfx::normalizeOptions(*descriptor, s);
    if (!normalized) return PreparedResult::failure(normalized.status());
    return PreparedResult::success(PixelFxPreparedConfig(kind, std::move(normalized).takeValue()));
}
Result<void> PixelFx::commitConfiguration(PixelFxPreparedConfig&& prepared) {
    if (!prepared.state_)
        return Result<void>::failure(configDiagnostic("Configuration candidate was already consumed", "/candidate"));
    if (prepared.state_->program->descriptor.info.kind != info_.kind)
        return Result<void>::failure(
            configDiagnostic("Configuration candidate belongs to a different effect", "/effect"));
    program_  = std::move(prepared.state_->program);
    playback_ = std::move(prepared.state_->playback);
    prepared.state_.reset();
    return Result<void>::success();
}
void PixelFx::configure(const PixelFxSettings& settings) {
    auto prepared = prepareConfiguration(info_.kind, settings);
    if (!prepared) throw std::invalid_argument(prepared.status().describe());
    auto committed = commitConfiguration(std::move(prepared).takeValue());
    if (!committed) throw std::invalid_argument(committed.status().describe());
}
Result<void> PixelFx::drawActor(std::span<uint32_t> target) const {
    if (target.size() != color_.size())
        return Result<void>::failure(configDiagnostic("Actor plane must match the effect dimensions", "/target"));
    std::fill(target.begin(), target.end(), 0u);
    const auto& a = program_->settings.actor;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            const int px = int(std::floor(a.x + x)) + info_.originX, py = int(std::floor(a.y + y)) + info_.originY;
            if (px >= 0 && py >= 0 && px < info_.width && py < info_.height)
                target[size_t(py) * size_t(info_.width) + size_t(px)] =
                    a.pixels[size_t(y) * size_t(a.width) + size_t(x)];
        }
    return Result<void>::success();
}
PixelFxActorVisibility PixelFx::actorVisibility() const {
    const double time =
        info_.loop ? std::fmod(playback_->time, info_.duration) : std::min(playback_->time, info_.duration);
    if (playback_->kernel) return playback_->kernel->actorVisibility(time);
    return program_->analytic ? program_->analytic->actorVisibility(time) : PixelFxActorVisibility::Visible;
}
PixelFxInfo PixelFx::info() const { return info_; }
bool        PixelFx::isSimulated() const { return bool(descriptor_->create || descriptor_->prepareSimulation); }
uint64_t    PixelFx::activeTickCount() const { return playback_->kernel ? playback_->ticks : 0; }
double      PixelFx::effectTime() const { return playback_->time; }
bool        PixelFx::done() const {
    return (!info_.loop && playback_->time >= info_.duration) ||
           (playback_->stopAt && playback_->time - *playback_->stopAt >= 0.3);
}
void PixelFx::stop() {
    if (!playback_->stopAt) playback_->stopAt = playback_->time;
}
Result<void> PixelFx::advance(double deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0 || deltaSeconds > 60)
        return Result<void>::failure(configDiagnostic("Playback delta must be finite and in [0,60]", "/deltaSeconds"));
    const double delta = deltaSeconds * program_->settings.playbackSpeed - playback_->compensation;
    const double next  = playback_->time + delta;
    if (!std::isfinite(next) || next > 1e12)
        return Result<void>::failure(configDiagnostic("Playback effect time exceeds 1e12", "/time"));
    const double compensation = (next - playback_->time) - delta;
    pixelfx::advanceTo(*playback_, next);
    playback_->compensation = compensation;
    return Result<void>::success();
}
Result<PixelFxPreparedSeek> PixelFx::prepareSeek(double effectSeconds) const {
    using R = Result<PixelFxPreparedSeek>;
    if (!std::isfinite(effectSeconds) || effectSeconds < 0 || effectSeconds > 1e12)
        return R::failure(configDiagnostic("Seek effect time must be finite and in [0,1e12]", "/effectSeconds"));
    if (isSimulated() && pixelfx::fixedTicks(effectSeconds) > 1000000)
        return R::failure(Diagnostic(DiagnosticCode::Unsupported, Severity::Error,
                                     "Seek exceeds one million active fixed ticks", "/effectSeconds"));
    auto playback = pixelfx::makePlayback(*program_);
    pixelfx::advanceTo(*playback, effectSeconds);
    return R::success(PixelFxPreparedSeek(program_, std::move(playback)));
}
Result<void> PixelFx::commitSeek(PixelFxPreparedSeek&& candidate) {
    if (!candidate.state_)
        return Result<void>::failure(configDiagnostic("Seek candidate was already consumed", "/candidate"));
    if (candidate.state_->program != program_)
        return Result<void>::failure(Diagnostic(DiagnosticCode::StaleHandle, Severity::Error,
                                                "Seek belongs to a different instance or configuration generation",
                                                "/program"));
    playback_ = std::move(candidate.state_->playback);
    candidate.state_.reset();
    return Result<void>::success();
}
Result<void> PixelFx::drawToCanvas(PixelFxCanvas canvas, PixelFxLayer layer, PixelFxPlacement placement) const {
    auto validated = pixelfx::validateCanvas(canvas, layer, placement);
    if (!validated) return validated;
    auto info    = info_;
    info.width   = canvas.width;
    info.height  = canvas.height;
    info.originX = canvas.originX;
    info.originY = canvas.originY;
    pixelfx::Paint p{info, canvas.color, canvas.light, program_->settings.lightGain};
    p.place(placement.x, placement.y, placement.flip, placement.angle);
    if (playback_->stopAt) p.fade = pixelfx::clamp(1 - (playback_->time - *playback_->stopAt) / .3);
    if (layer != PixelFxLayer::Front)
        pixelfx::drawProgramLayer(*program_, playback_->kernel.get(), playback_->time, p, pixelfx::Paint::Layer::Back);
    if (layer != PixelFxLayer::Back)
        pixelfx::drawProgramLayer(*program_, playback_->kernel.get(), playback_->time, p, pixelfx::Paint::Layer::Front);
    return Result<void>::success();
}
void PixelFx::draw() {
    auto drawn = drawLayer(PixelFxLayer::Both);
    if (!drawn) throw std::logic_error(drawn.status().describe());
}
Result<void> PixelFx::drawLayer(PixelFxLayer layer) {
    if (layer != PixelFxLayer::Back && layer != PixelFxLayer::Front && layer != PixelFxLayer::Both)
        return Result<void>::failure(configDiagnostic("Unknown effect layer", "/layer"));
    std::fill(color_.begin(), color_.end(), 0u);
    std::fill(light_.begin(), light_.end(), 0u);
    return drawToCanvas({info_.width, info_.height, info_.originX, info_.originY, color_, light_}, layer, {});
}
void PixelFx::render(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e12)
        throw std::invalid_argument("PixelFx: seconds must be finite and in [0,1e12]");
    if (isSimulated()) throw std::invalid_argument("PixelFx: simulated effects require advance/draw or prepared seek");
    const double time = seconds * program_->settings.playbackSpeed;
    if (time > 1e12) throw std::invalid_argument("PixelFx: scaled effect time exceeds 1e12");
    playback_->time         = time;
    playback_->compensation = 0;
    draw();
}
std::span<const uint32_t> PixelFx::color() const { return color_; }
std::span<const uint32_t> PixelFx::light() const { return light_; }
}  // namespace eve::stylize
