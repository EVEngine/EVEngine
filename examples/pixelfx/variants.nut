// Authored parameter examples; instantiated only for the selected effect.
local function custom_actor_data() {
    local a = { schema = "eve.pixelfx.actor", version = 1, source = "custom", width = 18, height = 34,
        scale = 1, x = -9.0, y = -34.0, head = [0.0, -34.0], chest = [0.0, -22.0],
        eyes = [[6, 5], [11, 5]], mask = [], pixels = [] };
    for (local y = 0; y < a.height; ++y) for (local x = 0; x < a.width; ++x) {
        local dx = x - 8.5, dy = y - 5.0;
        local head = dx * dx / 36.0 + dy * dy / 25.0 <= 1.0;
        local body = y >= 10 && y <= 23 && x >= 4 && x <= 13;
        local arms = y >= 11 && y <= 23 && ((x >= 1 && x <= 3) || (x >= 14 && x <= 16));
        local legs = y >= 24 && ((x >= 4 && x <= 6) || (x >= 11 && x <= 13));
        local filled = head || body || arms || legs;
        local eye = y == 5 && (x == 6 || x == 11);
        a.mask.push(filled ? 1 : 0);
        a.pixels.push(!filled ? 0 : eye ? 0xff302020 : head ? 0xffc8e0dc : body ? 0xffa08c58 : 0xff68503c);
    }
    return a;
}

export function custom_actor() { return custom_actor_data(); }

export function make_effect(id, variant) {
    local fx = eve.PixelFx(id);
    if (variant) {
        fx.setSeed(123);
        fx.setParameter("playbackSpeed", 0.55);
        fx.setParameter("lightGain", 0.55);
        if (id == "scifi/jetpack" || id == "scifi/teleporter-pad" || id == "scifi/hologram" || id == "scifi/laser-grid" || id == "scifi/energy-core") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX scifi snapshot failed";
            material.value.options.metal = [0xff38281c, 0xff705038, 0xffa88060];
            if (id == "scifi/jetpack") material.value.options.length = 40.5;
            if (id == "scifi/hologram") material.value.options.split = 0xff40e0ff;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX scifi preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX scifi publication failed";
        }
        if (id == "scifi/emp-blast") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX EMP snapshot failed";
            material.value.options.radius = 42.5;
            material.value.options.fringe = 0xff70ffd0;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX EMP preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX EMP publication failed";
        }
        if (id == "scifi/tractor-beam") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX tractor snapshot failed";
            material.value.options.hull = [0xff38281c, 0xff705038, 0xffa88060];
            material.value.options.stone = [0xff342438, 0xff785080, 0xffb890c0];
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX tractor preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX tractor publication failed";
        }
        if (id == "scifi/force-field") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX field snapshot failed";
            material.value.options.radius = 72.5;
            material.value.options.sparks = [0xffff8844, 0xffffdd88, 0xffcc6633];
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX field preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX field publication failed";
        }
        if (id == "impacts/heavy-landing" || id == "impacts/ground-slam") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX ground snapshot failed";
            material.value.options.stone = 0xff8c7460;
            if (id == "impacts/ground-slam") material.value.options.reach = 72.5;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX ground preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX ground publication failed";
        }
        if (id == "impacts/ricochet" || id == "impacts/shield-block") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX block/ricochet snapshot failed";
            if (id == "impacts/ricochet") material.value.options.stone = 0xff8c7460;
            else material.value.options.sparks = [0xffff8844, 0xffffdd88, 0xffcc6633];
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX block/ricochet preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX block/ricochet publication failed";
        }
        if (id == "impacts/parry") fx.setParameter("height", 32.5);
        if (id == "impacts/hit-flash") {
            fx.setParameter("lines", 12);
            fx.setParameter("side", -1.0);
        }
        if (id == "impacts/hit-spark") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX hit spark snapshot failed";
            material.value.options.aim = -0.5;
            material.value.options.height = 32.5;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX hit spark preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX hit spark publication failed";
        }
        if (id == "impacts/crit-hit") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX critical hit snapshot failed";
            material.value.options.aim = -0.5;
            material.value.options.height = 32.5;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX critical hit preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX critical hit publication failed";
        }
        if (id == "impacts/bullet-impact") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX bullet impact snapshot failed";
            material.value.options.stone = 0xff8c7460;
            material.value.options.height = 32.5;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX bullet impact preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX bullet impact publication failed";
        }
        if (id == "impacts/blunt-hit") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX blunt hit snapshot failed";
            material.value.options.warm = 0xffe0b060;
            material.value.options.dust = 0xffb08060;
            material.value.options.clod = 0xff886644;
            material.value.options.height = 32.5;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX blunt hit preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX blunt hit publication failed";
        }
        if (id == "holy/resurrection") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX resurrection snapshot failed";
            material.value.options.ash = [0xff403020, 0xffc0a080];
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX resurrection preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX resurrection publication failed";
        }
        if (id == "holy/smite") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX smite snapshot failed";
            material.value.options.stone = 0xffc08040;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX smite preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX smite publication failed";
        }
        if (id == "holy/holy-pillar") fx.setParameter("shafts", 8);
        if (id == "holy/heal-aoe") fx.setParameter("reach", 80.5);
        if (id == "holy/divine-shield") fx.setParameter("radius", 34.5);
        if (id == "holy/feather-burst") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX feather snapshot failed";
            material.value.options.glow = 0xffffaa55;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX feather preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX feather publication failed";
        }
        if (id == "holy/sunburst") fx.setParameter("rays", 12);
        if (id == "projectiles/laser-bolt") fx.setParameter("length", 70);
        if (id == "impacts/impact-frame") fx.setParameter("lines", 24);
        if (id == "fire/flame-pillar") {
            fx.setParameter("height", 105);
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX material snapshot failed";
            material.value.options.stone = 0xff916a45;
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX material preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX material publication failed";
        }
        if (id == "fire/fire-breath") {
            fx.setParameter("spread", 0.36);
            fx.setParameter("speed", 410);
        }
        if (id == "fire/flamethrower") {
            fx.setParameter("reach", 90);
            fx.setParameter("speed", 210);
            fx.setRampColor("pilot", 0, 39, 58, 130);
            fx.setRampColor("pilot", 1, 76, 145, 216);
            fx.setRampColor("pilot", 2, 210, 240, 255);
        }
        if (id == "fire/fireball") {
            fx.setParameter("radius", 8);
            fx.setParameter("speed", 200);
        }
        if (id == "projectiles/flare") {
            fx.setParameter("height", 110);
            fx.setRampColor("smoke", 4, 125, 170, 195);
        }
        if (id == "weather/falling-leaves") {
            fx.setRampColor("reds", 3, 90, 130, 210);
            fx.setRampColor("golds", 4, 180, 210, 245);
        }
        if (id == "lightning/spark-arc" || id == "lightning/ball-lightning" || id == "lightning/thunderclap" || id == "lightning/lightning-strike" || id == "lightning/tesla-coil" || id == "lightning/storm-cloud" || id == "lightning/electrified" || id == "lightning/charge-up" || id == "lightning/chain-lightning") fx.setRampColor("sparks", 2, 255, 190, 110);
        if (id == "lightning/tesla-coil") { fx.setRampColor("copper", 3, 70, 130, 195); fx.setRampColor("steel", 5, 210, 235, 250); }
        if (id == "lightning/storm-cloud") { fx.setRampColor("cloud", 4, 85, 105, 160); fx.setRampColor("rain", 2, 95, 190, 235); }
        if (id == "lightning/charge-up") fx.setParameter("charge", 0.95);
        if (id == "lightning/plasma-ball") fx.setParameter("tendrils", 5);
        if (id == "ice/blizzard-vortex") fx.setParameter("height", 93.5);
        if (id == "ice/frost-breath") { fx.setParameter("spread", 0.37); fx.setParameter("power", 1.4); }
        if (id == "ice/frost-nova") fx.setParameter("reach", 76);
        if (id == "lightning/thunderclap") fx.setParameter("reach", 72.5);
        if (id == "weather/storm") fx.setRampColor("bolt", 3, 255, 170, 85);
        if (id == "weather/fireflies") fx.setRampColor("grass", 4, 85, 105, 170);
        if (id == "weather/shooting-stars") fx.setRampColor("sky", 2, 80, 100, 150);
        if (id == "weather/rain") fx.setParameter("density", 0.6);
        if (id == "weather/aurora") fx.setRampColor("upper", 4, 120, 80, 220);
        if (id == "fire/meteor") fx.setRampColor("rock", 2, 70, 100, 130);
        if (id == "water/geyser") fx.setParameter("height", 72.5);
        if (id == "water/water-jet") fx.setParameter("length", 90);
        if (id == "water/waterfall") {
            local config = fx.getConfiguration();
            if (!config.ok) throw "PixelFX waterfall snapshot failed";
            config.value.options.height = 64.0;
            config.value.options.width = 10.0;
            config.value.options.ledge = false;
            local candidate = fx.prepareConfiguration(config.value);
            if (!candidate.ok) throw "PixelFX waterfall preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX waterfall publication failed";
        }
        if (id == "water/water-orb") fx.setParameter("radius", 25);
        if (id == "water/splash") {
            local config = fx.getConfiguration();
            if (!config.ok) throw "PixelFX splash snapshot failed";
            config.value.options.stone = false;
            local candidate = fx.prepareConfiguration(config.value);
            if (!candidate.ok) throw "PixelFX splash preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX splash publication failed";
        }
        if (id == "water/drip") {
            local config = fx.getConfiguration();
            if (!config.ok || typeof config.value.options.source != "bool") throw "PixelFX boolean snapshot failed";
            config.value.options.source = false;
            config.value.options.height = 45.0;
            local candidate = fx.prepareConfiguration(config.value);
            if (!candidate.ok) throw "PixelFX boolean preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX boolean publication failed";
            local published = fx.getConfiguration();
            if (!published.ok || published.value.options.source != false || published.value.options.height != 45.0) throw "PixelFX water settings mismatch";
        }
        if (id == "explosions/nuke") fx.setParameter("dust", 0xffc08050);
        if (id == "explosions/grenade") {
            fx.setRampColor("body", 3, 60, 145, 210);
            fx.setRampColor("body", 4, 130, 210, 245);
        }
        if (id == "explosions/smoke-bomb") {
            local material = fx.getConfiguration();
            if (!material.ok) throw "PixelFX smoke material snapshot failed";
            material.value.options.sparks = [0xfff0d090, 0xffe0a050, 0xffc08030, 0xff906020, 0xff604010];
            local candidate = fx.prepareConfiguration(material.value);
            if (!candidate.ok) throw "PixelFX smoke material preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX smoke material publication failed";
            fx.setRampColor("canister", 3, 60, 145, 210);
            local published = fx.getConfiguration();
            if (!published.ok || published.value.options.sparks.len() != 4) throw "PixelFX spark ramp did not resolve to four steps";
        }
        if (id == "explosions/cartoon-pow") {
            local word = fx.getConfiguration();
            if (!word.ok) throw "PixelFX text snapshot failed";
            word.value.options.text = "BAM?";
            local candidate = fx.prepareConfiguration(word.value);
            if (!candidate.ok) throw "PixelFX text preparation failed";
            local applied = fx.commitConfiguration(candidate.value);
            if (!applied.ok) throw "PixelFX text publication failed";
        }
        if (id == "explosions/plasma-explosion") {
            fx.setRampColor("accent", 2, 180, 45, 120);
            fx.setRampColor("accent", 3, 240, 100, 170);
        }
        if (id == "explosions/firework-burst") {
            fx.setRampColor("core", 3, 40, 110, 200);
            fx.setRampColor("core", 4, 70, 160, 230);
        }
        if (id == "explosions/firework-rocket") {
            fx.setRampColor("tips", 2, 170, 60, 200);
            fx.setRampColor("tips", 3, 230, 130, 240);
        }
        if (id == "explosions/dirt-explosion") {
            fx.setRampColor("fire", 3, 40, 110, 200);
            fx.setRampColor("fire", 4, 70, 160, 230);
            fx.setRampColor("fire", 5, 140, 220, 250);
        }
        if (id == "explosions/barrel-explosion") {
            fx.setRampColor("barrel", 3, 45, 110, 180);
            fx.setRampColor("barrel", 4, 80, 170, 220);
            fx.setRampColor("barrel", 5, 150, 220, 250);
        }
        if (id == "explosions/big-explosion") fx.setParameter("stone", 0xff907050);
        if (id == "explosions/small-explosion") {
            fx.setParameter("size", 1.5);
            fx.setParameter("stone", 0xff907050);
        }
        if (id == "projectiles/cannonball") {
            fx.setParameter("height", 30);
            fx.setRampColor("iron", 3, 150, 210, 240);
            fx.setRampColor("smoke", 4, 130, 170, 195);
        }
        if (id == "projectiles/rocket") {
            fx.setParameter("speed", 190);
            fx.setRampColor("metal", 5, 160, 215, 240);
            fx.setRampColor("paint", 3, 50, 140, 210);
            fx.setRampColor("smoke", 4, 130, 170, 195);
        }
        if (id == "projectiles/homing-missile") {
            fx.setRampColor("metal", 4, 150, 210, 230);
            fx.setRampColor("smoke", 4, 130, 170, 195);
            fx.setParameter("stone", 0xff906848);
        }
        if (id == "projectiles/magic-missile") {
            fx.setParameter("height", 40);
            fx.setParameter("reach", 150);
        }
        if (id == "projectiles/muzzle-smg") {
            fx.setParameter("height", 40);
            fx.setRampColor("brass", 3, 180, 220, 255);
            fx.setRampColor("smoke", 4, 120, 160, 185);
        }
        if (id == "projectiles/plasma-shot") {
            fx.setParameter("size", 9);
            fx.setParameter("speed", 350);
        }
        if (id == "projectiles/bullet-tracer") {
            fx.setParameter("length", 80);
            fx.setParameter("speed", 720);
            fx.setRampColor("smoke", 3, 96, 140, 175);
        }
        if (id == "projectiles/muzzle-pistol" || id == "projectiles/muzzle-shotgun") {
            fx.setParameter("height", 40);
            fx.setRampColor("smoke", 4, 120, 160, 185);
            if (id == "projectiles/muzzle-pistol") fx.setRampColor("brass", 3, 180, 220, 255);
            else fx.setRampColor("shell", 2, 70, 170, 220);
        }
        if (id == "projectiles/arrow") {
            fx.setRampColor("wood", 2, 160, 96, 56);
            fx.setRampColor("steel", 3, 170, 224, 255);
            fx.setParameter("wind", 0xffc0ff80);
        }
        if (id == "projectiles/energy-disc") {
            fx.setParameter("size", 12);
            fx.setParameter("teeth", 13);
            fx.setParameter("speed", 350);
        }
        if (id == "fire/fire-ring") fx.setParameter("reach", 70);
        if (id == "fire/campfire") {
            fx.setParameter("height", 35);
            fx.setRampColor("wood", 3, 96, 128, 144);
            fx.setRampColor("wood", 4, 144, 176, 184);
        }
        if (id == "critters/frog") fx.setRampColor("belly", 1, 180, 140, 225);
        if (id == "critters/slime") fx.setParameter("hop", 34.5);
        if (id == "critters/jellyfish") fx.setRampColor("glow", 2, 255, 170, 100);
        if (id == "critters/snake") fx.setRampColor("belly", 1, 190, 140, 225);
        if (id == "critters/wisp") fx.setParameter("hover", 46.5);
        if (id == "critters/ghost") fx.setParameter("hover", 22.5);
        if (id == "critters/flying-skull") {
            fx.setParameter("hover", 50.5);
            local skullConfig = fx.getConfiguration();
            if (!skullConfig.ok) throw "PixelFX skull snapshot failed";
            skullConfig.value.options.floor = false;
            local skullCandidate = fx.prepareConfiguration(skullConfig.value);
            if (!skullCandidate.ok) throw "PixelFX skull preparation failed";
            local skullApplied = fx.commitConfiguration(skullCandidate.value);
            if (!skullApplied.ok) throw "PixelFX skull publication failed";
            fx.setRampColor("bone", 4, 130, 180, 205);
        }
        if (id == "critters/fish-school") fx.setRampColor("stripe", 1, 200, 160, 245);
        if (id == "critters/bat") fx.setRampColor("eyes", 1, 120, 220, 255);
        if (id == "critters/eyeball") {
            fx.setParameter("hover", 38.5);
            fx.setRampColor("iris", 3, 180, 120, 255);
            fx.setRampColor("flesh", 3, 150, 100, 180);
        }
        if (id == "critters/butterfly") fx.setRampColor("spots", 2, 190, 255, 120);
        if (id == "critters/bird") fx.setRampColor("breast", 2, 180, 220, 100);
        if (id == "pickups/chest-open") {
            fx.setParameter("coins", 6);
            fx.setRampColor("wood", 4, 80, 120, 160);
            fx.setRampColor("wood", 5, 115, 160, 200);
        }
        if (id == "ice/ice-spikes") {
            fx.setParameter("reach", 141.5);
            fx.setParameter("height", 43.5);
        }
        if (id == "scifi/warp-in") fx.setParameter("width", 27.5);
        if (id == "ice/ice-lance") fx.setParameter("length", 55.5);
        if (id == "ice/snowflake-burst") fx.setParameter("count", 13);
        if (id == "ice/ice-crystal") fx.setParameter("height", 47.5);
        if (id == "fire/fire-tornado") fx.setParameter("height", 55);
        if (id == "fire/torch") {
            fx.setParameter("height", 28);
            fx.setRampColor("wood", 4, 150, 90, 190);
            fx.setRampColor("iron", 3, 85, 115, 140);
        }
        if (id == "fire/brazier") {
            fx.setParameter("height", 42);
            fx.setRampColor("iron", 2, 100, 140, 170);
        }
        if (id == "fire/candle") {
            fx.setParameter("height", 17);
            fx.setRampColor("wax", 4, 100, 175, 200);
            fx.setRampColor("brass", 3, 100, 150, 220);
        }
        if (id != "impacts/impact-frame") {
            local paletteCount = fx.getPaletteColorCount();
            if (paletteCount > 3) fx.setPaletteColor(3, 30, 95, 175);
            if (paletteCount > 4) fx.setPaletteColor(4, 60, 155, 235);
            if (paletteCount > 5) fx.setPaletteColor(5, 120, 205, 255);
            if (paletteCount > 6) fx.setPaletteColor(6, 205, 245, 255);
        }
    }
    if (variant && id == "shadow/soul-drain") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX soul drain snapshot failed";
        config.value.options.soul = [4283577879, 4289891659, 4293780668, 4294966513];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX soul drain material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX soul drain material publication failed";
    }
    if (variant && id == "shadow/necro-skulls") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX necro skulls snapshot failed";
        config.value.options.bone = [4283577879, 4289891659, 4293780668, 4294966513];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX necro skulls material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX necro skulls material publication failed";
    }
    if (variant && id == "shadow/life-steal") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX life steal snapshot failed";
        config.value.options.actor = custom_actor_data();
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX life steal actor preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX life steal actor publication failed";
    }
    if (variant && id == "shadow/eclipse") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX eclipse snapshot failed";
        config.value.options.pearl = [4283577879, 4289891659, 4293780668, 4294966513];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX eclipse material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX eclipse material publication failed";
    }
    if (variant && id == "shadow/curse") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX curse snapshot failed";
        config.value.options.bone = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        config.value.options.actor = custom_actor_data();
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX curse material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX curse material publication failed";
    }
    if (variant && id == "shadow/black-hole") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX black hole snapshot failed";
        config.value.options.rock = [4279373632, 4281862768, 4286604976, 4292677584];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX black hole material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX black hole material publication failed";
    }
    if (variant && (id == "melee/claw-swipe" || id == "melee/spear-sweep" || id == "melee/thrust" || id == "melee/sword-slash" || id == "melee/double-slash" || id == "melee/dagger-flurry" || id == "melee/uppercut" || id == "melee/spin-slash" || id == "melee/whip-crack")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX melee snapshot failed";
        config.value.options.sparks = [4279373632, 4286604976, 4293980400, 4294967295];
        if (id == "melee/spear-sweep") config.value.options.stone = 4293980400;
        if (id == "melee/thrust") config.value.options.reach = 110.0;
        if (id == "melee/sword-slash") config.value.options.radius = 56.0;
        if (id == "melee/uppercut") config.value.options.outline = 4279373632;
        if (id == "melee/spin-slash") config.value.options.radius = 60.0;
        if (id == "melee/whip-crack") config.value.options.length = 108.0;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX melee preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX melee publication failed";
    }
    if (variant && id.find("transitions/") == 0) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX transition snapshot failed";
        config.value.options.color = 4279373632;
        if ("sparks" in config.value.options) config.value.options.sparks = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        if ("split" in config.value.options) config.value.options.split = 4292677584;
        if ("focus" in config.value.options) {
            local rejected = false;
            try { fx.setRampColor("focus", 0, 255, 0, 0); }
            catch (error) { rejected = true; }
            if (!rejected) throw "PixelFX coordinate accepted as a palette";
            config.value.options.focus = [210.5, 72.25];
        }
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX transition preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX transition publication failed";
    }
    if (variant && (id == "ui/achievement" || id == "ui/quest-complete" || id == "ui/level-up")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX reward snapshot failed";
        if (id == "ui/achievement") {
            config.value.options.title = "VICTORY";
            config.value.options.text = "ALL CLEAR";
        } else if (id == "ui/quest-complete") config.value.options.text = "ALL CLEAR!";
        else config.value.options.text = "VICTORY!";
        if (id != "ui/level-up") config.value.options.ribbon = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX reward preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX reward publication failed";
    }
    if (variant && (id == "ui/countdown" || id == "ui/coin-collect")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX UI counter snapshot failed";
        if (id == "ui/countdown") {
            config.value.options.from = 2.5;
            config.value.options.word = "START!";
            config.value.options.go = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        } else {
            config.value.options.count = 6.5;
            config.value.options.from = 99.5;
        }
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX UI counter preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX UI counter publication failed";
    }
    if (variant && (id == "ui/crit-number" || id == "ui/health-bar-hit")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX UI impact snapshot failed";
        if (id == "ui/crit-number") {
            config.value.options.text = "999!";
            config.value.options.label = "HIT";
        } else {
            config.value.options.from = 1.0;
            config.value.options.to = 0.125;
        }
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX UI impact preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX UI impact publication failed";
    }
    if (variant && (id == "ui/damage-number" || id == "ui/combo-counter")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX UI snapshot failed";
        if (id == "ui/damage-number") config.value.options.text = "+999";
        else {
            config.value.options.count = 12.5;
            config.value.options.label = "HIT!";
        }
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX UI preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX UI publication failed";
    }
    if (variant && (id == "melee/heavy-cleave" || id == "melee/hammer-smash")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX melee ground snapshot failed";
        config.value.options.stone = 4293980400;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX melee ground preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX melee ground publication failed";
    }
    if (variant && (id == "status/burning" || id == "status/poisoned" || id == "status/rage")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX body status snapshot failed";
        if (id == "status/burning") {
            config.value.options.char = [4279373632, 4286604976, 4293980400];
            config.value.options.smoke = 4294967295;
        } else if (id == "status/poisoned") config.value.options.veins = 4294967295;
        else {
            config.value.options.heat = [4279373632, 4286604976, 4293980400, 4294967295];
            config.value.options.steam = 4294967295;
        }
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX body status material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX body status material publication failed";
    }
    if (variant && (id == "status/stunned" || id == "status/confused")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX status icon snapshot failed";
        if (id == "status/stunned") {
            config.value.options.birds = [4279373632, 4286604976, 4293980400, 4294967295];
            config.value.options.beak = 4294967295;
        } else config.value.options.marks = [4279373632, 4281862768, 4292677584, 4294967295];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX status icon material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX status icon material publication failed";
    }
    if (variant && id == "status/haste") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX haste snapshot failed";
        config.value.options.dust = 4294967295;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX haste material preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX haste material publication failed";
    }
    if (variant && id == "spawn/poof") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX poof snapshot failed";
        config.value.options.stars = [4294967295, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX poof preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX poof publication failed";
    }
    if (variant && id == "spawn/respawn-beam") fx.setParameter("width", 23.5);
    if (variant && id == "spawn/soul-rise") fx.setParameter("height", 72.5);
    if (variant && id == "spawn/spawn-portal") fx.setParameter("width", 45.5);
    if (variant && id == "spawn/summon-rise") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX summon rise snapshot failed";
        config.value.options.body = [4281862768, 4294967295];
        config.value.options.horn = [4294967295, 4294901760];
        config.value.options.stone = [4280951850, 4285556334];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX summon rise preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX summon rise publication failed";
    }
    if (variant && id == "spawn/egg-hatch") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX egg hatch snapshot failed";
        config.value.options.glow = [4294967295, 4294901760];
        config.value.options.speckle = 4281558681;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX egg hatch preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX egg hatch publication failed";
    }
    if (variant && id == "spawn/dissolve-burn") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX dissolve burn snapshot failed";
        config.value.options.ash = [4294967295, 4290748608, 4284631110];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX dissolve burn preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX dissolve burn publication failed";
    }
    if (variant && id == "spawn/disintegrate") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX disintegrate snapshot failed";
        config.value.options.wind = -1.5;
        config.value.options.accent = [4294967295, 4294952448, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX disintegrate preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX disintegrate publication failed";
    }
    if (variant && id == "spawn/coin-drop") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX coin drop snapshot failed";
        config.value.options.coins = 11;
        config.value.options.smoke = [4294967295, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX coin drop preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX coin drop publication failed";
    }
    if (variant && id == "nature/vine-whip") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX vine whip snapshot failed";
        config.value.options.thorn = [4294967295, 4294952448, 4294934656, 4294901760];
        config.value.options.soil = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX vine whip preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX vine whip publication failed";
    }
    if (variant && id == "nature/toxic-puddle") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX toxic puddle snapshot failed";
        config.value.options.width = 63.5;
        config.value.options.sheen = [4294967295, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX toxic puddle preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX toxic puddle publication failed";
    }
    if (variant && id == "nature/tornado") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX tornado snapshot failed";
        config.value.options.height = 144.0;
        config.value.options.dust = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        config.value.options.leaves = [4294967295, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX tornado preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX tornado publication failed";
    }
    if (variant && id == "nature/thorns") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX thorns snapshot failed";
        config.value.options.count = 9;
        config.value.options.glow = [4294967295, 4294952448, 4294934656, 4294901760];
        config.value.options.soil = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX thorns preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX thorns publication failed";
    }
    if (variant && id == "nature/spore-burst") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX spore burst snapshot failed";
        config.value.options.cap = [4280295456, 4282409024, 4284522592, 4286636160, 4288749728, 4292996351];
        config.value.options.stem = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX spore burst preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX spore burst publication failed";
    }
    if (variant && id == "nature/root-snare") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX root snare snapshot failed";
        config.value.options.glow = [4294967295, 4294952448, 4294934656, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX root snare preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX root snare publication failed";
    }
    if (variant && id == "nature/quake") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX quake snapshot failed";
        config.value.options.reach = 154.0;
        config.value.options.glow = [4294967295, 4294952448, 4294934656, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX quake preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX quake publication failed";
    }
    if (variant && id == "nature/poison-cloud") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX poison cloud snapshot failed";
        config.value.options.spread = 0.65;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX poison cloud preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX poison cloud publication failed";
    }
    if (variant && id == "nature/leaf-storm") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX leaf storm snapshot failed";
        config.value.options.leaves = [4294967295, 4294901760, 4278255360];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX leaf storm preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX leaf storm publication failed";
    }
    if (variant && id == "nature/healing-bloom") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX healing bloom snapshot failed";
        config.value.options.petal = [4279373632, 4281862768, 4286604976, 4292677584];
        config.value.options.heart = [4294967295, 4294952448, 4294934656, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX healing bloom preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX healing bloom publication failed";
    }
    if (variant && id == "nature/earth-spike") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX earth spike snapshot failed";
        config.value.options.height = 78.0;
        config.value.options.glow = [4279373632, 4281862768, 4286604976, 4292677584, 4293980400];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX earth spike preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX earth spike publication failed";
    }
    if (variant && id == "nature/boulder") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX boulder snapshot failed";
        config.value.options.size = 16.0;
        config.value.options.sparks = [4294967295, 4294952448, 4294934656, 4294901760];
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX boulder preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX boulder publication failed";
    }
    if (variant && id == "arcane/portal") {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX portal snapshot failed";
        config.value.options.width = 24.0;
        config.value.options.height = 23.0;
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX portal geometry preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX portal geometry publication failed";
    }
    if (variant && (id.find("moves/") == 0 || id == "ui/level-up" || id == "ui/exclamation" || id == "melee/claw-swipe" || id == "melee/spear-sweep" || id == "melee/thrust" || id == "melee/sword-slash" || id == "melee/double-slash" || id == "melee/dagger-flurry" || id == "melee/uppercut" || id == "melee/spin-slash" || id == "melee/whip-crack" || id == "melee/scythe-reap" || id == "melee/heavy-cleave" || id == "melee/hammer-smash" || id == "status/frozen" || id == "status/petrified" || id == "status/burning" || id == "status/poisoned" || id == "status/rage" || id == "status/stunned" || id == "status/sleep" || id == "status/confused" || id == "status/charmed" || id == "status/cursed" || id == "status/haste" || id == "status/slowed" || id == "status/bleeding" || id == "spawn/respawn-beam" || id == "spawn/shatter-death" || id == "spawn/soul-rise" || id == "spawn/dissolve-burn" || id == "spawn/disintegrate" || id == "nature/root-snare" || id == "nature/leaf-storm" || id == "arcane/blink-out" || id == "arcane/blink-in" || id == "arcane/arcane-missiles" || id == "arcane/arcane-beam" || id == "arcane/spell-charge" || id == "scifi/glitch" || id == "scifi/warp-in" || id == "scifi/warp-out" || id == "scifi/force-field" || id == "scifi/scanner" || id == "impacts/heavy-landing" || id == "impacts/shield-block" || id == "impacts/hit-flash" || id == "holy/resurrection" || id == "holy/heal" || id == "holy/divine-shield" || id == "holy/blessing" || id == "holy/halo" || id == "lightning/thunderclap" || id == "lightning/electrified" || id == "lightning/charge-up" || id == "lightning/chain-lightning" || id == "ice/freeze")) {
        local config = fx.getConfiguration();
        if (!config.ok) throw "PixelFX custom actor snapshot failed";
        config.value.options.actor = custom_actor_data();
        local candidate = fx.prepareConfiguration(config.value);
        if (!candidate.ok) throw "PixelFX custom actor preparation failed";
        local applied = fx.commitConfiguration(candidate.value);
        if (!applied.ok) throw "PixelFX custom actor publication failed";
    }
    return fx;
}
