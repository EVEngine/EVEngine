// Real Poco JSON + Procgen declaration; deterministic getters isolate routing.
#include "procgen/Procgen.h"
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "recipe_lists.inc"

static std::vector<std::string> algorithms, meshes, textures, materials;
static bool failGetter = false;
namespace eve::procgen {
Procgen::Procgen() = default;
const char* Procgen::name = "Procgen";
int Procgen::getAlgorithmCount() const { return int(algorithms.size()); }
int Procgen::getMeshRecipeCount() const { return int(meshes.size()); }
int Procgen::getTextureRecipeCount() const { return int(textures.size()); }
int Procgen::getPbrRecipeCount() const { return int(materials.size()); }
std::string Procgen::getAlgorithmId(int i) const {
    if (failGetter) throw std::runtime_error("getter failed");
    return algorithms.at(i);
}
std::string Procgen::getMeshRecipeId(int i) const { return meshes.at(i); }
std::string Procgen::getTextureRecipeId(int i) const { return textures.at(i); }
std::string Procgen::getPbrRecipeId(int i) const { return materials.at(i); }
}

int main() {
    eve::procgen::Procgen pg;
    auto empty = recipes(&pg);
    for (const char* key : {"algorithms", "meshRecipes", "textureRecipes", "pbrRecipes"})
        assert(empty->getArray(key)->size() == 0);
    algorithms = {"maze", "cave"};
    meshes = {"cube"};
    textures = {"quote\"", "line\nbreak", "中文"};
    materials = {"water", "wood"};
    auto full = recipes(&pg);
    assert(full->size() == 4);
    const std::vector<std::pair<const char*, const std::vector<std::string>*>> expected = {
        {"algorithms", &algorithms}, {"meshRecipes", &meshes},
        {"textureRecipes", &textures}, {"pbrRecipes", &materials}
    };
    for (const auto& [key, values] : expected) {
        auto array = full->getArray(key);
        assert(array->size() == values->size());
        for (unsigned i = 0; i < values->size(); ++i)
            assert(array->getElement<std::string>(i) == values->at(i));
    }
    std::ostringstream json;
    full->stringify(json);
    assert(json.str().find("quote\\\"") != std::string::npos);
    assert(json.str().find("line\\nbreak") != std::string::npos);
    failGetter = true;
    bool caught = false;
    try { recipes(&pg); } catch (const std::runtime_error&) { caught = true; }
    assert(caught);
}
