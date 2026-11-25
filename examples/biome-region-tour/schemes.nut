// Project-owned scheme assets. Biome documents store only their ResourceUri references.
// This is example content, not a second engine biome registry.
export function tourContent() {
    local regions = [
        { id="meadow", name="01  SUNLIT MEADOW", description="Oak groves / grazing deer / moss guardians",
          ground=[0.21,0.37,0.12], leaf=[0.52,0.78,0.33], rock=[0.40,0.45,0.34],
          sky=[0.46,0.65,0.79], fog=[0.65,0.78,0.73], density=0.003, rain=0.0, wind=1.5,
          weather="clear", treeHeight=6.0, crown=2.2, spacing=5.0, foliage=0.35,
          animal=[0.58,0.36,0.16], monster=[0.35,0.52,0.20], species="deer" },
        { id="marsh", name="02  RAINVEIL MARSH", description="Willows / shallow pools / boars and bog spirits",
          ground=[0.12,0.25,0.22], leaf=[0.26,0.55,0.39], rock=[0.25,0.36,0.34],
          sky=[0.28,0.40,0.44], fog=[0.43,0.62,0.61], density=0.025, rain=0.55, wind=4.0,
          weather="rain", treeHeight=7.5, crown=2.0, spacing=5.0, foliage=0.48,
          animal=[0.27,0.23,0.20], monster=[0.24,0.62,0.55], species="boar" },
        { id="amber", name="03  AMBER WILDS", description="Golden scrub / sandstone / dune beetles",
          ground=[0.60,0.38,0.13], leaf=[0.95,0.66,0.20], rock=[0.65,0.35,0.17],
          sky=[0.67,0.58,0.43], fog=[0.83,0.67,0.42], density=0.007, rain=0.0, wind=5.0,
          weather="wind", treeHeight=3.8, crown=2.2, spacing=6.5, foliage=0.52,
          animal=[0.66,0.46,0.21], monster=[0.71,0.25,0.10], species="beetle" },
        { id="frost", name="04  FROST CROWN", description="Slender snow trees / white foxes / ice sentinels",
          ground=[0.78,0.85,0.88], leaf=[0.68,0.86,0.87], rock=[0.40,0.53,0.62],
          sky=[0.43,0.56,0.69], fog=[0.76,0.87,0.94], density=0.013, rain=0.6, wind=2.5,
          weather="snow", treeHeight=8.0, crown=1.05, spacing=4.8, foliage=0.18,
          animal=[0.83,0.88,0.91], monster=[0.33,0.68,0.85], species="fox" }
    ];
    local assets = {};
    foreach (r in regions) {
        local prefix = "project://tour/" + r.id + "/";
        assets[prefix+"plants"] <- { height=r.treeHeight, crown=r.crown, spacing=r.spacing,
            foliage=r.foliage, color=r.leaf, density=0.70 };
        assets[prefix+"surface"] <- { color=r.ground, rock=r.rock, water=r.id=="marsh" };
        assets[prefix+"animals"] <- { color=r.animal, count=5, species=r.species };
        assets[prefix+"monsters"] <- { color=r.monster, count=2, species="guardian" };
        assets[prefix+"weather"] <- { preset=r.weather, intensity=r.rain, wind=r.wind, sky=r.sky };
        assets[prefix+"fog"] <- { color=r.fog, density=r.density };
    }
    assets["project://tour/shared/storm"] <- { preset="storm", intensity=0.85, wind=9.0, sky=[0.20,0.28,0.35] };
    return { regions=regions, assets=assets };
}
