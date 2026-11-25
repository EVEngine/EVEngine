// Effect definitions own metadata, defaults and drawing; the host only composes their accessors.
#pragma once
#include <functional>
#include <variant>
#include "stylize/PixelFx.h"

namespace eve::stylize::pixelfx {
struct Paint;
struct Kernel;
struct AnalyticKernel;
struct SimulationRecipe;
/** @brief Internal cold-boundary binding; no field-name lookup occurs while drawing. */
struct Option {
    using Member = std::variant<
        double PixelFxSettings::*, int PixelFxSettings::*, uint32_t PixelFxSettings::*,
        std::vector<uint32_t> PixelFxSettings::*, double PixelFxFireSettings::*,
        std::vector<uint32_t> PixelFxFireSettings::*, uint32_t PixelFxFireSettings::*, PixelFxActor PixelFxSettings::*,
        std::vector<uint32_t> PixelFxProjectileSettings::*, uint32_t PixelFxProjectileSettings::*,
        double PixelFxProjectileSettings::*, int PixelFxProjectileSettings::*, double PixelFxExplosionSettings::*,
        uint32_t PixelFxExplosionSettings::*, std::vector<uint32_t> PixelFxExplosionSettings::*,
        std::string PixelFxExplosionSettings::*, double PixelFxWaterSettings::*, bool PixelFxWaterSettings::*,
        std::vector<uint32_t> PixelFxWeatherSettings::*, double PixelFxWeatherSettings::*,
        std::vector<uint32_t> PixelFxLightningSettings::*, int PixelFxLightningSettings::*,
        double PixelFxLightningSettings::*, double PixelFxIceSettings::*, int PixelFxIceSettings::*,
        std::vector<int64_t> PixelFxPickupSettings::*, int PixelFxPickupSettings::*,
        std::vector<uint32_t> PixelFxCritterSettings::*, double PixelFxCritterSettings::*,
        bool PixelFxCritterSettings::*, uint32_t PixelFxHolySettings::*, double PixelFxHolySettings::*,
        int PixelFxHolySettings::*, std::vector<uint32_t> PixelFxHolySettings::*, uint32_t PixelFxImpactSettings::*,
        double PixelFxImpactSettings::*, int PixelFxImpactSettings::*, std::vector<uint32_t> PixelFxImpactSettings::*,
        std::vector<uint32_t> PixelFxScifiSettings::*, uint32_t PixelFxScifiSettings::*, double PixelFxScifiSettings::*,
        double PixelFxArcaneSettings::*, std::vector<uint32_t> PixelFxShadowSettings::*,
        uint32_t PixelFxShadowSettings::*, std::vector<uint32_t> PixelFxNatureSettings::*,
        double PixelFxNatureSettings::*, int PixelFxNatureSettings::*, std::vector<uint32_t> PixelFxSpawnSettings::*,
        int PixelFxSpawnSettings::*, double PixelFxSpawnSettings::*, int64_t PixelFxSpawnSettings::*,
        uint32_t PixelFxStatusSettings::*, std::vector<uint32_t> PixelFxStatusSettings::*,
        std::vector<uint32_t> PixelFxMeleeSettings::*, uint32_t PixelFxMeleeSettings::*, double PixelFxMeleeSettings::*,
        std::string PixelFxUiSettings::*, double PixelFxUiSettings::*, std::vector<uint32_t> PixelFxUiSettings::*,
        uint32_t PixelFxTransitionSettings::*, std::array<double, 2> PixelFxTransitionSettings::*,
        std::vector<uint32_t> PixelFxTransitionSettings::*>;
    std::string_view name;
    double           minimum, maximum;
    bool             exclusiveMinimum;
    int              rampSteps;
    Member           member;
    bool             color = false;
};
/** @brief Borrow a known compiled descriptor; invalid kinds throw std::invalid_argument. */
const Descriptor& descriptorFor(PixelFxKind kind);
/** @brief Borrow the common parameter declarations for process lifetime. */
std::span<const Option> commonOptions();
/** @brief Read a declared option into an independent cold-boundary Value.
 * @cost Copies a bounded palette once per cold serialization call. */
Value readOption(const PixelFxSettings& settings, const Option& option);
/** @brief Decode one declared option into a private candidate; no input nodes are retained.
 * @cost Copies bounded palette input once per cold decode call. */
Result<void> writeOption(PixelFxSettings& settings, const Option& option, const Value& value);
/** @brief Validate and normalize only source-declared active options into an owning copy.
 * @cost Copies and resamples bounded active ramps once per configuration preparation. */
Result<PixelFxSettings> normalizeOptions(const Descriptor& descriptor, const PixelFxSettings& settings);
/** @brief Encode an independent versioned actor snapshot at the cold boundary.
 * @cost Copies at most 65536 mask cells and pixels once per configuration snapshot. */
Value encodeActor(const PixelFxActor& actor);
/** @brief Return owning actor schema constraints from the same bounded validator.
 * @param required Exclude absent input when the active source kernel requires a snapshot.
 * @param minimumFilledMaskCells Source-owned lower bound on occupied binary mask cells.
 * @param minimumHeight Source row-sampling lower bound, checked before any geometry is constructed. */
Value actorConstraints(bool required, int minimumFilledMaskCells, int minimumHeight);
/** @brief Validate and resolve an owning actor without retaining input Value nodes.
 * @cost Validates and copies bounded custom pixels once per configuration preparation. */
Result<PixelFxActor> decodeActor(const Value& input);
/** @brief Immutable source-owned cold actor-site contract; all strings/callbacks have process lifetime.
 * @note Validator borrows its input only synchronously, invokes no scripts and retains no pointers.
 * @cost Bounded mask/site validation once per configuration preparation, before geometry publication. */
struct ActorRule {
    std::string_view id, description;
    Result<void> (*validate)(const PixelFxActor&);
};
/** @brief Internal immutable process-lifetime metadata and trusted algorithm entry points. */
struct Descriptor {
    PixelFxInfo info;
    int         paletteSteps;
    void (*defaults)(PixelFxSettings&);
    void (*draw)(Paint&, double, const PixelFxSettings&);
    std::span<const Option> options{};
    std::unique_ptr<Kernel> (*create)(const PixelFxSettings&) = nullptr;
    void (*drawBack)(Paint&, double, const PixelFxSettings&)  = nullptr;
    /** @brief Prepare owning immutable analytic geometry, without retaining inputs or external pointers.
     * @cost Builds bounded authored maps once per configuration preparation, reused across frames and seeks. */
    std::unique_ptr<const AnalyticKernel> (*prepareAnalytic)(const PixelFxSettings&) = nullptr;
    /** @brief Reject absent actor input when the source kernel requires a silhouette. */
    bool actorRequired = false;
    /** @brief Minimum occupied cells required before kernel construction; zero permits an empty mask. */
    int minimumActorCells = 0;
    /** @brief Source row-sampling lower bound; one accepts all otherwise valid actor heights. */
    int minimumActorHeight = 1;
    /** @brief Optional process-lifetime source site rule, used only at cold validation/introspection. */
    std::optional<std::reference_wrapper<const ActorRule>> actorRule{};
    /** @brief Prepare the sole heavy simulation route; legacy create/analytic routes must be absent.
     * @cost Builds bounded immutable maps/patterns once per configuration, amortized across resets/seeks. */
    std::unique_ptr<const SimulationRecipe> (*prepareSimulation)(const PixelFxSettings&) = nullptr;
};
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& haloDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& sunburstDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& laserBoltDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& impactFrameDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& torchDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& candleDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& brazierDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& spiritFlameDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& fireballDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& fireTornadoDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& fireBreathDescriptor();
/** @brief Borrow the algorithm's immutable descriptor for the process lifetime; no callbacks. */
const Descriptor& flamethrowerDescriptor();
/** @brief Borrow the process-lifetime flame-pillar definition. */
const Descriptor& flamePillarDescriptor();
/** @brief Borrow the process-lifetime campfire definition. */
const Descriptor& campfireDescriptor();
/** @brief Borrow the process-lifetime fire-ring definition. */
const Descriptor& fireRingDescriptor();
/** @brief Borrow the process-lifetime arrow definition. */
const Descriptor& arrowDescriptor();
/** @brief Borrow the process-lifetime energy-disc definition. */
const Descriptor& energyDiscDescriptor();
/** @brief Borrow the process-lifetime bullet tracer definition. */
const Descriptor& bulletTracerDescriptor();
/** @brief Borrow the process-lifetime pistol muzzle definition. */
const Descriptor& muzzlePistolDescriptor();
/** @brief Borrow the process-lifetime shotgun muzzle definition. */
const Descriptor& muzzleShotgunDescriptor();
/** @brief Borrow the process-lifetime SMG definition. */
const Descriptor& muzzleSmgDescriptor();
/** @brief Borrow the process-lifetime plasma shot definition. */
const Descriptor& plasmaShotDescriptor();
/** @brief Borrow the process-lifetime magic missile definition. */
const Descriptor& magicMissileDescriptor();
/** @brief Borrow the process-lifetime cannon shot definition. */
const Descriptor& cannonballDescriptor();
/** @brief Borrow the process-lifetime rocket definition. */
const Descriptor& rocketDescriptor();
/** @brief Borrow the process-lifetime homing missile definition. */
const Descriptor& homingMissileDescriptor();
/** @brief Borrow the process-lifetime signal flare definition. */
const Descriptor& signalFlareDescriptor();
/** @brief Borrow the process-lifetime small explosion definition. */
const Descriptor& smallExplosionDescriptor();
/** @brief Borrow the process-lifetime big explosion definition. */
const Descriptor& bigExplosionDescriptor();
/** @brief Borrow the process-lifetime barrel explosion definition. */
const Descriptor& barrelExplosionDescriptor();
/** @brief Borrow the process-lifetime dirt explosion definition. */
const Descriptor& dirtExplosionDescriptor();
/** @brief Borrow the process-lifetime implosion definition. */
const Descriptor& implosionDescriptor();
/** @brief Borrow the process-lifetime firework burst definition. */
const Descriptor& fireworkBurstDescriptor();
/** @brief Borrow the process-lifetime firework rocket definition. */
const Descriptor& fireworkRocketDescriptor();
/** @brief Borrow the process-lifetime plasma explosion definition. */
const Descriptor& plasmaExplosionDescriptor();
/** @brief Borrow the process-lifetime cartoon POW definition. */
const Descriptor& cartoonPowDescriptor();
/** @brief Borrow the process-lifetime grenade definition. */
const Descriptor& grenadeDescriptor();
/** @brief Borrow the process-lifetime smoke bomb definition. */
const Descriptor& smokeBombDescriptor();
/** @brief Borrow the process-lifetime nuke definition. */
const Descriptor& nukeDescriptor();
/** @brief Borrow the process-lifetime water ripple definition. */
const Descriptor& ripplesDescriptor();
/** @brief Borrow the process-lifetime rain splash definition. */
const Descriptor& rainSplashDescriptor();
/** @brief Borrow the process-lifetime water drip definition. */
const Descriptor& dripDescriptor();
/** @brief Borrow the process-lifetime underwater bubbles definition. */
const Descriptor& bubblesDescriptor();
/** @brief Borrow the process-lifetime simulated water splash definition. */
const Descriptor& waterSplashDescriptor();
/** @brief Borrow the process-lifetime stone-basin fountain definition. */
const Descriptor& fountainDescriptor();
/** @brief Borrow the process-lifetime floating water sphere definition. */
const Descriptor& waterOrbDescriptor();
/** @brief Borrow the process-lifetime differentially wound water vortex definition. */
const Descriptor& whirlpoolDescriptor();
/** @brief Borrow the process-lifetime pressurized water stream definition. */
const Descriptor& waterJetDescriptor();
/** @brief Borrow the process-lifetime waterfall definition and cold geometry factory. */
const Descriptor& waterfallDescriptor();
/** @brief Borrow the process-lifetime breaking wave definition. */
const Descriptor& waveCrashDescriptor();
/** @brief Borrow the process-lifetime erupting geyser definition. */
const Descriptor& geyserDescriptor();
/** @brief Borrow the process-lifetime flaming meteor definition. */
const Descriptor& meteorDescriptor();
/** @brief Borrow the process-lifetime layered snowfall definition. */
const Descriptor& snowDescriptor();
/** @brief Borrow the process-lifetime banked fog definition and immutable noise factory. */
const Descriptor& fogDescriptor();
/** @brief Borrow the process-lifetime moonlit cloud definition. */
const Descriptor& cloudsDescriptor();
/** @brief Borrow the process-lifetime volumetric sunlight definition. */
const Descriptor& godRaysDescriptor();
/** @brief Borrow the process-lifetime aurora definition and immutable field factory. */
const Descriptor& auroraDescriptor();
/** @brief Borrow the process-lifetime rain definition and immutable field factory. */
const Descriptor& rainDescriptor();
/** @brief Borrow the process-lifetime blizzard definition and immutable field factory. */
const Descriptor& blizzardDescriptor();
/** @brief Borrow the process-lifetime shooting-star definition. */
const Descriptor& shootingStarsDescriptor();
/** @brief Borrow the process-lifetime autumn leaves definition and litter preparation. */
const Descriptor& fallingLeavesDescriptor();
/** @brief Borrow the process-lifetime falling blossoms definition and scatter preparation. */
const Descriptor& cherryBlossomsDescriptor();
/** @brief Borrow the process-lifetime fireflies definition. */
const Descriptor& firefliesDescriptor();
/** @brief Borrow the process-lifetime storm definition. */
const Descriptor& stormDescriptor();
/** @brief Borrow the process-lifetime spark arc definition. */
const Descriptor& sparkArcDescriptor();
/** @brief Borrow the process-lifetime ball lightning definition. */
const Descriptor& ballLightningDescriptor();
/** @brief Borrow the process-lifetime plasma globe definition. */
const Descriptor& plasmaBallDescriptor();
/** @brief Borrow the process-lifetime thunderclap definition. */
const Descriptor& thunderclapDescriptor();
/** @brief Borrow the process-lifetime contact zap definition. */
const Descriptor& zapHitDescriptor();
/** @brief Borrow the process-lifetime targeted lightning strike definition. */
const Descriptor& lightningStrikeDescriptor();
/** @brief Borrow the process-lifetime Tesla coil definition. */
const Descriptor& teslaCoilDescriptor();
/** @brief Borrow the process-lifetime storm cloud definition. */
const Descriptor& stormCloudDescriptor();
/** @brief Borrow the process-lifetime electrified actor definition. */
const Descriptor& electrifiedDescriptor();
/** @brief Borrow the process-lifetime charge/release definition. */
const Descriptor& chargeUpDescriptor();
/** @brief Borrow the process-lifetime three-hop lightning definition. */
const Descriptor& chainLightningDescriptor();
/** @brief Borrow the process-lifetime rotating ice crystal definition. */
const Descriptor& iceCrystalDescriptor();
/** @brief Borrow the process-lifetime snowflake burst definition. */
const Descriptor& snowflakeBurstDescriptor();
/** @brief Borrow the process-lifetime ice lance definition. */
const Descriptor& iceLanceDescriptor();
/** @brief Borrow the process-lifetime ice-spike wave definition. */
const Descriptor& iceSpikesDescriptor();
/** @brief Borrow the process-lifetime falling icicle definition. */
const Descriptor& icicleFallDescriptor();
/** @brief Borrow the process-lifetime ice block shatter definition. */
const Descriptor& iceShatterDescriptor();
/** @brief Borrow the process-lifetime actor-freeze definition. */
const Descriptor& freezeDescriptor();
/** @brief Borrow the process-lifetime frost nova definition. */
const Descriptor& frostNovaDescriptor();
/** @brief Borrow the process-lifetime frost breath definition. */
const Descriptor& frostBreathDescriptor();
/** @brief Borrow the process-lifetime blizzard vortex definition. */
const Descriptor& blizzardVortexDescriptor();
/** @brief Borrow the process-lifetime spinning coin definition. */
const Descriptor& coinSpinDescriptor();
/** @brief Borrow the process-lifetime rotating gem definition and immutable mesh factory. */
const Descriptor& gemSpinDescriptor();
/** @brief Borrow the process-lifetime broken crystal definition and immutable mesh factory. */
const Descriptor& crystalShardDescriptor();
/** @brief Borrow the process-lifetime figure-eight XP orb definition. */
const Descriptor& xpOrbDescriptor();
/** @brief Borrow the process-lifetime dropped-sword loot beam definition. */
const Descriptor& lootBeamDescriptor();
/** @brief Borrow the process-lifetime bevelled power star definition. */
const Descriptor& powerStarDescriptor();
/** @brief Borrow the process-lifetime ornate key definition. */
const Descriptor& keyGlintDescriptor();
/** @brief Borrow the process-lifetime double-beating heart definition. */
const Descriptor& heartPickupDescriptor();
/** @brief Borrow the process-lifetime rocking potion definition. */
const Descriptor& potionDescriptor();
/** @brief Borrow the process-lifetime collection flash and curved-star simulation definition. */
const Descriptor& collectBurstDescriptor();
/** @brief Borrow the process-lifetime treasure chest simulation and source options. */
const Descriptor& chestOpenDescriptor();
/** @brief Borrow the process-lifetime butterfly definition and eyespot options. */
const Descriptor& butterflyDescriptor();
/** @brief Borrow the process-lifetime songbird definition and breast options. */
const Descriptor& birdDescriptor();
/** @brief Borrow the process-lifetime four-frame bat and eye-ramp definition. */
const Descriptor& batDescriptor();
/** @brief Borrow the process-lifetime floating eyeball definition and owning vein preparation. */
const Descriptor& eyeballDescriptor();
/** @brief Borrow the process-lifetime fish school and stripe-ramp definition. */
const Descriptor& fishSchoolDescriptor();
/** @brief Borrow the process-lifetime flying-skull definition and immutable crown preparation. */
const Descriptor& flyingSkullDescriptor();
/** @brief Borrow the process-lifetime frog pose and belly-ramp definition. */
const Descriptor& frogDescriptor();
/** @brief Borrow the process-lifetime translucent sheet-ghost definition. */
const Descriptor& ghostDescriptor();
/** @brief Borrow the immutable native wisp descriptor for the process lifetime. */
const Descriptor& wispDescriptor();
/** @brief Borrow the immutable native snake descriptor for the process lifetime. */
const Descriptor& snakeDescriptor();
/** @brief Borrow the immutable native jellyfish descriptor for the process lifetime. */
const Descriptor& jellyfishDescriptor();
/** @brief Borrow the immutable native slime descriptor for the process lifetime. */
const Descriptor& slimeDescriptor();
/** @brief Borrow the immutable native blessing descriptor for the process lifetime. */
const Descriptor& blessingDescriptor();
/** @brief Borrow the immutable native feather-burst descriptor for the process lifetime. */
const Descriptor& featherBurstDescriptor();
/** @brief Borrow the immutable native divine-shield descriptor for the process lifetime. */
const Descriptor& divineShieldDescriptor();
/** @brief Borrow the immutable native heal descriptor for the process lifetime. */
const Descriptor& healDescriptor();
/** @brief Borrow the immutable native healing-ring descriptor for the process lifetime. */
const Descriptor& healAoeDescriptor();
/** @brief Borrow the immutable native holy-pillar descriptor for the process lifetime. */
const Descriptor& holyPillarDescriptor();
/** @brief Borrow the immutable native smite descriptor for the process lifetime. */
const Descriptor& smiteDescriptor();
/** @brief Borrow the immutable native resurrection descriptor for the process lifetime. */
const Descriptor& resurrectionDescriptor();
/** @brief Borrow the immutable native blunt-hit descriptor for the process lifetime. */
const Descriptor& bluntHitDescriptor();
/** @brief Borrow the immutable native bullet-impact descriptor for the process lifetime. */
const Descriptor& bulletImpactDescriptor();
/** @brief Borrow the immutable native critical-hit descriptor for the process lifetime. */
const Descriptor& critHitDescriptor();
/** @brief Borrow the immutable native hit-spark descriptor for the process lifetime. */
const Descriptor& hitSparkDescriptor();
/** @brief Borrow the immutable native hit-flash descriptor for the process lifetime. */
const Descriptor& hitFlashDescriptor();
/** @brief Borrow the immutable native parry descriptor for the process lifetime. */
const Descriptor& parryDescriptor();
/** @brief Borrow the immutable native ricochet descriptor for the process lifetime. */
const Descriptor& ricochetDescriptor();
/** @brief Borrow the immutable native shield-block descriptor for the process lifetime. */
const Descriptor& shieldBlockDescriptor();
/** @brief Borrow the immutable native heavy-landing descriptor for the process lifetime. */
const Descriptor& heavyLandingDescriptor();
/** @brief Borrow the immutable native ground-slam descriptor for the process lifetime. */
const Descriptor& groundSlamDescriptor();
/** @brief Borrow the immutable native jetpack descriptor for the process lifetime. */
const Descriptor& jetpackDescriptor();
/** @brief Borrow the immutable native teleporter-pad descriptor for the process lifetime. */
const Descriptor& teleporterPadDescriptor();
/** @brief Borrow the immutable native hologram descriptor for the process lifetime. */
const Descriptor& hologramDescriptor();
/** @brief Borrow the immutable native laser-grid descriptor for the process lifetime. */
const Descriptor& laserGridDescriptor();
/** @brief Borrow the immutable native energy-core descriptor for the process lifetime. */
const Descriptor& energyCoreDescriptor();
/** @brief Borrow the immutable native scanner descriptor for the process lifetime. */
const Descriptor& scannerDescriptor();
/** @brief Borrow the immutable native force-field descriptor for the process lifetime. */
const Descriptor& forceFieldDescriptor();
/** @brief Borrow the immutable native tractor-beam descriptor for the process lifetime. */
const Descriptor& tractorBeamDescriptor();
/** @brief Borrow the immutable native warp-out descriptor for the process lifetime. */
const Descriptor& warpOutDescriptor();
/** @brief Borrow the immutable native warp-in descriptor for the process lifetime. */
const Descriptor& warpInDescriptor();
/** @brief Borrow the immutable native EMP-blast descriptor for the process lifetime. */
const Descriptor& empBlastDescriptor();
/** @brief Borrow the immutable native glitch descriptor for the process lifetime. */
const Descriptor& glitchDescriptor();
/** @brief Borrow the immutable native rune-circle descriptor for the process lifetime. */
const Descriptor& runeCircleDescriptor();
/** @brief Borrow the immutable native star-fall descriptor for the process lifetime. */
const Descriptor& starFallDescriptor();
/** @brief Borrow the immutable native mana-burst descriptor for the process lifetime. */
const Descriptor& manaBurstDescriptor();
/** @brief Borrow the immutable native spell-charge descriptor for the process lifetime. */
const Descriptor& spellChargeDescriptor();
/** @brief Borrow the immutable native arcane-orb descriptor for the process lifetime. */
const Descriptor& arcaneOrbDescriptor();
/** @brief Borrow the immutable native arcane-beam descriptor for the process lifetime. */
const Descriptor& arcaneBeamDescriptor();
/** @brief Borrow the immutable native arcane-missiles descriptor for the process lifetime. */
const Descriptor& arcaneMissilesDescriptor();
/** @brief Borrow the immutable native blink-in descriptor for the process lifetime. */
const Descriptor& blinkInDescriptor();
/** @brief Borrow the immutable native blink-out descriptor for the process lifetime. */
const Descriptor& blinkOutDescriptor();
/** @brief Borrow the immutable native magic-circle-cast descriptor for the process lifetime. */
const Descriptor& magicCircleCastDescriptor();
/** @brief Borrow the immutable native portal descriptor for the process lifetime. */
const Descriptor& portalDescriptor();
/** @brief Borrow the immutable native time-stop descriptor for the process lifetime. */
const Descriptor& timeStopDescriptor();
/** @brief Borrow the immutable native black-hole descriptor for the process lifetime. */
const Descriptor& blackHoleDescriptor();
/** @brief Borrow the immutable native curse descriptor for the process lifetime. */
const Descriptor& curseDescriptor();
/** @brief Borrow the immutable native dark-explosion descriptor for the process lifetime. */
const Descriptor& darkExplosionDescriptor();
/** @brief Borrow the immutable native eclipse descriptor for the process lifetime. */
const Descriptor& eclipseDescriptor();
/** @brief Borrow the immutable native grasping-hands descriptor for the process lifetime. */
const Descriptor& graspingHandsDescriptor();
/** @brief Borrow the immutable native hex-sigil descriptor for the process lifetime. */
const Descriptor& hexSigilDescriptor();
/** @brief Borrow the immutable native life-steal descriptor for the process lifetime. */
const Descriptor& lifeStealDescriptor();
/** @brief Borrow the immutable native necro-skulls descriptor for the process lifetime. */
const Descriptor& necroSkullsDescriptor();
/** @brief Borrow the immutable native shadow-flames descriptor for the process lifetime. */
const Descriptor& shadowFlamesDescriptor();
/** @brief Borrow the immutable native shadow-tendrils descriptor for the process lifetime. */
const Descriptor& shadowTendrilsDescriptor();
/** @brief Borrow the immutable native soul-drain descriptor for the process lifetime. */
const Descriptor& soulDrainDescriptor();
/** @brief Borrow the immutable native void-orb descriptor for the process lifetime. */
const Descriptor& voidOrbDescriptor();
/** @brief Borrow the immutable native acid-splash descriptor for the process lifetime. */
const Descriptor& acidSplashDescriptor();
/** @brief Borrow the immutable native boulder descriptor for the process lifetime. */
const Descriptor& boulderDescriptor();
/** @brief Borrow the immutable native earth-spike descriptor for the process lifetime. */
const Descriptor& earthSpikeDescriptor();
/** @brief Borrow the immutable native healing-bloom descriptor for the process lifetime. */
const Descriptor& healingBloomDescriptor();
/** @brief Borrow the immutable native leaf-storm descriptor for the process lifetime. */
const Descriptor& leafStormDescriptor();
/** @brief Borrow the immutable native poison-cloud descriptor for the process lifetime. */
const Descriptor& poisonCloudDescriptor();
/** @brief Borrow the immutable native quake descriptor for the process lifetime. */
const Descriptor& quakeDescriptor();
/** @brief Borrow the immutable native root-snare descriptor for the process lifetime. */
const Descriptor& rootSnareDescriptor();
/** @brief Borrow the process-lifetime growing mushroom and spore cloud definition. */
const Descriptor& sporeBurstDescriptor();
/** @brief Borrow the process-lifetime sequential hooked thorn definition. */
const Descriptor& thornsDescriptor();
/** @brief Borrow the process-lifetime layered storm funnel definition. */
const Descriptor& tornadoDescriptor();
/** @brief Borrow the process-lifetime luminous tar pool and bubble definition. */
const Descriptor& toxicPuddleDescriptor();
/** @brief Borrow the process-lifetime spring-driven thorny vine definition. */
const Descriptor& vineWhipDescriptor();
/** @brief Borrow the process-lifetime bouncing coin loot definition. */
const Descriptor& coinDropDescriptor();
/** @brief Borrow the process-lifetime per-pixel actor disintegration definition. */
const Descriptor& disintegrateDescriptor();
/** @brief Source-faithful ascending body burn with owning ash material. */
const Descriptor& dissolveBurnDescriptor();
/** @brief Source egg rocking, staged cracks, lit shell pieces and hatching rays. */
const Descriptor& eggHatchDescriptor();
/** @brief Layered cartoon poof, speed lines and spinning stars. */
const Descriptor& poofDescriptor();
/** @brief Body-writing scan beam with owned actor geometry. */
const Descriptor& respawnBeamDescriptor();
/** @brief Faceted body ice, growing cracks and tumbling melting pieces. */
const Descriptor& shatterDeathDescriptor();
/** @brief Body-derived pale soul with stretching, melting legs and edge motes. */
const Descriptor& soulRiseDescriptor();
/** @brief Ragged swirling rift with smoke belches and wisp fountains. */
const Descriptor& spawnPortalDescriptor();
/** @brief Procedural horned brute rising through rune fire and rubble. */
const Descriptor& summonRiseDescriptor();
/** @brief Borrow the immutable source Haste definition for process lifetime. */
const Descriptor& hasteDescriptor();
/** @brief Borrow the immutable source Slowed definition for process lifetime. */
const Descriptor& slowedDescriptor();
/** @brief Borrow the immutable source Bleeding definition for process lifetime. */
const Descriptor& bleedingDescriptor();
/** @brief Borrow the immutable source Stunned definition for process lifetime. */
const Descriptor& stunnedDescriptor();
/** @brief Borrow the immutable source Sleep definition for process lifetime. */
const Descriptor& sleepDescriptor();
/** @brief Borrow the immutable source Confused definition for process lifetime. */
const Descriptor& confusedDescriptor();
/** @brief Borrow the immutable source Charmed definition for process lifetime. */
const Descriptor& charmedDescriptor();
/** @brief Borrow the immutable source Cursed definition for process lifetime. */
const Descriptor& cursedDescriptor();
/** @brief Borrow the immutable source Burning definition for process lifetime. */
const Descriptor& burningDescriptor();
/** @brief Borrow the immutable source Poisoned definition for process lifetime. */
const Descriptor& poisonedDescriptor();
/** @brief Borrow the immutable source Rage definition for process lifetime. */
const Descriptor& rageDescriptor();
/** @brief Borrow the immutable source Petrified definition for process lifetime. */
const Descriptor& petrifiedDescriptor();
/** @brief Borrow the immutable source Frozen definition for process lifetime. */
const Descriptor& frozenDescriptor();
/** @brief Borrow the immutable source Claw swipe definition for process lifetime. */
const Descriptor& clawSwipeDescriptor();
/** @brief Borrow the immutable source Spear sweep definition for process lifetime. */
const Descriptor& spearSweepDescriptor();
/** @brief Borrow the immutable source Thrust definition for process lifetime. */
const Descriptor& thrustDescriptor();
/** @brief Borrow the immutable source Sword slash definition for process lifetime. */
const Descriptor& swordSlashDescriptor();
/** @brief Borrow the immutable source Double slash definition for process lifetime. */
const Descriptor& doubleSlashDescriptor();
/** @brief Borrow the immutable source Dagger flurry definition for process lifetime. */
const Descriptor& daggerFlurryDescriptor();
/** @brief Source rising crescent and comic impact recipe. */
const Descriptor& uppercutDescriptor();
/** @brief Source far/front tilted spinning ring recipe. */
const Descriptor& spinSlashDescriptor();
/** @brief Source rolling whip, sampled tip trail and crack recipe. */
const Descriptor& whipCrackDescriptor();
/** @brief Source torn ghost crescent and wisp-thread simulation. */
const Descriptor& scytheReapDescriptor();
/** @brief Source ember cleave and hot cracked-ground simulation. */
const Descriptor& heavyCleaveDescriptor();
/** @brief Source hammer comet, dent, force dome and settled debris simulation. */
const Descriptor& hammerSmashDescriptor();
/** @brief Source thrown/squashed damage text recipe. */
const Descriptor& damageNumberDescriptor();
/** @brief Source streaming combo words and draining timer recipe. */
const Descriptor& comboCounterDescriptor();
/** @brief Source spring-driven alert mark recipe. */
const Descriptor& exclamationDescriptor();
/** @brief Source critical text, jagged starburst and stamped tag recipe. */
const Descriptor& critNumberDescriptor();
/** @brief Source segmented health/ghost drain and struck HUD plate recipe. */
const Descriptor& healthBarHitDescriptor();
/** @brief Source cracked heart, tumbling halves and empty socket recipe. */
const Descriptor& heartLostDescriptor();
/** @brief Source burning countdown dial, terminal word and shared-stream particle recipe. */
const Descriptor& countdownDescriptor();
/** @brief Source coin fountain, staggered flights and incrementing HUD recipe. */
const Descriptor& coinCollectDescriptor();
/** @brief Source party poppers, fluttering paper and curled point-chain recipe. */
const Descriptor& confettiDescriptor();
/** @brief Source achievement toast, medal and prepared mini-font title recipe. */
const Descriptor& achievementDescriptor();
/** @brief Source stitched quest ribbon, typed letters and stamped seal recipe. */
const Descriptor& questCompleteDescriptor();
/** @brief Source actor-lit level pillar and simulated falling-letter recipe. */
const Descriptor& levelUpDescriptor();
/** @brief Source screen slats and gap dust recipe. */
const Descriptor& blindsDescriptor();
/** @brief Source CRT squeeze, phosphor dot and warm-up recipe. */
const Descriptor& crtOffDescriptor();
/** @brief Source ranked procedural block dissolve recipe. */
const Descriptor& pixelDissolveDescriptor();
/** @brief Source warm spotlight and radial beveled iris recipe. */
const Descriptor& irisDescriptor();
/** @brief Source spinning gold star iris and tip particle recipe. */
const Descriptor& starIrisDescriptor();
/** @brief Source prepared faceted diamond lattice and ordered glints. */
const Descriptor& diamondWipeDescriptor();
/** @brief Source prepared torn bands, quantized freeze/back-jump and split-color slivers. */
const Descriptor& glitchWipeDescriptor();
/** @brief Source grinding blade edge, second cut and split cover halves. */
const Descriptor& slashWipeDescriptor();
/** @brief Source prepared fire fields, char veins, flames, smoke and reopening fronts. */
const Descriptor& burnAwayDescriptor();
/** @brief Source prepared crack cells, bouncing pane and independently tumbling glass shards. */
const Descriptor& shatterWipeDescriptor();
/** @brief Borrow the process-lifetime arrow-shot cue composition. */
const Descriptor& arrowShotDescriptor();
/** @brief Borrow the process-lifetime pistol-shots cue composition. */
const Descriptor& pistolShotsDescriptor();
/** @brief Borrow the process-lifetime fireball-throw cue composition. */
const Descriptor& fireballThrowDescriptor();
/** @brief Borrow the process-lifetime ice-lance-throw cue composition. */
const Descriptor& iceLanceThrowDescriptor();
/** @brief Borrow the process-lifetime rocket-launcher cue composition. */
const Descriptor& rocketLauncherDescriptor();
/** @brief Borrow the process-lifetime lightning-call cue composition. */
const Descriptor& lightningCallDescriptor();
/** @brief Borrow the process-lifetime meteor-call cue composition. */
const Descriptor& meteorCallDescriptor();
/** @brief Borrow the process-lifetime holy-smite cue composition. */
const Descriptor& holySmiteDescriptor();
/** @brief Borrow the process-lifetime dash-strike cue composition. */
const Descriptor& dashStrikeDescriptor();
/** @brief Borrow the process-lifetime earth-stomp cue composition. */
const Descriptor& earthStompDescriptor();
/** @brief Borrow the process-lifetime ground-pound cue composition. */
const Descriptor& groundPoundDescriptor();
/** @brief Borrow the process-lifetime sword-combo cue composition. */
const Descriptor& swordComboDescriptor();
}  // namespace eve::stylize::pixelfx
