// Owning definition values. All profiles use four uniformly spaced keys.
return [
    {
        schema="eve.example.whirlwind", version=1, id="jade-gale",
        height=3.5, radius=1.6, spin=8.5, scroll=0.8, travel=0.0,
        windup=0.16, sustain=1.35, fade=0.45, opacity=0.9,
        color=[0.08,0.66,0.55], edge=[0.8,0.95,0.88],
        radiusCurve=[0.18,0.45,0.70,1.03], scaleCurve=[0.15,0.94,1.0,0.72],
        blades=4, turns=1.1, ribbonWidth=0.085, segments=100, seed=104729,
        mask="fx/assets/wind-blade.tga", shader="fx/shaders/blade.frag.spv"
    },
    {
        schema="eve.example.whirlwind", version=1, id="amber-ink",
        height=3.0, radius=1.9, spin=-7.0, scroll=0.55, travel=0.0,
        windup=0.25, sustain=1.55, fade=0.6, opacity=0.92,
        color=[0.82,0.32,0.07], edge=[1.0,0.86,0.43],
        radiusCurve=[0.25,0.65,1.0,0.35], scaleCurve=[0.08,1.0,0.9,0.55],
        blades=3, turns=1.45, ribbonWidth=0.17, segments=120, seed=130363,
        mask="fx/assets/ink-streak.tga", shader="fx/shaders/blade.frag.spv"
    }
];
