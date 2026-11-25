#pragma once

#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace eve::stylize {
namespace pixelfx {
struct Descriptor;
struct Program;
struct PlaybackState;
}  // namespace pixelfx

/** @brief Native algorithms ported from patch-gg/pixelfx; other upstream ids are not supported. */
enum class PixelFxKind {
    Halo,
    Sunburst,
    LaserBolt,
    ImpactFrame,
    Torch,
    Candle,
    Brazier,
    SpiritFlame,
    Fireball,
    FireTornado,
    FireBreath,
    Flamethrower,
    FlamePillar,
    Campfire,
    FireRing,
    Arrow,
    EnergyDisc,
    BulletTracer,
    MuzzlePistol,
    MuzzleShotgun,
    MuzzleSmg,
    PlasmaShot,
    MagicMissile,
    Cannonball,
    Rocket,
    HomingMissile,
    SignalFlare,
    SmallExplosion,
    BigExplosion,
    BarrelExplosion,
    DirtExplosion,
    Implosion,
    FireworkBurst,
    FireworkRocket,
    PlasmaExplosion,
    CartoonPow,
    Grenade,
    SmokeBomb,
    Nuke,
    Ripples,
    RainSplash,
    Drip,
    Bubbles,
    WaterSplash,
    Fountain,
    WaterOrb,
    Whirlpool,
    WaterJet,
    Waterfall,
    WaveCrash,
    Geyser,
    Meteor,
    Snow,
    Fog,
    Clouds,
    GodRays,
    Aurora,
    Rain,
    Blizzard,
    ShootingStars,
    FallingLeaves,
    CherryBlossoms,
    Fireflies,
    Storm,
    SparkArc,
    BallLightning,
    PlasmaBall,
    Thunderclap,
    ZapHit,
    LightningStrike,
    TeslaCoil,
    StormCloud,
    Electrified,
    ChargeUp,
    ChainLightning,
    IceCrystal,
    SnowflakeBurst,
    IceLance,
    IceSpikes,
    IcicleFall,
    IceShatter,
    Freeze,
    FrostNova,
    FrostBreath,
    BlizzardVortex,
    CoinSpin,
    GemSpin,
    CrystalShard,
    XpOrb,
    LootBeam,
    PowerStar,
    KeyGlint,
    HeartPickup,
    Potion,
    CollectBurst,
    ChestOpen,
    Butterfly,
    Bird,
    Bat,
    Eyeball,
    FishSchool,
    FlyingSkull,
    Frog,
    Ghost,
    Wisp,
    Snake,
    Jellyfish,
    Slime,
    Blessing,
    FeatherBurst,
    DivineShield,
    Heal,
    HealAoe,
    HolyPillar,
    Smite,
    Resurrection,
    BluntHit,
    BulletImpact,
    CritHit,
    HitSpark,
    HitFlash,
    Parry,
    Ricochet,
    ShieldBlock,
    HeavyLanding,
    GroundSlam,
    Jetpack,
    TeleporterPad,
    Hologram,
    LaserGrid,
    EnergyCore,
    Scanner,
    ForceField,
    TractorBeam,
    WarpOut,
    WarpIn,
    EmpBlast,
    Glitch,
    RuneCircle,
    StarFall,
    ManaBurst,
    SpellCharge,
    ArcaneOrb,
    ArcaneBeam,
    ArcaneMissiles,
    BlinkIn,
    BlinkOut,
    MagicCircleCast,
    Portal,
    TimeStop,
    BlackHole,
    Curse,
    DarkExplosion,
    Eclipse,
    GraspingHands,
    HexSigil,
    LifeSteal,
    NecroSkulls,
    ShadowFlames,
    ShadowTendrils,
    SoulDrain,
    VoidOrb,
    AcidSplash,
    Boulder,
    EarthSpike,
    HealingBloom,
    LeafStorm,
    PoisonCloud,
    Quake,
    RootSnare,
    SporeBurst,
    Thorns,
    Tornado,
    ToxicPuddle,
    VineWhip,
    CoinDrop,
    Disintegrate,
    DissolveBurn,
    EggHatch,
    Poof,
    RespawnBeam,
    ShatterDeath,
    SoulRise,
    SpawnPortal,
    SummonRise,
    Haste,
    Slowed,
    Bleeding,
    Stunned,
    Sleep,
    Confused,
    Charmed,
    Cursed,
    Burning,
    Poisoned,
    Rage,
    Petrified,
    Frozen,
    ClawSwipe,
    SpearSweep,
    Thrust,
    SwordSlash,
    DoubleSlash,
    DaggerFlurry,
    Uppercut,
    SpinSlash,
    WhipCrack,
    ScytheReap,
    HeavyCleave,
    HammerSmash,
    DamageNumber,
    ComboCounter,
    Exclamation,
    CritNumber,
    HealthBarHit,
    HeartLost,
    Countdown,
    CoinCollect,
    Confetti,
    Achievement,
    QuestComplete,
    LevelUp,
    Blinds,
    CrtOff,
    PixelDissolve,
    Iris,
    StarIris,
    DiamondWipe,
    GlitchWipe,
    SlashWipe,
    BurnAway,
    ShatterWipe,
    ArrowShot,
    PistolShots,
    FireballThrow,
    IceLanceThrow,
    RocketLauncher,
    LightningCall,
    MeteorCall,
    HolySmite,
    DashStrike,
    EarthStomp,
    GroundPound,
    SwordCombo
};

/** @brief Immutable metadata snapshot; id borrows a static literal; dimensions/origin are logical pixels. */
struct PixelFxInfo {
    PixelFxKind      kind;
    std::string_view id;
    int              width, height, originX, originY;
    double           duration;
    bool             loop;
};

/** @brief Effect passes used by explicit body/effect compositors. */
enum class PixelFxLayer { Back, Front, Both };
/** @brief Source policy for the external actor layer; effect-owned actor pixels remain in effect planes. */
enum class PixelFxActorVisibility { Visible, Hidden };
/** @brief Synchronously borrowed target planes; the caller retains ownership and valid storage. */
struct PixelFxCanvas {
    int                 width, height, originX, originY;
    std::span<uint32_t> color, light;
};
/** @brief Local-space placement applied at draw time, without modifying simulation inputs or time. */
struct PixelFxPlacement {
    double x = 0, y = 0, angle = 0;
    bool   flip = false;
};

/** @brief Strongly typed material ramps, flame height, projectile radius and physical speed, cone spread and stream
 * reach. */
struct PixelFxFireSettings {
    std::vector<uint32_t> wood, iron, root, wax, brass, pilot, rock;
    double                height = 20;
    double                radius = 5;
    double                speed  = 120;
    double                spread = 0.24;
    double                reach  = 112;
    uint32_t              stone  = 0xff693f4au;
};

/** @brief Owning projectile material ramps and wind tint, resolved during configuration preparation. */
struct PixelFxProjectileSettings {
    std::vector<uint32_t> wood, steel, smoke, brass, shell, iron, metal, paint;
    double                size = 9, speed = 220, height = 26, reach = 112;
    int                   teeth = 8;
    uint32_t              wind  = 0xffe8c8b8u;
    uint32_t              stone = 0xff693f4au;
};

/** @brief Owning explosion scale, ground, barrel and secondary fire materials, validated at cold configuration
 * boundaries. */
struct PixelFxExplosionSettings {
    /** @brief Owning material ramps; active descriptors define their normalized step counts. */
    std::vector<uint32_t> barrel, fire, core, tips, accent, body, canister, sparks;
    /** @brief Owning UTF-8 caption; active descriptors validate scalar-count bounds during preparation. */
    std::string text;
    double      size  = 1;
    uint32_t    stone = 0xff693f4au;
    /** @brief Owning packed opaque dust color, validated by the active descriptor. */
    uint32_t dust = 0xff66748au;
};

/** @brief Owning lightning materials, copied and validated during cold preparation. */
struct PixelFxLightningSettings {
    /** @brief Owning head-to-tail spark colors; active descriptors validate and fit four source steps. */
    std::vector<uint32_t> sparks;
    /** @brief Owning coil and storm materials, fitted to their active source tone counts. */
    std::vector<uint32_t> copper, steel, cloud, rain;
    /** @brief Number of plasma filaments, validated by the active descriptor. */
    int tendrils = 9;
    /** @brief Shockwave reach in logical pixels, validated during cold preparation. */
    double reach = 104;
    /** @brief Charge duration in seconds; validated before simulation publication. */
    double charge = 1.6;
};

/** @brief Owning weather material ramps, normalized only by their active effect descriptor. */
struct PixelFxWeatherSettings {
    /** @brief Owning aurora upper-hue ramp; the active descriptor validates and fits seven steps. */
    std::vector<uint32_t> upper;
    /** @brief Owning starfield material; active descriptors validate and fit its tone count. */
    std::vector<uint32_t> sky;
    /** @brief Owning autumn red/gold ramps; active descriptors validate and fit six steps. */
    std::vector<uint32_t> reds, golds;
    /** @brief Owning meadow material ramp; active descriptors validate and fit six steps. */
    std::vector<uint32_t> grass;
    /** @brief Owning lightning material ramp; active descriptors validate and fit six steps. */
    std::vector<uint32_t> bolt;
    /** @brief Rain slot multiplier, validated against the active effect range during preparation. */
    double density = 1;
};

/** @brief Owning water parameters, copied during cold preparation; caller-thread affine with no callbacks. */
struct PixelFxWaterSettings {
    /** @brief Water release/fall height in logical pixels, validated by the active source descriptor. */
    double height = 62;
    /** @brief Floating water sphere radius in logical pixels; active descriptors own finite bounds. */
    double radius = 19;
    /** @brief Falling sheet half-width in logical pixels, validated by the active descriptor. */
    double width = 13;
    /** @brief Whether to draw the source dripstone; this is a strict boolean configuration value. */
    bool source = true;
    /** @brief Whether to draw the incoming splash stone; strict boolean in active configurations. */
    bool stone = true;
    /** @brief Whether to draw the waterfall outcrop; strict boolean in active configurations. */
    bool ledge = true;
};

/** @brief Source of an owning actor input; stock geometry is derived during cold preparation. */
enum class PixelFxActorSource { None, Hero, Dummy, Custom };
/**
 * @brief Owning local-space actor snapshot; no engine object, image or VM references are retained.
 * @note Custom dimensions are 1..256; mask and pixels each contain width*height entries in row order.
 * Mask entries are 0/1 independently of the packed RGBA pixels. Head/chest are local-space anchors;
 * eyes are mask-local integer cells. Stock sources derive all fields; None derives empty buffers.
 * Configure through PixelFx preparation before rendering. Caller-thread affine, no callbacks.
 */
struct PixelFxActor {
    PixelFxActorSource              source = PixelFxActorSource::None;
    int                             width = 0, height = 0, scale = 1;
    double                          x = 0, y = 0;
    std::array<double, 2>           head{}, chest{};
    std::vector<std::array<int, 2>> eyes;
    std::vector<uint8_t>            mask;
    std::vector<uint32_t>           pixels;
};

/** @brief Ice-family configuration owned by its instance; caller-thread affine. */
struct PixelFxIceSettings {
    /** @brief Frost-breath cone half-angle in radians, validated by the active descriptor. */
    double spread = .21;
    /** @brief Frost-breath billow velocity multiplier; zero leaves snow/streak motion active as upstream. */
    double power = 1;
    /** @brief Crystal floating height in logical pixels, validated at configuration. */
    double height = 34;
    /** @brief Ice-spike wave reach in logical pixels, validated by the active descriptor. */
    double reach = 172;
    /** @brief Number of authored spinning flakes, validated by the active effect. */
    int count = 9;
};

/** @brief Owning pickup materials/counts; caller-thread affine, normalized during preparation. */
struct PixelFxPickupSettings {
    /** @brief Chest wood ramp, resampled to seven opaque entries; preserve signed or unsigned32-bit
     * source numbers because source strict equality against unsigned sprite cells observes the sign. */
    std::vector<int64_t> wood;
    /** @brief Authored gold coins, zero to64; four gems remain independently present as upstream. */
    int coins = 12;
};

/** @brief Owning critter material ramps; caller-thread affine and normalized during preparation. */
struct PixelFxCritterSettings {
    /** @brief Butterfly eyespot ramp, normalized to four opaque entries. */
    std::vector<uint32_t> spots;
    /** @brief Songbird breast ramp, normalized to four opaque entries. */
    std::vector<uint32_t> breast;
    /** @brief Bat eye ramp, normalized to four opaque entries. */
    std::vector<uint32_t> eyes;
    /** @brief Eyeball flesh ramp, normalized to five opaque entries. */
    std::vector<uint32_t> flesh;
    /** @brief Eyeball iris ramp, normalized to five opaque entries. */
    std::vector<uint32_t> iris;
    /** @brief Fish-school stripe ramp, normalized to three opaque entries. */
    std::vector<uint32_t> stripe;
    /** @brief Flying-skull bone ramp, normalized to seven opaque entries. */
    std::vector<uint32_t> bone;
    /** @brief Frog/snake belly ramp, normalized to three opaque entries. */
    std::vector<uint32_t> belly;
    /** @brief Jellyfish glow ramp, normalized to four opaque entries. */
    std::vector<uint32_t> glow;
    /** @brief Enable flying-skull floor shadow/light and ballistic floor sparks. */
    bool floor = true;
    /** @brief Critter floating height in logical pixels, validated in0..256. */
    double hover = 48;
    /** @brief Slime jump height in logical pixels, validated in0.01..256 to keep floor-light normalization finite. */
    double hop = 24;
};

/** @brief Holy-family parameters owned by each configuration; caller-thread affine. */
struct PixelFxHolySettings {
    /** @brief Feather-burst light color, validated as opaque packed RGBA. */
    uint32_t glow = 0xffc0f0ffu;
    /** @brief Divine-shield sphere radius in logical pixels, validated in1..256. */
    double radius = 29;
    /** @brief Healing-ring floor reach in logical pixels, validated in1..256. */
    double reach = 104;
    /** @brief Holy-pillar shaft count, validated as an integer in3..64. */
    int shafts = 7;
    /** @brief Smite stone material, validated as opaque packed RGBA. */
    uint32_t stone = 0xff66505au;
    /** @brief Resurrection ash ramp, owning opaque packed colors resampled to six steps. */
    std::vector<uint32_t> ash;
};

/** @brief Impact-family options owned by each configuration; caller-thread affine. */
struct PixelFxImpactSettings {
    /** @brief Blunt-hit pool light, validated as opaque packed RGBA. */
    uint32_t warm = 0xff8ac9ffu;
    /** @brief Blunt-hit dust material, validated as opaque packed RGBA. */
    uint32_t dust = 0xffa6a8b7u;
    /** @brief Faceted clod body material, validated as opaque packed RGBA. */
    uint32_t clod = 0xff5a5a6eu;
    /** @brief Bullet-impact wall stone material, validated as opaque packed RGBA. */
    uint32_t stone = 0xff79505bu;
    /** @brief Directional impact aim in radians, validated in-2pi..2pi. */
    double aim = .12;
    /** @brief Impact height above the floor in logical pixels, validated in0..256. */
    double height = 22;
    /** @brief Comic hit-line count, validated in0..128 at configuration preparation. */
    int lines = 7;
    /** @brief Blow-side chest offset multiplier, validated in-8..8; one unit is12logical pixels. */
    double side = 1;
    /** @brief Ground-slam shock-front reach in logical pixels, validated in0..512. */
    double reach = 104;
    /** @brief Shield glancing-spark ramp, bounded to1..64stops and normalized to4during preparation. */
    std::vector<uint32_t> sparks = {0xffa6f3ffu, 0xff6be2ffu, 0xff2a8affu, 0xff1d56f2u};
};

/** @brief Owning science-fiction materials/options, affine to the configuration caller thread. */
struct PixelFxScifiSettings {
    /** @brief Owning opaque tractor-pod hull ramp, bounded to1..64stops and normalized to6. */
    std::vector<uint32_t> hull;
    /** @brief Owning opaque tractor-rock ramp, bounded to1..64stops and normalized to5. */
    std::vector<uint32_t> stone;

    /** @brief Dome radius in logical pixels, validated per effect in0..256(force-field minimum0.125). */
    double radius = 44;
    /** @brief Owning opaque glancing-spark ramp, bounded to1..64stops and normalized to4. */
    std::vector<uint32_t> sparks = {0xffa6f3ffu, 0xff6be2ffu, 0xff2a8affu, 0xff1d56f2u};

    /** @brief Opaque metal ramp, bounded to1..64stops and normalized to6during preparation. */
    std::vector<uint32_t> metal = {0xff140d0eu, 0xff271c1eu, 0xff3e2e31u, 0xff59474bu, 0xff826a6fu, 0xffb0989du};
    /** @brief Hologram split color as opaque packed RGBA. */
    uint32_t split = 0xffd84fffu;
    /** @brief Jetpack plume length in logical pixels, validated in0..128. */
    double length = 26;
    /** @brief Warp-in column half-width in native pixels, in [0, 128]. */
    double width = 15;
    /** @brief Opaque EMP fringe color, copied during cold configuration preparation. */
    uint32_t fringe = 0xffd85affu;
};

/** @brief Owning arcane geometry parameters; validate through PixelFx configuration preparation.
 * @note Caller-thread values; inactive fields are ignored and no external references are retained. */
struct PixelFxArcaneSettings {
    /** @brief Portal horizontal radius in logical pixels; accepted range [.125,128]. */
    double portalWidth = 17;
    /** @brief Portal vertical radius in logical pixels; accepted range [.125,128]. */
    double portalHeight = 29;
};

/** @brief Owning shadow materials; prepare/commit validates only the active source options.
 * @note Caller-thread values; material buffers retain no external references. */
struct PixelFxShadowSettings {
    /** @brief Source rock, bone, pearl and soul ramps, normalized by their owning descriptors. */
    std::vector<uint32_t> rock, bone, pearl, soul;
    /** @brief Source dark tint, packed 0xAABBGGRR. */
    uint32_t dark = 0xff40102au;
};

/** @brief Owning nature options copied into prepared instances.
 * @note Caller-thread values; buffers retain no external references. */
struct PixelFxNatureSettings {
    /** @brief Source spark ramp, normalized to four packed colors. */
    std::vector<uint32_t> sparks;
    /** @brief Source nature glow ramp, normalized by its owning descriptor. */
    std::vector<uint32_t> glow;
    /** @brief Healing-bloom petal and heart ramps, each normalized to four packed colors. */
    std::vector<uint32_t> petal, heart;
    /** @brief Spore-burst cap/stem ramps, normalized to six/five packed colors. */
    std::vector<uint32_t> cap, stem;
    /** @brief Source nature soil ramp, normalized to eight packed colors. */
    std::vector<uint32_t> soil;
    /** @brief Vine-whip thorn ramp, normalized to four packed colors. */
    std::vector<uint32_t> thorn;
    /** @brief Tornado dust ramp, normalized to five packed colors. */
    std::vector<uint32_t> dust;
    /** @brief Toxic-puddle sheen ramp, normalized to four packed colors. */
    std::vector<uint32_t> sheen;
    /** @brief Toxic-puddle half-width in logical pixels; accepted range [1,256]. */
    double width = 42;
    /** @brief Thorns count in [1,64]; the source emits at least two. */
    int count = 6;
    /** @brief Source leaf ramps, normalized by the owning effect descriptor. */
    std::vector<uint32_t> leaves;
    /** @brief Poison-cloud horizontal expansion multiplier; accepted range [0,4]. */
    double spread = 1;
    /** @brief Quake rift half-reach in logical pixels; accepted range [0,512]. */
    double reach = 118;
    /** @brief Nature effect height in logical pixels; accepted range [1,256]. */
    double height = 62;
    /** @brief Boulder radius in logical pixels; accepted range [.5,32]. */
    double size = 11;
};

/** @brief Owning spawn/death options copied into prepared instances.
 * @note Caller-thread values; buffers retain no external references. */
struct PixelFxSpawnSettings {
    /** @brief Coin-drop smoke ramp, normalized to four packed colors. */
    std::vector<uint32_t> smoke;
    /** @brief Emitted coin count; accepted integral range [0,64], including smoke-only zero. */
    int coins = 7;
    /** @brief Disintegrate accent ramp, normalized to three packed colors. */
    std::vector<uint32_t> accent;
    /** @brief Disintegrate signed wind multiplier; accepted range [-4,4]. */
    double wind = 1;
    /** @brief Dissolve-burn ash ramp, normalized to three packed colors. */
    std::vector<uint32_t> ash;
    /** @brief Egg-hatch inner glow ramp, normalized to three packed colors. */
    std::vector<uint32_t> glow;
    /** @brief Egg-hatch source color number; signed/unsigned identity and packed RGBA bits are retained. */
    int64_t speckle = int64_t(0xff526a9au) - 4294967296LL;
    /** @brief Poof star ramp, normalized to three packed colors. */
    std::vector<uint32_t> stars;
    /** @brief Respawn beam / spawn portal width in logical pixels; active descriptors own bounds. */
    double width = 15;
    /** @brief Soul rise lift height in logical pixels; active descriptor owns bounds. */
    double height = 50;
    /** @brief Summon rise body, horn and floor materials, each normalized to five packed colors. */
    std::vector<uint32_t> body, horn, stone;
};

/** @brief Status-effect materials; active compiled descriptors declare bounds and source option names. */
struct PixelFxStatusSettings {
    /** @brief Haste dust tint; preserves the source packed color including raw alpha bits and zero. */
    uint32_t dust = 0xff90808au;
    /** @brief Stun bird and confusion question-mark ramps, normalized to four opaque colors. */
    std::vector<uint32_t> birds, marks;
    /** @brief Stun bird beak packed color; raw alpha bits and zero retain source semantics. */
    uint32_t beak = 0xff35aeffu;
    /** @brief Burning char and rage heat ramps, normalized to three/four opaque stops. */
    std::vector<uint32_t> charRamp, heat;
    /** @brief Body-status scalar tints preserving raw source color bits, including zero. */
    uint32_t smoke = 0xff66555bu, steam = 0xffeee8f2u, veins = 0xff3a1a2cu;
};
/** @brief Owning near-combat materials and lengths, validated by active source descriptors. */
struct PixelFxMeleeSettings {
    /** @brief Opaque particle ramp, bounded to1..64 stops and normalized to four during preparation. */
    std::vector<uint32_t> sparks;
    /** @brief Ground debris body color preserving source raw packed alpha bits and zero. */
    uint32_t stone = 0xff693f4au;
    /** @brief Thrust reach in logical pixels, validated in0..512 during configuration preparation. */
    double reach = 66;
    /** @brief Sword/spin radius in logical pixels, validated in0..512 during configuration preparation. */
    double radius = 42;
    /** @brief Uppercut outline preserving source packed alpha bits and zero. */
    uint32_t outline = 0xff080a2au;
    /** @brief Positive whip length in logical pixels, validated in1..512 during preparation. */
    double length = 76;
};
/** @brief Owning UI content, validated only by active source descriptors during preparation. */
struct PixelFxUiSettings {
    /** @brief UI text, bounded to0..64 UTF-8 codepoints; font and spacing follow the active descriptor. */
    std::string text = "-27";
    /** @brief Combo/crit label, bounded to0..64 UTF-8 codepoints and laid out once during preparation. */
    std::string label = "COMBO";
    /** @brief Counter/coin count, bounded by its active descriptor and displayed using source half-up rounding. */
    double count = 5;
    /** @brief Health before the hit, validated as a fraction in0..1 during preparation. */
    double from = .8;
    /** @brief Requested health after the hit, validated in0..1; rendering uses min(to,from). */
    double to = .45;
    /** @brief Countdown start in1..3, displayed using source half-up rounding. */
    double countdownFrom = 3;
    /** @brief Initial coin counter in-999999..999999, using source half-up rounding. */
    double coinFrom = 12;
    /** @brief Countdown terminal word, bounded to0..64 UTF-8 codepoints during preparation. */
    std::string word = "GO!";
    /** @brief Opaque countdown GO palette, bounded1..64 stops and normalized to eight during preparation. */
    std::vector<uint32_t> go;
    /** @brief Achievement title, bounded to0..64 UTF-8 codepoints and prepared as mini-font glyphs. */
    std::string title = "ACHIEVEMENT";
    /** @brief Opaque UI ribbon ramp, bounded1..64 stops and normalized by the active descriptor. */
    std::vector<uint32_t> ribbon;
};
/** @brief Owning screen-transition settings, validated only by active transition descriptors. */
struct PixelFxTransitionSettings {
    /** @brief Source raw packed0xAABBGGRR covering color, including zero/partial alpha. */
    uint32_t color = 0xff100a0bu;
    /** @brief Owning logical-pixel focus; active point declarations validate finite components in [-4096,4096]. */
    std::array<double, 2> focus{160, 100};
    /** @brief Source raw0xAABBGGRR glitch split color; independent of the primary ramp and cover. */
    uint32_t split = 0xff6f2dffu;
    /** @brief Owning opaque sparks ramp,1..64 stops normalized to four by the active Slash wipe recipe. */
    std::vector<uint32_t> sparks{0xffa6f3ffu, 0xff6be2ffu, 0xff2a8affu, 0xff1d56f2u};
};
/** @brief Active parameters copied into an instance; unused legacy slots are ignored. Packed colors are 0xAABBGGRR,
 * dark to light. */
struct PixelFxSettings {
    std::vector<uint32_t>     palette = std::vector<uint32_t>(8, 0u);
    PixelFxFireSettings       fire;
    PixelFxProjectileSettings projectile;
    PixelFxExplosionSettings  explosion;
    PixelFxWaterSettings      water;
    PixelFxWeatherSettings    weather;
    PixelFxLightningSettings  lightning;
    PixelFxIceSettings        ice;
    PixelFxPickupSettings     pickup;
    PixelFxCritterSettings    critter;
    PixelFxHolySettings       holy;
    PixelFxImpactSettings     impact;
    PixelFxScifiSettings      scifi;
    PixelFxArcaneSettings     arcane;
    PixelFxShadowSettings     shadow;
    PixelFxNatureSettings     nature;
    PixelFxSpawnSettings      spawn;
    PixelFxStatusSettings     status;
    PixelFxMeleeSettings      melee;
    PixelFxUiSettings         ui;
    PixelFxTransitionSettings transition;
    PixelFxActor              actor;
    uint32_t                  seed          = 1;
    double                    playbackSpeed = 1;
    double                    lightGain     = 1;
    int                       rays          = 8;
    int                       lines         = 64;
    double                    length        = 40;
};

/**
 * @brief Owning, validated configuration candidate; destruction cancels without publication.
 * @note Move-only and not default-constructible. May outlive the preparing instance and publish
 * to any instance of the same effect kind, once. No instance, VM or borrowed settings are retained.
 * Affine to its caller thread; no callbacks or implicit commit.
 */
class PixelFxPreparedConfig {
public:
    /** @brief Cancel an unpublished candidate and release owned settings. */
    EVENGINE_API_WORLD ~PixelFxPreparedConfig();
    /** @brief Transfer the candidate; the source becomes consumed. */
    EVENGINE_API_WORLD PixelFxPreparedConfig(PixelFxPreparedConfig&&) noexcept;
    /** @brief Cancel this candidate and transfer the source candidate. */
    EVENGINE_API_WORLD PixelFxPreparedConfig& operator=(PixelFxPreparedConfig&&) noexcept;
    PixelFxPreparedConfig(const PixelFxPreparedConfig&)            = delete;
    PixelFxPreparedConfig& operator=(const PixelFxPreparedConfig&) = delete;

private:
    struct State;
    std::unique_ptr<State> state_;
    PixelFxPreparedConfig(PixelFxKind kind, PixelFxSettings settings);
    friend class PixelFx;
};

/**
 * @brief Move-only prepared timeline state; destruction cancels without publication.
 * @note Owns replayed state and a shared immutable program identity. It can outlive the source
 * instance but can publish only to that same program generation. No temporary pointers survive.
 * Caller-thread affine; no callbacks, implicit commit or persistent representation.
 */
class PixelFxPreparedSeek {
public:
    /** @brief Cancel an unpublished seek. */
    EVENGINE_API_WORLD ~PixelFxPreparedSeek();
    /** @brief Transfer the candidate; the source becomes consumed. */
    EVENGINE_API_WORLD PixelFxPreparedSeek(PixelFxPreparedSeek&&) noexcept;
    /** @brief Cancel this candidate and transfer the source. */
    EVENGINE_API_WORLD PixelFxPreparedSeek& operator=(PixelFxPreparedSeek&&) noexcept;
    PixelFxPreparedSeek(const PixelFxPreparedSeek&)            = delete;
    PixelFxPreparedSeek& operator=(const PixelFxPreparedSeek&) = delete;

private:
    struct State;
    std::unique_ptr<State> state_;
    PixelFxPreparedSeek(std::shared_ptr<const pixelfx::Program> program,
                        std::unique_ptr<pixelfx::PlaybackState> playback);
    friend class PixelFx;
};

/**
 * @brief Instance-owned procedural pixel buffers with explicit absolute-time playback.
 * @note All calls are synchronous and affine to one owner thread; no callbacks, providers, ECS,
 * wall clock or global RNG. The instance retains no external pointers. Reentrant calls on different
 * instances are safe. Integer RNG matches upstream; floating-point raster boundaries have no bitwise
 * cross-platform guarantee. This is an in-process API, not a persistent format.
 */
class PixelFx {
public:
    /** @brief Draw the prepared actor's original pixels into an effect-sized caller-owned plane.
     * @param target Borrowed writable color pixels, exactly width*height; no reference is retained.
     * @return Failure leaves target unchanged; success clears and fills the plane without changing playback.
     * @note Caller-thread affine; no callbacks. Body drawing is separate from the effect color/light planes.
     * @cost Linear in the bounded actor and output pixel counts per explicit body refresh. */
    EVENGINE_API_WORLD Result<void> drawActor(std::span<uint32_t> target) const;
    /** @brief Query whether the source effect permits an external actor layer at its current playback time.
     * @return Owning visibility value; does not imply that the actor snapshot contains pixels.
     * @note Owner-thread only; synchronous, no callbacks, allocation, rebuild or timeline mutation.
     * Manual drawActor always paints the original snapshot independently of this composition policy. */
    [[nodiscard]] EVENGINE_API_WORLD PixelFxActorVisibility actorVisibility() const;
    /** @brief Returns a static immutable catalogue, valid for the process lifetime. */
    [[nodiscard]] EVENGINE_API_WORLD static std::span<const PixelFxInfo> catalogue();
    /** @brief Returns the built-in value settings for a known kind; throws on an invalid enum. */
    [[nodiscard]] EVENGINE_API_WORLD static PixelFxSettings defaults(PixelFxKind kind);
    /**
     * @brief Allocate an owning effect builder and its fixed-size color/light buffers.
     * @param kind Known algorithm; invalid enums throw std::invalid_argument.
     * @cost Allocates pixel planes and authored simulation/immutable geometry once per instance,
     * amortized over frames; prepared noise scales with field area and octave count.
     */
    EVENGINE_API_WORLD explicit PixelFx(PixelFxKind kind);
    /** @brief Release owned buffers, immutable program reference and mutable playback state. */
    EVENGINE_API_WORLD ~PixelFx();
    /** @brief Transfer ownership; the source is valid only for destruction or assignment. */
    EVENGINE_API_WORLD PixelFx(PixelFx&&) noexcept;
    /** @brief Release this state and transfer ownership from the source. */
    EVENGINE_API_WORLD PixelFx& operator=(PixelFx&&) noexcept;
    PixelFx(const PixelFx&)            = delete;
    PixelFx& operator=(const PixelFx&) = delete;
    /** @brief Return an independent owning settings copy. @cost Copies material ramps and actor entries once per
     * introspection call; retain outside frame loops. */
    [[nodiscard]] EVENGINE_API_WORLD PixelFxSettings settings() const;
    /**
     * @brief Copy, validate and normalize an owning candidate without changing an instance.
     * @param kind Known effect kind; unknown kinds return NotFound.
     * @param settings Synchronously borrowed input, copied before return.
     * @return Move-only candidate or structured field diagnostic; no partial mutation.
     * @cost Copies/resamples bounded ramps and builds authored state and immutable geometry/maps once per
     * preparation, amortized over frames and seeks; noise fields scale with area and octaves,
     * while ranked layouts/fields additionally sort bounded keys. Screen-sized preparation can dominate a draw;
     * reuse its immutable program across frames and seeks.
     * @note Caller-thread affine; no callbacks. Allocation failure may throw std::bad_alloc.
     */
    [[nodiscard]] EVENGINE_API_WORLD static Result<PixelFxPreparedConfig> prepareConfiguration(
        PixelFxKind kind, const PixelFxSettings& settings);
    /**
     * @brief Prepare a schema-versioned configuration Value through the same typed validator.
     * @param kind Target kind; the envelope effect id must match.
     * @param configuration Borrowed owning Value; no nodes are retained.
     * @return Validated owning candidate or path-aware diagnostic. Unknown fields/versions reject.
     * @cost Decodes bounded fields, resamples ramps and prepares authored state/immutable maps once per preparation.
     * Screen-sized maps can dominate a draw; reuse them outside frame loops across frames and seeks.
     * @note Caller-thread affine; no callbacks. Schema eve.pixelfx.configuration version 1.
     */
    [[nodiscard]] EVENGINE_API_WORLD static Result<PixelFxPreparedConfig> prepareConfigurationValue(
        PixelFxKind kind, const Value& configuration);
    /**
     * @brief Describe supported parameters, bounds, ramp sizes and actual defaults from source declarations.
     * @return Owning eve.pixelfx.parameters version 1 Value, or NotFound for an unknown kind.
     * @cost Copies parameter/default metadata once per introspection call; retain outside frame loops.
     * @note Caller-thread affine; no callbacks or retained references.
     */
    [[nodiscard]] EVENGINE_API_WORLD static Result<Value> describeParameters(PixelFxKind kind);
    /**
     * @brief Encode current active options as an independent versioned configuration Value.
     * @return Owning envelope; packed palette integers are opaque 0xAABBGGRR.
     * @cost Copies bounded active options and palettes once per introspection call.
     * @note Owner-thread only, no callbacks; returned nodes do not borrow instance storage.
     */
    [[nodiscard]] EVENGINE_API_WORLD Value configurationValue() const;
    /**
     * @brief Atomically publish and consume a prepared candidate of this effect kind.
     * @param prepared Owning candidate; mismatch/consumed failures preserve it and this instance.
     * @return Success or structured Rejected diagnostic. Existing pixels remain until render().
     * @note Transfers validated ownership without allocation/resampling; releases old simulation storage.
     * @note Owner-thread only; configuration publication resets playback and stop state.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> commitConfiguration(PixelFxPreparedConfig&& prepared);
    /**
     * @brief Compatibility facade over prepareConfiguration and commitConfiguration; rejected input throws
     * std::invalid_argument.
     * @cost Performs the same bounded copying/resampling as preparation, once per call.
     * @ownership settings is synchronously borrowed; normalized values are instance-owned.
     * @lifetime Input is required only during this call; no caller references are retained.
     * Only source-declared active fields are validated. Input palettes are opaque and resampled
     * to the declared ramp size; describeParameters(kind) supplies exact active bounds.
     * Rejection preserves configuration, playback and the previous frame.
     */
    EVENGINE_API_WORLD void configure(const PixelFxSettings& settings);
    /** @brief Return metadata by value; id refers to a static literal. */
    [[nodiscard]] EVENGINE_API_WORLD PixelFxInfo info() const;
    /** @brief Whether this effect owns a fixed-tick simulation kernel. */
    [[nodiscard]] EVENGINE_API_WORLD bool isSimulated() const;
    /**
     * @brief Advance injected playback delta; retain sub-tick time and never silently drop elapsed time.
     * @param deltaSeconds Finite wall-time delta in [0,60], scaled once by playbackSpeed.
     * @return Success or invalid-time diagnostic before mutation; total effect time is at most 1e12.
     * @cost Work scales with elapsed 1/120-second active ticks; normal frames amortize one or two ticks.
     * @note Owner-thread only. done() is advisory; explicitly accepted advances keep updating the kernel after duration
     * or stop. Kernel allocations may throw std::bad_alloc; no rollback is promised for process allocation failure.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> advance(double deltaSeconds);
    /**
     * @brief Reconstruct an uninterrupted timeline in an independent owning candidate.
     * @param effectSeconds Absolute effect time in [0,1e12], independent of playbackSpeed.
     * @return Candidate or diagnostic; active replay above one million ticks rejects before work.
     * @cost Shares prepared immutable maps/patterns; analytic playback allocation is constant; simulations replay
     * elapsed fixed ticks once per explicit seek preparation.
     * @note Does not mutate the owner or retain temporary data; clears previous stop state.
     * Owner-thread affine; no callbacks. Allocation failure may throw std::bad_alloc.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<PixelFxPreparedSeek> prepareSeek(double effectSeconds) const;
    /**
     * @brief Publish a same-program seek once; stale/wrong-owner/consumed candidates reject atomically.
     * @return Checked outcome. Old pixels stay valid until draw().
     * @note Owner-thread only, no allocation or replay; releases old owned simulation storage.
     */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> commitSeek(PixelFxPreparedSeek&& candidate);
    /** @brief Render the current owned timeline state; does not advance or seek.
     * @cost Raster work scales with authored elements and logical area per draw; planes are retained.
     * Volume effects may allocate frame-local Float32 scratch proportional to logical area.
     * @note Owner-thread only; no callbacks. A duration-complete effect may retain source tail pixels; only a completed
     * stop fade forces transparency. */
    EVENGINE_API_WORLD void draw();
    /** @brief Replace owned planes with the selected layer at the current playback time.
     * @param layer Back, Front or Both; invalid values preserve both planes.
     * @return Checked layer validation and drawing status.
     * @note Caller-thread affine; synchronous, with no retained painter or callbacks. Existing plane borrows
     * remain valid until owner mutation/destruction, but their contents are replaced on success.
     * @cost Runs selected authored raster passes per frame, reusing output planes; effect-local scratch costs
     * follow draw(). */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> drawLayer(PixelFxLayer layer);
    /** @brief Draw selected passes onto an existing caller-owned canvas without clearing it.
     * @param canvas Borrowed, disjoint color/light planes of exactly width*height; dimensions 1..4096,
     * origins within -4096..4096. No target references survive this synchronous call.
     * @param layer Explicit back/front/both selection; both uses source back-before-front order.
     * @param placement Finite x/y within -4096..4096 and angle within -64*pi..64*pi; no state is retained.
     * @return Invalid arguments leave canvas unchanged. Success does not advance time or replace retained planes.
     * @note Owner-thread only; no callbacks. A completed stop fade leaves the target unchanged; duration expiry alone
     * does not suppress source pixels.
     * @cost Runs authored raster passes and touches clipped logical pixels per explicit draw;
     * uses prepared simulation state and may allocate authored temporary particle records. */
    [[nodiscard]] EVENGINE_API_WORLD Result<void> drawToCanvas(PixelFxCanvas canvas, PixelFxLayer layer,
                                                               PixelFxPlacement placement) const;
    /** @brief Start the upstream 0.3-second fade once; repeated calls do not restart it.
     * @note Draw dispatch remains active: source chain children replace their own fade, and source unlight
     * operations may clear existing light even after the parent fade completes. done() is advisory. */
    EVENGINE_API_WORLD void stop();
    /** @brief Query one-shot duration expiry or completed stop fade; this advisory query never disables rendering. */
    [[nodiscard]] EVENGINE_API_WORLD bool done() const;
    /** @brief Query active kernel ticks actually executed; analytic effects return zero. */
    [[nodiscard]] EVENGINE_API_WORLD uint64_t activeTickCount() const;
    /** @brief Query unwrapped effect time, including retained sub-tick elapsed time. */
    [[nodiscard]] EVENGINE_API_WORLD double effectTime() const;
    /**
     * @brief Replace both planes with the frame at absolute seconds * playbackSpeed.
     * @param seconds Finite seconds in [0,1e12], with scaled effect time also at most 1e12; invalid input throws before
     * mutation. Analytic effects only; simulations reject this compatibility call before mutation. Loops wrap; analytic
     * one-shots clamp evaluation to their declared duration. Simulations use advance/draw or prepared seek.
     * @cost Raster work grows with logical pixel area and authored element count per frame;
     * pixel buffers are retained; authored fire passes may allocate temporary particle records.
     */
    EVENGINE_API_WORLD void render(double seconds);
    /** @brief Borrow the color plane until the next render/draw, move, assignment or destruction. */
    [[nodiscard]] EVENGINE_API_WORLD std::span<const uint32_t> color() const;
    /** @brief Borrow saturated additive RGB (alpha 255 on lit pixels), with the same lifetime as color(). */
    [[nodiscard]] EVENGINE_API_WORLD std::span<const uint32_t> light() const;

private:
    // Borrowed immutable descriptor has process lifetime; no plugin/provider unload dependency.
    const pixelfx::Descriptor*              descriptor_;
    PixelFxInfo                             info_;
    std::shared_ptr<const pixelfx::Program> program_;
    std::unique_ptr<pixelfx::PlaybackState> playback_;
    std::vector<uint32_t>                   color_, light_;
};
}  // namespace eve::stylize
