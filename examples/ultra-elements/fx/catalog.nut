// Auto-derived from Humpalumps/ultra-elements spell modules (params/ranges/presets/timing).
// Presentation is EVEngine-native; values stay live-editable in the lab UI.
function catalog() {
    local elements = [
        { id="lightning", name="Lightning", hue="#7fb4ff", key="1" },
        { id="fire", name="Fire", hue="#ff7a1a", key="2" },
        { id="water", name="Water", hue="#2fa8ff", key="3" },
        { id="ice", name="Ice", hue="#9ff0ff", key="4" },
        { id="earth", name="Earth", hue="#c08a4a", key="5" },
        { id="wind", name="Wind", hue="#c8f5e4", key="6" },
        { id="void", name="Void", hue="#8a3dff", key="7" },
        { id="light", name="Light", hue="#ffe08a", key="8" },
        { id="acid", name="Acid", hue="#8aff3c", key="9" },
        { id="arcane", name="Arcane", hue="#ff4fd8", key="0" },
        { id="hybrid", name="Hybrid", hue="#ffffff", key="h" },
    ];
    local spells = [
        {
            id="lightning", name="Lightning", element="lightning", shape="strike",
            forms=["Thunderstrike", "Chain Arc"],
            params={ intensity=1.0, branches=8.0, jag=0.085, restrikes=3.0, sparks=1.0, crawl=1.0, cloud=1.0, damp=1.0, core="#d6e6ff", glow="#5b78ff" },
            ranges={ intensity=[0.1, 3.0, 0.01], branches=[0.0, 8.0, 1.0], jag=[0.02, 0.3, 0.005], restrikes=[1.0, 5.0, 1.0], sparks=[0.0, 3.0, 0.01], crawl=[0.0, 3.0, 0.01], cloud=[0.0, 2.0, 0.01], damp=[0.0, 2.0, 0.01] },
            timing={ charge=0.6, impact=0.8, linger=1.6, sustain=1.8 },
            presets=[{ name="crimson", values={ core="#ffd0d0", glow="#ff2a3c" } }, { name="violet", values={ core="#efd8ff", glow="#8a3dff" } }, { name="white hot", values={ core="#ffffff", glow="#b8c8ff", intensity=1.6 } }]
        },
        {
            id="ball-lightning", name="Ball Lightning", element="lightning", shape="projectile",
            forms=["Ball Lightning"],
            params={ size=1.0, speed=3.6, wander=1.0, intensity=1.0, arcs=1.0, reach=5.5, instability=1.0, blast=1.0, bolts=12.0, sparks=1.0, core="#eef3ff", glow="#5b78ff", plasma="#a48dff" },
            ranges={ size=[0.4, 2.5, 0.01], speed=[1.0, 12.0, 0.1], wander=[0.0, 3.0, 0.01], intensity=[0.1, 3.0, 0.01], arcs=[0.0, 3.0, 0.01], reach=[1.0, 9.0, 0.1], instability=[0.0, 2.0, 0.01], blast=[0.3, 2.5, 0.01], bolts=[0.0, 14.0, 1.0], sparks=[0.0, 3.0, 0.01] },
            timing={ charge=0.9, impact=0.5, linger=2.6, sustain=8.0 },
            presets=[{ name="st elmo", values={ core="#e8fffb", glow="#1fd6c8", plasma="#7fffe6", wander=1.4 } }]
        },
        {
            id="thunderstorm", name="Thunderstorm", element="lightning", shape="area",
            forms=["Thunderstorm"],
            params={ radius=9.0, duration=8.0, height=9.5, strikes=1.0, intensity=1.0, branches=7.0, jag=0.12, spin=1.0, rain=1.0, sheets=1.0, wind=1.0, angle=195.0, gust=1.0, darkness=1.0, wetness=1.0, core="#dfe9ff", glow="#6f80ff", cloud="#8b8e92" },
            ranges={ radius=[5.0, 16.0, 0.1], duration=[3.0, 16.0, 0.1], height=[7.5, 14.0, 0.1], strikes=[0.2, 2.5, 0.01], intensity=[0.2, 2.5, 0.01], branches=[0.0, 8.0, 1.0], jag=[0.04, 0.25, 0.005], spin=[0.0, 3.0, 0.01], rain=[0.0, 2.0, 0.01], sheets=[0.0, 2.5, 0.01], wind=[0.0, 2.5, 0.01], angle=[0.0, 360.0, 1.0], gust=[0.0, 2.0, 0.01], darkness=[0.0, 1.5, 0.01], wetness=[0.0, 2.0, 0.01] },
            timing={ charge=1.6, impact=0.3, linger=11.0, sustain=2.0 },
            presets=[{ name="supercell", values={ radius=13.0, spin=1.9, strikes=0.85, height=10.5, rain=0.7, sheets=1.5, cloud="#6f7f8c" } }, { name="monsoon", values={ rain=1.9, sheets=1.7, strikes=0.55, wind=1.5, wetness=1.6, cloud="#8a919c" } }, { name="dry lightning", values={ rain=0.08, sheets=0.3, strikes=1.9, wetness=0.1, gust=1.5, cloud="#8a7a6c", glow="#a07dff", core="#efe6ff" } }]
        },
        {
            id="fire", name="Fire", element="fire", shape="projectile",
            forms=["Fireball", "Inferno"],
            params={ size=1.0, speed=18.0, temperature=1900.0, intensity=1.0, soot=1.0, embers=1.0, smoke=1.0, haze=1.0, scorch=1.0, height=1.0, tint="#ffffff" },
            ranges={ size=[0.3, 3.0, 0.01], speed=[4.0, 40.0, 0.1], temperature=[900.0, 6500.0, 10.0], intensity=[0.0, 3.0, 0.01], soot=[0.0, 3.0, 0.01], embers=[0.0, 3.0, 0.01], smoke=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01], scorch=[0.0, 2.0, 0.01], height=[0.4, 2.0, 0.01] },
            timing={ charge=0.7, impact=0.25, linger=5.0, sustain=2.6 },
            // "canonical" locks the upstream default Fire look (HD volume path ≈ original).
            presets=[{ name="canonical", values={ size=1.0, speed=18.0, temperature=1900.0, intensity=1.0, soot=1.0, embers=1.0, smoke=1.0, haze=1.0, scorch=1.0, height=1.0, tint="#ffffff" } }, { name="hellfire", values={ temperature=1500.0, tint="#ff6a4a", smoke=1.6, soot=1.6, intensity=1.2 } }, { name="blue flame", values={ temperature=6500.0, tint="#8fb8ff", smoke=0.3, soot=0.15 } }, { name="soulfire", values={ temperature=2600.0, tint="#3cff9a", smoke=0.5, soot=0.5 } }]
        },
        {
            id="phoenix", name="Phoenix", element="fire", shape="projectile",
            forms=["Phoenix"],
            params={ size=1.0, speed=9.0, arc=1.0, swing=1.0, flap=1.0, temperature=1900.0, intensity=1.0, flames=1.0, feathers=1.0, embers=1.0, tail=1.0, smoke=1.0, haze=1.0, scorch=1.0, tint="#ffffff" },
            ranges={ size=[0.4, 2.0, 0.01], speed=[4.0, 20.0, 0.1], arc=[0.3, 2.0, 0.01], swing=[-2.0, 2.0, 0.01], flap=[0.0, 2.0, 0.01], temperature=[900.0, 6500.0, 10.0], intensity=[0.0, 3.0, 0.01], flames=[0.0, 3.0, 0.01], feathers=[0.0, 3.0, 0.01], embers=[0.0, 3.0, 0.01], tail=[0.3, 2.0, 0.01], smoke=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01], scorch=[0.0, 2.0, 0.01] },
            timing={ charge=0.9, impact=0.35, linger=3.6, sustain=6.0 },
            presets=[{ name="sun phoenix", values={ temperature=2600.0, intensity=1.3, tail=1.3, smoke=0.4 } }, { name="azure phoenix", values={ temperature=6500.0, tint="#8fb8ff", smoke=0.3, embers=1.4 } }, { name="ember hawk", values={ size=0.7, speed=13.0, arc=0.6, tail=0.7, flap=1.4, feathers=0.6, temperature=1500.0, tint="#ff7a5a" } }]
        },
        {
            id="ember-swarm", name="Ember Swarm", element="fire", shape="area",
            forms=["Ember Swarm"],
            params={ count=1.0, size=1.0, speed=1.0, temperature=1500.0, glow=1.0, exposure=1.0, arcane=0.2, flock=1.0, turbulence=1.0, radius=1.05, hold=2.4, shed=0.3, cling=0.1, haze=1.0, smoke=1.0, scorch=1.0, arcaneColor="#ff2fb4", ash="#2a2623" },
            ranges={ count=[0.1, 1.0, 0.01], size=[0.5, 2.5, 0.01], speed=[0.5, 2.0, 0.01], temperature=[1000.0, 3000.0, 10.0], glow=[0.0, 4.0, 0.01], exposure=[0.2, 3.0, 0.01], arcane=[0.0, 1.0, 0.01], flock=[0.0, 2.0, 0.01], turbulence=[0.0, 3.0, 0.01], radius=[0.7, 2.0, 0.01], hold=[1.0, 6.0, 0.1], shed=[0.0, 0.7, 0.01], cling=[0.0, 0.4, 0.01], haze=[0.0, 3.0, 0.01], smoke=[0.0, 3.0, 0.01], scorch=[0.0, 2.0, 0.01] },
            timing={ charge=0.6, impact=0.3, linger=2.5, sustain=2.0 },
            presets=[{ name="forge sparks", values={ temperature=2200.0, exposure=1.7, size=0.8, speed=1.35, turbulence=0.7, arcane=0.15, smoke=0.5 } }, { name="fireflies", values={ temperature=1250.0, count=0.45, exposure=0.7, speed=0.65, size=1.2, glow=1.8, haze=0.2, smoke=0.2, scorch=0.3, arcane=0.0 } }, { name="witch fire", values={ temperature=2300.0, arcane=1.0, glow=1.2, hold=4.0, radius=1.3, shed=0.45, arcaneColor="#e01fff" } }]
        },
        {
            id="water", name="Water", element="water", shape="beam",
            forms=["Tidal Lance", "Geyser"],
            params={ size=1.0, speed=32.0, whip=1.0, height=4.5, droplets=1.0, mist=1.0, splash=1.0, wetness=1.0, clarity=1.0, tint="#2fa8ff" },
            ranges={ size=[0.5, 2.0, 0.01], speed=[12.0, 60.0, 0.5], whip=[0.0, 2.0, 0.01], height=[1.5, 8.0, 0.1], droplets=[0.0, 3.0, 0.01], mist=[0.0, 3.0, 0.01], splash=[0.3, 2.0, 0.01], wetness=[0.0, 2.0, 0.01], clarity=[0.2, 3.0, 0.01] },
            timing={ charge=0.6, impact=0.35, linger=3.0, sustain=1.4 },
            presets=[{ name="tropical", values={ tint="#1fe0c8", clarity=1.6 } }, { name="abyssal", values={ tint="#1a4dff", clarity=0.45, mist=1.4 } }, { name="violet tide", values={ tint="#9a6bff", clarity=0.9 } }]
        },
        {
            id="tsunami", name="Tsunami", element="water", shape="area",
            forms=["Tsunami"],
            params={ height=6.0, width=13.0, speed=1.0, curl=1.0, peel=1.0, angle=-60.0, foam=1.0, spray=1.0, mist=1.0, wetness=1.0, clarity=1.0, tint="#2fe4ff", foamColor="#f2f6f7" },
            ranges={ height=[2.0, 8.0, 0.1], width=[6.0, 20.0, 0.1], speed=[0.5, 1.8, 0.01], curl=[0.4, 1.5, 0.01], peel=[0.0, 2.5, 0.01], angle=[-90.0, 90.0, 1.0], foam=[0.0, 2.0, 0.01], spray=[0.0, 3.0, 0.01], mist=[0.0, 3.0, 0.01], wetness=[0.0, 2.0, 0.01], clarity=[0.2, 3.0, 0.01] },
            timing={ charge=1.0, impact=0.3, linger=2.5, sustain=2.0 },
            presets=[{ name="rogue wave", values={ height=7.5, width=17.0, speed=0.85, clarity=0.7, spray=1.4, mist=1.4 } }, { name="tropical", values={ tint="#1fe0c8", clarity=1.7, foam=1.2, height=4.0 } }, { name="abyssal", values={ tint="#1a4dff", clarity=0.6, foam=0.7, mist=1.6, foamColor="#cfe0f0" } }]
        },
        {
            id="ice", name="Ice", element="ice", shape="projectile",
            forms=["Glacial Spike", "Ice Wall"],
            params={ size=1.0, speed=24.0, ior=1.31, clarity=1.0, frost=1.0, interior=1.0, glints=1.0, mist=1.0, motes=1.0, shards=1.0, nova=1.0, spikes=8.0, wallSpikes=11.0, wallHeight=3.0, wallWidth=7.5, wallCurve=1.0, tint="#a8dcff", glow="#5fd0ff" },
            ranges={ size=[0.4, 2.5, 0.01], speed=[6.0, 50.0, 0.1], ior=[1.0, 2.4, 0.01], clarity=[0.2, 4.0, 0.01], frost=[0.0, 2.0, 0.01], interior=[0.0, 3.0, 0.01], glints=[0.0, 3.0, 0.01], mist=[0.0, 3.0, 0.01], motes=[0.0, 3.0, 0.01], shards=[0.0, 3.0, 0.01], nova=[0.4, 2.0, 0.01], spikes=[0.0, 8.0, 1.0], wallSpikes=[3.0, 15.0, 1.0], wallHeight=[1.0, 4.5, 0.01], wallWidth=[3.0, 12.0, 0.1], wallCurve=[-1.0, 2.0, 0.01] },
            timing={ charge=0.8, impact=0.3, linger=6.5, sustain=2.0 },
            presets=[{ name="glacier", values={ tint="#3fa6ff", clarity=1.8, glow="#3f86ff", frost=0.7 } }, { name="rime", values={ tint="#d2f2ff", clarity=0.6, frost=1.8, interior=1.8, mist=1.7, glow="#bfefff" } }, { name="black ice", values={ tint="#2f7d8f", clarity=3.0, glints=2.2, frost=0.4, mist=0.6, glow="#6ff6ff" } }]
        },
        {
            id="absolute-zero", name="Absolute Zero", element="ice", shape="area",
            forms=["Absolute Zero"],
            params={ radius=3.6, drop=2.6, hold=2.6, desaturate=0.85, negative=1.0, haze=1.0, lens=1.0, frost=1.0, cracks=1.0, shards=1.0, dust=1.0, mist=1.0, glints=1.0, tint="#a9d8ff", glow="#62d4ff" },
            ranges={ radius=[2.0, 6.0, 0.01], drop=[2.0, 10.0, 0.1], hold=[1.0, 5.0, 0.05], desaturate=[0.0, 1.0, 0.01], negative=[0.0, 1.0, 0.01], haze=[0.0, 3.0, 0.01], lens=[0.0, 3.0, 0.01], frost=[0.0, 2.0, 0.01], cracks=[0.0, 3.0, 0.01], shards=[0.0, 3.0, 0.01], dust=[0.0, 3.0, 0.01], mist=[0.0, 3.0, 0.01], glints=[0.0, 3.0, 0.01] },
            timing={ charge=1.3, impact=0.25, linger=6.0, sustain=2.0 },
            presets=[{ name="glacier", values={ tint="#6fb4ff", glow="#3f86ff", desaturate=0.7, haze=1.5 } }, { name="whiteout", values={ tint="#e6f4ff", glow="#bfefff", desaturate=1.0, haze=2.4, mist=2.0, dust=2.2 } }, { name="cryo", values={ tint="#8ffff0", glow="#2fffd8", cracks=2.2, radius=4.6, hold=3.2 } }]
        },
        {
            id="blizzard", name="Blizzard", element="ice", shape="area",
            forms=["Blizzard"],
            params={ radius=4.0, duration=6.0, strength=1.0, wind=10.0, angle=25.0, snow=1.0, visibility=1.0, spindrift=1.0, streaks=1.0, hail=1.0, refraction=1.0, frost=1.0, freeze=1.0, air="#dcecff", glow="#86d2ff" },
            ranges={ radius=[2.0, 6.0, 0.01], duration=[2.0, 12.0, 0.1], strength=[0.2, 2.5, 0.01], wind=[3.0, 20.0, 0.1], angle=[-90.0, 90.0, 1.0], snow=[0.0, 3.0, 0.01], visibility=[0.0, 3.0, 0.01], spindrift=[0.0, 3.0, 0.01], streaks=[0.0, 3.0, 0.01], hail=[0.0, 3.0, 0.01], refraction=[0.0, 3.0, 0.01], frost=[0.0, 2.0, 0.01], freeze=[0.0, 2.0, 0.01] },
            timing={ charge=1.0, impact=0.35, linger=7.6, sustain=2.0 },
            presets=[{ name="whiteout", values={ radius=5.0, snow=1.8, visibility=1.9, spindrift=2.0, streaks=0.8, hail=0.4, air="#eef6ff" } }, { name="polar vortex", values={ strength=1.8, wind=15.0, streaks=1.6, visibility=1.2, spindrift=1.4, air="#c8e2ff", glow="#4f9dff" } }, { name="sleet", values={ hail=2.6, snow=0.7, visibility=0.7, spindrift=0.6, strength=1.3, air="#d2e6f0", glow="#9fd8e6" } }]
        },
        {
            id="earth", name="Earth", element="earth", shape="projectile",
            forms=["Upheaval", "Meteor"],
            params={ size=1.0, height=2.4, spikes=7.0, stagger=0.03, hold=3.0, debris=1.0, dust=1.0, crack=1.0, shake=1.0, heat=0.0, glow=0.0, meteorSize=1.0, arc=3.5, meteorHeat=0.6, rim=6.0, rockColor="#3b342d", dustColor="#696159" },
            ranges={ size=[0.4, 2.0, 0.01], height=[0.8, 4.5, 0.01], spikes=[3.0, 16.0, 1.0], stagger=[0.0, 0.12, 0.001], hold=[0.5, 8.0, 0.1], debris=[0.0, 3.0, 0.01], dust=[0.0, 3.0, 0.01], crack=[0.0, 2.0, 0.01], shake=[0.0, 2.0, 0.01], heat=[0.0, 1.0, 0.01], glow=[0.0, 2.0, 0.01], meteorSize=[0.5, 2.0, 0.01], arc=[1.0, 8.0, 0.1], meteorHeat=[0.0, 1.0, 0.01], rim=[0.0, 12.0, 1.0] },
            timing={ charge=0.5, impact=0.35, linger=4.3, sustain=2.0 },
            presets=[{ name="molten", values={ heat=0.85, meteorHeat=1.0, glow=1.0, dustColor="#4a3c35", rockColor="#2e2622" } }, { name="desert", values={ dustColor="#b39573", rockColor="#6b5643", dust=1.5, crack=0.7 } }, { name="tectonic", values={ spikes=11.0, height=3.4, size=1.3, stagger=0.045, shake=1.4, debris=1.4, meteorSize=1.6, rim=10.0 } }]
        },
        {
            id="fissure", name="Fissure", element="earth", shape="area",
            forms=["Fissure"],
            params={ width=0.42, depth=1.1, reach=9.0, overshoot=1.6, hold=4.2, temperature=1350.0, intensity=1.0, lips=5.0, lipHeight=0.4, unzip=0.2, debris=1.0, steam=1.0, dust=1.0, haze=1.0, shake=1.0, tint="#ffffff" },
            ranges={ width=[0.15, 1.0, 0.01], depth=[0.4, 2.5, 0.01], reach=[3.0, 16.0, 0.1], overshoot=[0.0, 4.0, 0.05], hold=[1.5, 9.0, 0.1], temperature=[900.0, 2600.0, 10.0], intensity=[0.0, 3.0, 0.01], lips=[0.0, 12.0, 1.0], lipHeight=[0.2, 1.8, 0.01], unzip=[0.02, 0.8, 0.01], debris=[0.0, 3.0, 0.01], steam=[0.0, 3.0, 0.01], dust=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01], shake=[0.0, 2.0, 0.01] },
            timing={ charge=0.9, impact=0.35, linger=6.0, sustain=2.0 },
            presets=[{ name="obsidian rift", values={ temperature=1100.0, width=0.3, depth=1.8, steam=2.0, intensity=1.3, lipHeight=0.55 } }, { name="cataclysm", values={ width=0.75, reach=13.0, lips=8.0, lipHeight=1.25, debris=1.7, dust=1.5, shake=1.6, unzip=0.32, hold=5.5 } }, { name="sulfur vent", values={ tint="#8fb0ff", temperature=2400.0, steam=2.4, debris=0.7, intensity=0.8 } }]
        },
        {
            id="lava-geyser", name="Lava Geyser", element="earth", shape="disk",
            forms=["Lava Geyser"],
            params={ height=6.0, width=0.4, duration=4.0, temperature=1400.0, intensity=1.0, surge=1.0, bombs=1.0, spatter=1.0, plume=1.0, haze=1.0, rim=7.0, rimHeight=0.55, shake=1.0, tint="#ffffff" },
            ranges={ height=[2.0, 12.0, 0.1], width=[0.12, 0.8, 0.01], duration=[1.5, 9.0, 0.1], temperature=[1000.0, 2600.0, 10.0], intensity=[0.0, 3.0, 0.01], surge=[0.0, 3.0, 0.01], bombs=[0.0, 3.0, 0.01], spatter=[0.0, 3.0, 0.01], plume=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01], rim=[0.0, 12.0, 1.0], rimHeight=[0.2, 1.6, 0.01], shake=[0.0, 2.0, 0.01] },
            timing={ charge=1.1, impact=0.3, linger=8.0, sustain=2.0 },
            presets=[{ name="hawaiian fountain", values={ height=9.5, width=0.26, surge=0.5, bombs=0.6, plume=0.8, duration=5.5, rimHeight=0.45 } }, { name="strombolian", values={ height=5.0, width=0.36, surge=2.4, bombs=2.2, plume=1.7, spatter=0.7, rimHeight=0.85, rim=9.0 } }, { name="sulfur blue", values={ tint="#7fa6ff", temperature=1750.0, height=4.2, plume=1.8, bombs=0.4, intensity=0.9 } }]
        },
        {
            id="wind", name="Wind", element="wind", shape="projectile",
            forms=["Cyclone", "Wind Slash"],
            params={ strength=1.0, size=1.0, drift=3.5, duration=3.5, dust=1.0, debris=1.0, streaks=1.0, refraction=1.0, slashSpeed=24.0, slashSize=1.0, tilt=50.0, scratch=1.0, air="#dff4ff", dustColor="#7d6a55" },
            ranges={ strength=[0.2, 2.5, 0.01], size=[0.4, 2.0, 0.01], drift=[0.0, 10.0, 0.1], duration=[1.0, 8.0, 0.1], dust=[0.0, 3.0, 0.01], debris=[0.0, 3.0, 0.01], streaks=[0.0, 3.0, 0.01], refraction=[0.0, 3.0, 0.01], slashSpeed=[6.0, 50.0, 0.1], slashSize=[0.4, 2.5, 0.01], tilt=[0.0, 90.0, 1.0], scratch=[0.0, 2.0, 0.01] },
            timing={ charge=0.9, impact=0.3, linger=3.5, sustain=8.0 },
            presets=[{ name="dust devil", values={ size=0.7, strength=1.3, dust=1.8, debris=0.6, dustColor="#b4875a", air="#ffe9cf" } }, { name="gale", values={ size=1.4, strength=1.8, streaks=2.0, refraction=1.6, debris=2.2, slashSize=1.5, scratch=1.6 } }, { name="spirit wind", values={ air="#8dffd6", dustColor="#5f8f84", streaks=2.4, dust=0.5, refraction=1.4 } }]
        },
        {
            id="dust-devils", name="Dust Devils", element="wind", shape="area",
            forms=["Dust Devils"],
            params={ size=1.0, strength=1.0, weave=1.0, spread=1.0, duration=4.4, dust=1.0, debris=1.0, streaks=1.0, grit=1.0, refraction=1.0, tracks=1.0, sand="#a28b6c", air="#fff1dc" },
            ranges={ size=[0.5, 1.8, 0.01], strength=[0.3, 2.5, 0.01], weave=[0.2, 2.5, 0.01], spread=[0.4, 2.0, 0.01], duration=[1.5, 9.0, 0.1], dust=[0.0, 3.0, 0.01], debris=[0.0, 3.0, 0.01], streaks=[0.0, 3.0, 0.01], grit=[0.0, 3.0, 0.01], refraction=[0.0, 3.0, 0.01], tracks=[0.0, 2.0, 0.01] },
            timing={ charge=0.9, impact=0.4, linger=2.5, sustain=2.0 },
            presets=[{ name="red planet", values={ sand="#a3532c", air="#ffcfae", grit=0.4, tracks=1.6, dust=1.3 } }, { name="ash devils", values={ sand="#4b4743", air="#d9dce6", grit=0.0, debris=0.4, dust=1.4, streaks=0.6 } }, { name="autumn whirl", values={ sand="#7c6a4c", air="#fff6e0", debris=2.8, dust=0.55, size=1.2, weave=1.4 } }]
        },
        {
            id="void", name="Void", element="void", shape="projectile",
            forms=["Singularity", "Rift"],
            params={ size=1.0, speed=6.0, gravity=1.0, lensing=1.0, disk=1.0, temperature=4800.0, beaming=0.35, tendrils=1.0, corruption=1.0, collapse=1.0, hold=2.6, tint="#ffffff", edge="#9a4dff" },
            ranges={ size=[0.4, 2.0, 0.01], speed=[2.0, 16.0, 0.1], gravity=[0.3, 3.0, 0.01], lensing=[0.0, 3.0, 0.01], disk=[0.0, 2.0, 0.01], temperature=[2500.0, 20000.0, 10.0], beaming=[0.0, 0.95, 0.01], tendrils=[0.0, 3.0, 0.01], corruption=[0.0, 3.0, 0.01], collapse=[0.0, 3.0, 0.01], hold=[0.8, 6.0, 0.1] },
            timing={ charge=1.0, impact=0.55, linger=3.2, sustain=2.0 },
            presets=[{ name="gargantua", values={ temperature=4600.0, beaming=0.6, tendrils=0.25, lensing=1.6, size=1.3, tint="#ffd6a8" } }, { name="violet disk", values={ temperature=14000.0, tint="#9b62ff", edge="#c070ff" } }, { name="abyssal", values={ temperature=3400.0, tint="#ff7088", edge="#ff2a60", corruption=2.0, tendrils=1.8 } }]
        },
        {
            id="portal", name="Portal", element="void", shape="area",
            forms=["Portal"],
            params={ size=1.0, reach=1.0, speed=16.0, stream=1.0, sparks=1.0, clarity=1.0, swirl=1.0, ripple=1.0, knockback=1.0, corruption=1.0, rim="#9a4dff", core="#efe4ff" },
            ranges={ size=[0.5, 1.6, 0.01], reach=[0.6, 1.8, 0.01], speed=[6.0, 30.0, 0.1], stream=[0.0, 3.0, 0.01], sparks=[0.0, 3.0, 0.01], clarity=[0.0, 1.0, 0.01], swirl=[0.0, 3.0, 0.01], ripple=[0.0, 3.0, 0.01], knockback=[0.0, 3.0, 0.01], corruption=[0.0, 3.0, 0.01] },
            timing={ charge=1.2, impact=2.4, linger=2.0, sustain=2.0 },
            presets=[{ name="event horizon", values={ rim="#5a8cff", core="#e4f0ff", ripple=2.2, swirl=0.6, sparks=0.4 } }, { name="ember ring", values={ rim="#ff7a1a", core="#fff0d8", sparks=2.2, swirl=1.4, corruption=0.3 } }, { name="abyss", values={ rim="#ff2a60", core="#ffd6e0", clarity=0.55, swirl=2.0, corruption=2.0, stream=1.6 } }]
        },
        {
            id="light", name="Light", element="light", shape="beam",
            forms=["Judgment", "Radiant Lance"],
            params={ intensity=1.0, width=2.0, sigil=1.5, tilt=18.0, rays=0.85, scatter=0.35, motes=1.0, feathers=1.0, glow=1.0, storm=1.0, dust=1.0, molten=1.0, lance=0.6, core="#fff4dc", color="#ffb84a" },
            ranges={ intensity=[0.0, 3.0, 0.01], width=[0.5, 4.0, 0.01], sigil=[0.6, 3.0, 0.01], tilt=[0.0, 25.0, 0.1], rays=[0.0, 1.0, 0.01], scatter=[0.0, 0.9, 0.01], motes=[0.0, 3.0, 0.01], feathers=[0.0, 3.0, 0.01], glow=[0.0, 3.0, 0.01], storm=[0.0, 1.0, 0.01], dust=[0.0, 3.0, 0.01], molten=[0.0, 3.0, 0.01], lance=[0.2, 1.5, 0.01] },
            // Short charge so lab / MCP captures reach the pillar quickly.
            timing={ charge=0.35, impact=0.7, linger=4.0, sustain=2.2 },
            presets=[{ name="dawn", values={ core="#fff0e8", color="#ff8a5c", tilt=16.0, rays=0.9 } }, { name="seraphic", values={ core="#ffffff", color="#bcd4ff", intensity=1.25, feathers=2.2, scatter=0.5 } }, { name="solar flare", values={ core="#fff3c0", color="#ff9a12", width=3.2, intensity=1.5, rays=1.0, motes=1.8 } }]
        },
        {
            id="solar-flare", name="Solar Flare", element="light", shape="projectile",
            forms=["Solar Flare"],
            params={ size=1.0, speed=8.0, intensity=1.0, pulse=1.0, loops=1.0, flare=1.0, granules=8.0, corona=1.0, sparks=1.0, glow=1.0, haze=1.0, core="#ffd27a", color="#ff560c" },
            ranges={ size=[0.5, 2.0, 0.01], speed=[3.0, 20.0, 0.1], intensity=[0.0, 3.0, 0.01], pulse=[0.0, 2.0, 0.01], loops=[0.0, 3.0, 0.01], flare=[0.4, 2.0, 0.01], granules=[2.0, 14.0, 0.1], corona=[0.0, 3.0, 0.01], sparks=[0.0, 3.0, 0.01], glow=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01] },
            timing={ charge=0.9, impact=0.5, linger=2.8, sustain=2.0 },
            presets=[{ name="red dwarf", values={ core="#ffd2a0", color="#ff3a12", size=0.8, speed=6.0, pulse=1.6, granules=5.0 } }, { name="blue giant", values={ core="#f0f5ff", color="#4a86ff", size=1.35, speed=7.0, flare=1.25, granules=11.0, corona=1.4 } }, { name="white dwarf", values={ core="#ffffff", color="#ffd090", size=0.65, speed=12.0, intensity=1.6, pulse=2.0, flare=0.8 } }]
        },
        {
            id="aurora-veil", name="Aurora Veil", element="light", shape="area",
            forms=["Aurora Veil"],
            params={ curtains=3.0, length=18.0, height=8.0, baseWidth=3.4, fold=1.0, rays=1.0, speed=1.0, duration=5.5, intensity=1.0, light=1.0, precipitation=1.0, veil=1.0, color="#19ff66", top="#b23cff", fringe="#ff3fa8" },
            ranges={ length=[8.0, 26.0, 0.1], height=[4.0, 14.0, 0.1], baseWidth=[2.5, 8.0, 0.05], fold=[0.0, 2.5, 0.01], rays=[0.0, 2.0, 0.01], speed=[0.2, 3.0, 0.01], duration=[2.0, 12.0, 0.1], intensity=[0.1, 3.0, 0.01], light=[0.0, 3.0, 0.01], precipitation=[0.0, 3.0, 0.01], veil=[0.0, 2.0, 0.01] },
            timing={ charge=1.2, impact=1.0, linger=5.5, sustain=2.0 },
            presets=[{ name="substorm", values={ curtains=4.0, fold=1.7, rays=1.6, speed=2.0, intensity=1.4, light=1.3 } }, { name="quiet arc", values={ curtains=1.0, length=24.0, fold=0.45, rays=0.6, speed=0.45, duration=8.0, intensity=0.8 } }, { name="blood aurora", values={ color="#ff2b47", top="#a01cff", fringe="#ffb0d0", rays=1.3 } }]
        },
        {
            id="supernova", name="Supernova", element="light", shape="area",
            forms=["Supernova"],
            params={ size=1.0, intensity=1.0, collapse=1.0, blast=1.0, fireball=1.0, soot=1.0, rays=1.0, lensing=1.0, pulsar=1.0, sparks=1.0, smoke=1.0, granules=3.0, star="#ffb468", limb="#d42a08", shock="#bfe9ff", filament="#ff4a2a", void="#8a3dff" },
            ranges={ size=[0.6, 1.6, 0.01], intensity=[0.0, 3.0, 0.01], collapse=[0.0, 2.0, 0.01], blast=[0.4, 2.0, 0.01], fireball=[0.0, 3.0, 0.01], soot=[0.0, 2.0, 0.01], rays=[0.0, 3.0, 0.01], lensing=[0.0, 3.0, 0.01], pulsar=[0.0, 2.0, 0.01], sparks=[0.0, 3.0, 0.01], smoke=[0.0, 3.0, 0.01], granules=[1.5, 10.0, 0.1] },
            timing={ charge=1.8, impact=1.4, linger=3.4, sustain=2.0 },
            presets=[{ name="betelgeuse", values={ star="#ffb468", limb="#d42a08", shock="#bfe9ff", filament="#ff4a2a", void="#8a3dff", size=1.0, granules=3.0, pulsar=1.0, collapse=1.0, soot=1.0 } }, { name="blue hypergiant", values={ star="#eef3ff", limb="#3d6dff", shock="#ffffff", filament="#ff4fb0", void="#5a3dff", size=1.2, granules=5.0, pulsar=1.4, soot=0.7 } }, { name="kilonova", values={ star="#fff2c4", limb="#ff9a10", shock="#fff0c8", filament="#ffc040", void="#ff5a2a", size=0.8, granules=2.2, pulsar=0.0, collapse=1.5, rays=1.6, soot=1.4 } }]
        },
        {
            id="acid", name="Acid", element="acid", shape="projectile",
            forms=["Toxic Bloom", "Corrosion Cloud"],
            params={ size=1.0, speed=10.0, glow=1.0, drips=1.0, fog=1.0, bubbles=1.0, iridescence=0.35, corrosion=1.0, cloudTime=8.0, color="#7cff2a", fogColor="#bfd895", cloudColor="#bfe08a" },
            ranges={ size=[0.4, 2.5, 0.01], speed=[4.0, 30.0, 0.1], glow=[0.0, 3.0, 0.01], drips=[0.0, 3.0, 0.01], fog=[0.0, 2.0, 0.01], bubbles=[0.0, 3.0, 0.01], iridescence=[0.0, 1.0, 0.01], corrosion=[0.0, 2.0, 0.01], cloudTime=[3.0, 15.0, 0.1] },
            timing={ charge=0.6, impact=0.3, linger=6.0, sustain=2.0 },
            presets=[{ name="bile", values={ color="#d8ff3a", fogColor="#dcd8a0", cloudColor="#e0eb86", glow=0.85 } }, { name="venom", values={ color="#c24dff", fogColor="#d2c0dc", cloudColor="#b98ad8", bubbles=1.6 } }, { name="radioactive", values={ color="#a0ff00", fogColor="#c8e2a8", cloudColor="#b0f070", glow=2.2, fog=0.7, iridescence=0.15 } }]
        },
        {
            id="plague-swarm", name="Plague Swarm", element="acid", shape="area",
            forms=["Plague Swarm"],
            params={ count=1.0, size=1.0, speed=1.0, glow=1.0, sync=0.6, flock=1.0, turbulence=1.0, radius=1.15, landing=0.32, hold=3.0, corrosion=1.0, color="#a8ff2a", color2="#f0ff58", chitin="#262619" },
            ranges={ count=[0.1, 1.0, 0.01], size=[0.5, 2.5, 0.01], speed=[0.4, 2.0, 0.01], glow=[0.0, 4.0, 0.01], sync=[0.0, 1.0, 0.01], flock=[0.0, 2.0, 0.01], turbulence=[0.0, 3.0, 0.01], radius=[0.7, 2.2, 0.01], landing=[0.0, 0.8, 0.01], hold=[1.0, 6.0, 0.1], corrosion=[0.0, 1.5, 0.01] },
            timing={ linger=8.0, charge=0.6, impact=0.3, sustain=2.0 },
            presets=[{ name="locusts", values={ glow=0.3, size=1.35, speed=1.25, sync=0.0, landing=0.5, chitin="#3d3420", color="#c8ff3a", turbulence=1.2 } }, { name="fireflies", values={ glow=2.4, size=0.7, count=0.5, sync=0.9, speed=0.6, turbulence=0.6, landing=0.15, color="#d6ff4a", color2="#ffd34a", chitin="#121210" } }, { name="blight", values={ color="#6dff3a", color2="#3affb4", glow=1.5, corrosion=1.45, hold=4.5, radius=1.35 } }]
        },
        {
            id="arcane", name="Arcane", element="arcane", shape="projectile",
            forms=["Arcane Barrage", "Aegis"],
            params={ missiles=8.0, speed=24.0, homing=4.0, spread=1.0, weave=0.6, sparkles=1.0, intensity=1.0, shield=1.45, duration=6.0, hex=1.0, distortion=1.0, pulse=0.6, anchor="target", color="#ff4fd8", core="#ffe3fb", glow="#b347ff" },
            ranges={ missiles=[1.0, 16.0, 1.0], speed=[8.0, 45.0, 0.1], homing=[2.0, 8.0, 0.1], spread=[0.0, 2.0, 0.01], weave=[0.0, 2.0, 0.01], sparkles=[0.0, 3.0, 0.01], intensity=[0.1, 3.0, 0.01], shield=[0.8, 3.0, 0.01], duration=[1.0, 20.0, 0.1], hex=[0.0, 3.0, 0.01], distortion=[0.0, 3.0, 0.01], pulse=[0.0, 3.0, 0.01] },
            timing={ charge=0.35, impact=0.2, linger=6.0, sustain=2.0 },
            presets=[{ name="frost ward", values={ color="#4fc8ff", core="#e6faff", glow="#2f5bff" } }, { name="golden aegis", values={ color="#ffb54a", core="#fff4dc", glow="#ff6a1a", hex=1.4 } }, { name="void swarm", values={ color="#a35cff", core="#f1e6ff", glow="#2a0a6e", missiles=12.0, weave=1.3, homing=5.5 } }]
        },
        {
            id="starfall", name="Starfall", element="arcane", shape="area",
            forms=["Starfall"],
            params={ stars=18.0, radius=3.2, duration=1.6, height=8.5, angle=26.0, speed=30.0, trail=6.0, size=1.0, sparkles=1.0, intensity=1.0, sky=1.0, night=1.0, color="#ff4fd8", core="#fff1fc", glow="#7a3dff" },
            ranges={ stars=[3.0, 47.0, 1.0], radius=[1.5, 6.0, 0.01], duration=[0.6, 3.0, 0.01], height=[6.0, 16.0, 0.1], angle=[0.0, 50.0, 0.5], speed=[12.0, 60.0, 0.1], trail=[1.0, 9.0, 0.01], size=[0.4, 2.5, 0.01], sparkles=[0.0, 3.0, 0.01], intensity=[0.1, 3.0, 0.01], sky=[0.0, 2.0, 0.01], night=[0.0, 2.0, 0.01] },
            timing={ charge=1.1, impact=2.0, linger=2.4, sustain=2.0 },
            presets=[{ name="meteor shower", values={ stars=24.0, duration=1.3, angle=38.0, speed=44.0, trail=6.0, size=0.75 } }, { name="grand comets", values={ stars=6.0, size=1.7, trail=7.5, speed=24.0, duration=1.8, angle=18.0 } }, { name="twilight", values={ color="#b06cff", core="#f3e8ff", glow="#3b2dff", sky=1.3, night=1.4 } }]
        },
        {
            id="frozen-lightning", name="Frozen Lightning", element="hybrid", shape="strike",
            forms=["Frozen Lightning"],
            params={ intensity=1.0, branches=6.0, jag=0.12, thickness=1.0, spurs=1.0, freeze=1.0, hold=1.0, shatter=1.0, mist=1.0, glints=1.0, frost=1.0, core="#e6f4ff", glow="#4fc8ff", tint="#a8dcff" },
            ranges={ intensity=[0.1, 3.0, 0.01], branches=[0.0, 8.0, 1.0], jag=[0.04, 0.25, 0.005], thickness=[0.4, 2.5, 0.01], spurs=[0.0, 2.0, 0.01], freeze=[0.4, 3.0, 0.01], hold=[0.2, 3.0, 0.01], shatter=[0.0, 3.0, 0.01], mist=[0.0, 3.0, 0.01], glints=[0.0, 3.0, 0.01], frost=[0.0, 2.0, 0.01] },
            timing={ charge=0.8, impact=2.0, linger=2.4, sustain=2.0 },
            presets=[{ name="aurora", values={ glow="#3dffb0", core="#e8fff4", tint="#b8ffe8" } }, { name="void ice", values={ glow="#9a5bff", core="#f2e8ff", tint="#c8b8ff", jag=0.16 } }, { name="glacier", values={ glow="#2f7dff", tint="#5fa8ff", thickness=1.5, spurs=1.6, branches=8.0 } }]
        },
        {
            id="fire-tornado", name="Fire Tornado", element="hybrid", shape="area",
            forms=["Fire Tornado"],
            params={ size=1.0, height=1.0, spin=1.0, duration=4.5, wander=1.0, temperature=1900.0, intensity=1.0, flames=1.0, embers=1.0, smoke=1.0, dust=1.0, haze=1.0, scorch=1.0, tint="#ffffff" },
            ranges={ size=[0.4, 2.0, 0.01], height=[0.4, 1.8, 0.01], spin=[0.2, 2.5, 0.01], duration=[1.5, 10.0, 0.1], wander=[0.0, 3.0, 0.01], temperature=[900.0, 6500.0, 10.0], intensity=[0.0, 3.0, 0.01], flames=[0.0, 3.0, 0.01], embers=[0.0, 3.0, 0.01], smoke=[0.0, 3.0, 0.01], dust=[0.0, 3.0, 0.01], haze=[0.0, 3.0, 0.01], scorch=[0.0, 2.0, 0.01] },
            timing={ charge=1.0, impact=0.3, linger=4.5, sustain=2.0 },
            presets=[{ name="firestorm", values={ size=1.35, height=1.3, spin=1.4, embers=1.8, smoke=1.5, duration=6.0 } }, { name="blue whirl", values={ temperature=6500.0, tint="#8fb8ff", smoke=0.2, dust=0.4, spin=1.6, height=0.8 } }, { name="hell spout", values={ temperature=1450.0, tint="#ff5a4a", smoke=2.2, flames=1.4, haze=1.6 } }]
        },
        {
            id="black-flame", name="Black Flame", element="hybrid", shape="capsule",
            forms=["Black Flame"],
            params={ size=1.0, height=1.0, intensity=1.0, absorb=1.0, embers=1.0, swirl=1.0, smoke=1.0, corruption=1.0, knockback=1.0, rim="#ff5a28", core="#1a1018" },
            ranges={ size=[0.5, 1.8, 0.01], height=[0.5, 1.8, 0.01], intensity=[0.0, 3.0, 0.01], absorb=[0.2, 2.0, 0.01], embers=[0.0, 3.0, 0.01], swirl=[0.0, 3.0, 0.01], smoke=[0.0, 3.0, 0.01], corruption=[0.0, 3.0, 0.01], knockback=[0.0, 3.0, 0.01] },
            timing={ charge=1.0, impact=3.0, linger=2.2, sustain=2.0 },
            presets=[{ name="hellmouth", values={ rim="#ff3a1e", core="#ffd9b8", embers=1.6, swirl=1.4 } }, { name="soul fire", values={ rim="#2dffc0", core="#dafff2", absorb=0.8, smoke=0.5 } }, { name="eclipse", values={ rim="#ffae3a", core="#fff3d6", absorb=1.6, intensity=1.4, height=1.25, embers=0.6 } }]
        },
    ];
    return { elements=elements, spells=spells };
}
return catalog();
