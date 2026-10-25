// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "RenderSystem.cpp",
    "SkyWispsAsset.cpp",
    "resource.cpp",
    "shader.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/unittest.h"

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_RenderSystem)
#include <cmath>
#include <cstdint>
#include <vector>
#include "graphics/Batcher.h"
#include "graphics/Canvas.h"
#include "graphics/DrawItem2D.h"
#include "graphics/RenderSystem.h"
#include "graphics/Texture.h"

namespace test_render_system_contract {
TEST_CASE("graphics.Batcher addRect/toNDC: six vertices and top-left screen origin") {
    eve::graphics::Batcher batch;
    batch.addRect(10, 20, 30, 40, eve::graphics::Color(1, 0, 0, 1));
    REQUIRE_EQ(batch.vertices().size(), 6u);
    CHECK_EQ(batch.vertices()[0].pos.x, 10.0f);
    CHECK_EQ(batch.vertices()[0].pos.y, 20.0f);
    batch.toNDC(100, 100);
    CHECK(std::fabs(batch.vertices()[0].pos.x - (-0.8f)) < 1e-5f);
    CHECK(std::fabs(batch.vertices()[0].pos.y - (-0.6f)) < 1e-5f);
}

TEST_CASE("graphics.DrawItem2D sort: equal visual keys retain submission order") {
    std::vector<eve::graphics::DrawItem2D> items(3);
    for (auto& item : items) {
        item.layer  = 4;
        item.depthY = 20.0f;
    }
    items[0].texture  = reinterpret_cast<eve::graphics::Texture*>(uintptr_t(0x300));
    items[1].texture  = reinterpret_cast<eve::graphics::Texture*>(uintptr_t(0x100));
    items[2].texture  = reinterpret_cast<eve::graphics::Texture*>(uintptr_t(0x200));
    const auto first  = items[0].texture;
    const auto second = items[1].texture;
    const auto third  = items[2].texture;
    eve::graphics::sortDrawItems2D(items);
    CHECK_EQ(items[0].texture, first);
    CHECK_EQ(items[1].texture, second);
    REQUIRE_EQ(items[2].texture, third);
}
}  // namespace test_render_system_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_SkyWispsAsset)
#include <algorithm>
#include "graphics/ShaderResources.h"
#include "graphics/sky/SkyWispsAsset.h"
#include "graphics/sky/SkyWispsLayer.h"

namespace test_sky_wisps_asset_contract {
TEST_CASE("graphics.SkyWispsAsset generate: equal seeds reproduce owned textures and different seeds diverge") {
    auto first = eve::graphics::SkyWispsAsset::generate(2026);
    auto again = eve::graphics::SkyWispsAsset::generate(2026);
    auto other = eve::graphics::SkyWispsAsset::generate(17);
    REQUIRE(first.ok());
    REQUIRE(again.ok());
    REQUIRE(other.ok());
    const auto& a = first.value().layer();
    const auto& b = again.value().layer();
    const auto& c = other.value().layer();
    REQUIRE(a.daylight.validate().ok());
    const auto bytesEqual = [](const auto* left, const auto* right) {
        return left->bytes.size() == right->bytes.size() &&
               std::equal(left->bytes.begin(), left->bytes.end(), right->bytes.begin());
    };
    CHECK(bytesEqual(a.densityTexture, b.densityTexture));
    CHECK(bytesEqual(a.starsTexture, b.starsTexture));
    CHECK(!bytesEqual(a.densityTexture, c.densityTexture));
    CHECK_EQ(a.starsTexture->mipLevels, 11);
    CHECK_EQ(a.moonColorTexture->mipLevels, 9);
}
}  // namespace test_sky_wisps_asset_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_resource)
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/Capability.h"
#include "common/Resource.h"
#include "common/ResourceRef.h"
#include "filesystem/ResourceReader.h"

namespace test_resource_contract {
class GenerationResource final : public eve::Resource {
public:
    GenerationResource() : eve::Resource("") { ++alive; }
    ~GenerationResource() override { --alive; }
    void adopt(eve::Resource& replacement) override {
        auto& other = static_cast<GenerationResource&>(replacement);
        if (other.failAdopt) throw std::runtime_error("injected adopt failure");
        std::swap(generation, other.generation);
    }
    int        generation = 0;
    bool       failAdopt  = false;
    static int alive;
};
int GenerationResource::alive = 0;

class GenerationProvider final : public eve::caps::IAssetReloader {
public:
    const char* reloadKind() const override { return "generation-test"; }
    bool        handlesPath(const std::string& key) const override {
        const auto path = eve::ResourceManager::pathOfKey(key);
        return path.size() >= 4 && path.compare(path.size() - 4, 4, ".dat") == 0;
    }
    eve::Result<bool> reload(const std::string&) override { return eve::Result<bool>::success(false); }
    eve::Resource*    load(const std::string& key) override {
        if (!handlesPath(key)) return nullptr;
        if (failed.count(key) != 0) return nullptr;
        auto* resource       = new GenerationResource();
        resource->generation = ++generations[key];
        resource->failAdopt  = adoptFailed.count(key) != 0;
        return resource;
    }
    std::map<std::string, int> generations;
    std::set<std::string>      failed;
    std::set<std::string>      adoptFailed;
};

struct ResourceFixture {
    ResourceFixture() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
        eve::cap::addListener<eve::caps::IAssetReloader>(&provider, eve::caps::IAssetReloader::kCache);
    }
    ~ResourceFixture() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    eve::ResourceManager& manager = eve::ResourceManager::getInstance();
    GenerationProvider    provider;
};

TEST_CASE("filesystem.ResourceManager keys: path normalization and parameter identity") {
    CHECK_EQ(eve::ResourceManager::normalizePath("./a\\b.dat"), std::string("a/b.dat"));
    CHECK_EQ(eve::ResourceManager::makeKey("a\\b.dat", "size=16"), std::string("a/b.dat?size=16"));
    REQUIRE_EQ(eve::ResourceManager::pathOfKey("./a/b.dat?size=16"), std::string("a/b.dat"));
}

TEST_CASE("filesystem.ResourceManager get/peek/unload: cache identity and pinned lifetime") {
    ResourceFixture fixture;
    auto*           first = static_cast<GenerationResource*>(fixture.manager.get("a.dat"));
    REQUIRE(first != nullptr);
    CHECK_EQ(fixture.manager.get("a.dat"), first);
    CHECK_EQ(first->generation, 1);
    CHECK_EQ(fixture.manager.count(), 1u);
    CHECK(!fixture.manager.peek("missing.dat").has_value());
    CHECK_EQ(fixture.manager.count(), 1u);
    auto pin = fixture.manager.pin(first);
    REQUIRE(pin.ok());
    eve::ResourcePin kept = std::move(pin).takeValue();
    fixture.manager.unload("a.dat");
    CHECK_EQ(fixture.manager.count(), 0u);
    CHECK_EQ(static_cast<GenerationResource*>(kept.get())->generation, 1);
    auto* reloaded = static_cast<GenerationResource*>(fixture.manager.get("a.dat"));
    REQUIRE(reloaded != nullptr);
    CHECK_EQ(reloaded->generation, 2);
    CHECK(reloaded != first);
}

TEST_CASE("filesystem.ResourceManager reload: cached identity refreshes while variants remain distinct") {
    ResourceFixture fixture;
    auto*           small = static_cast<GenerationResource*>(fixture.manager.get("a.dat?size=16"));
    auto*           large = static_cast<GenerationResource*>(fixture.manager.get("a.dat?size=32"));
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    CHECK(small != large);
    auto refreshed = fixture.manager.reload("a.dat");
    REQUIRE(refreshed.ok());
    CHECK(refreshed.value());
    CHECK_EQ(small->generation, 2);
    CHECK_EQ(large->generation, 2);
    CHECK_EQ(fixture.manager.count(), 2u);
}

TEST_CASE("filesystem.ResourceManager reload: candidate and commit failures roll back every variant") {
    SUB_CASE("candidate load failure") {
        ResourceFixture fixture;
        auto*           first  = static_cast<GenerationResource*>(fixture.manager.get("a.dat?variant=1"));
        auto*           second = static_cast<GenerationResource*>(fixture.manager.get("a.dat?variant=2"));
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        fixture.provider.failed.insert("a.dat?variant=2");
        auto result = fixture.manager.reload("a.dat");
        REQUIRE(result.ok());
        CHECK(!result.value());
        CHECK_EQ(first->generation, 1);
        CHECK_EQ(second->generation, 1);
    };
    SUB_CASE("adopt failure after another variant was prepared") {
        ResourceFixture fixture;
        auto*           first  = static_cast<GenerationResource*>(fixture.manager.get("a.dat?variant=1"));
        auto*           second = static_cast<GenerationResource*>(fixture.manager.get("a.dat?variant=2"));
        REQUIRE(first != nullptr);
        REQUIRE(second != nullptr);
        fixture.provider.adoptFailed.insert("a.dat?variant=2");
        auto result = fixture.manager.reload("a.dat");
        REQUIRE(result.ok());
        CHECK(!result.value());
        CHECK_EQ(first->generation, 1);
        CHECK_EQ(second->generation, 1);
    };
}

TEST_CASE("filesystem.ResourceManager dependencies: reload propagates and unload retains pinned dependency") {
    ResourceFixture fixture;
    auto*           source  = static_cast<GenerationResource*>(fixture.manager.get("source.dat"));
    auto*           derived = static_cast<GenerationResource*>(fixture.manager.get("derived.dat"));
    REQUIRE(source != nullptr);
    REQUIRE(derived != nullptr);
    REQUIRE(derived->addDependency(*source).ok());
    const int before = GenerationResource::alive;
    fixture.manager.unload("source.dat");
    CHECK_EQ(GenerationResource::alive, before);
    auto dependencies = derived->getDependencies();
    REQUIRE_EQ(dependencies.size(), 1u);
    CHECK_EQ(dependencies.front(), source);
    fixture.manager.clear();
    CHECK_EQ(GenerationResource::alive, before - 2);
}

TEST_CASE("filesystem.ResourceUri parse: known schemes canonicalize while untrusted paths fail") {
    SUB_CASE("project URI and explicit legacy path") {
        auto uri = eve::ResourceUri::parse("PROJECT://textures/wood.png");
        REQUIRE(uri.ok());
        CHECK_EQ(uri.value().format(), std::string("project://textures/wood.png"));
        auto legacy = eve::ResourceUri::fromLegacyProjectPath("./textures\\wood.png");
        REQUIRE(legacy.ok());
        CHECK_EQ(legacy.value().format(), uri.value().format());
    };
    SUB_CASE("unsupported and escaping paths") {
        CHECK(!eve::ResourceUri::parse("file://textures/wood.png").ok());
        CHECK(!eve::ResourceUri::parse("project://../escape").ok());
        CHECK(!eve::ResourceUri::parse("project://a\\b").ok());
        CHECK(!eve::ResourceUri::parse("textures/wood.png").ok());
    };
}

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string& path, std::vector<std::uint8_t>& out) override {
        lastPath   = path;
        auto found = files.find(path);
        if (found == files.end()) return false;
        out = found->second;
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return files.count(path) != 0; }
    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                                      lastPath;
};

TEST_CASE("filesystem.ResourceReader read: project URI routes once and other namespaces do not fall back") {
    MemoryFileSystem filesystem;
    filesystem.files["textures/wood.bin"] = {1, 2, 3};
    eve::filesystem::ResourceReader reader(filesystem);
    auto                            uri = eve::ResourceUri::parse("project://textures/wood.bin");
    REQUIRE(uri.ok());
    auto bytes = reader.read(uri.value());
    REQUIRE(bytes.ok());
    CHECK_EQ(bytes.value(), std::vector<std::uint8_t>({1, 2, 3}));
    CHECK_EQ(filesystem.lastPath, std::string("textures/wood.bin"));
    filesystem.lastPath.clear();
    auto builtin = eve::ResourceUri::parse("builtin://textures/wood.bin");
    REQUIRE(builtin.ok());
    auto rejected = reader.read(builtin.value());
    CHECK(!rejected.ok());
    CHECK_EQ(rejected.code(), eve::StatusCode::Unsupported);
    CHECK(filesystem.lastPath.empty());
}
}  // namespace test_resource_contract
#endif

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_shader)
#include "common/Exception.h"
#include "graphics/Shader.h"

namespace test_shader_contract {
TEST_CASE("graphics.Shader uniform: declared shape occupies slots and values round trip") {
    eve::graphics::Shader shader;
    CHECK_EQ(shader.declareFloat("time"), 0);
    CHECK_EQ(shader.declareVec4("tint"), 1);
    CHECK_EQ(shader.usedFloats(), 5);
    shader.sendFloat("time", 1.5f);
    shader.sendVec4("tint", 0.2f, 0.4f, 0.6f, 1.0f);
    float time = 0.0f;
    REQUIRE_EQ(shader.getFromVar("time", &time, sizeof(time)), int(sizeof(time)));
    CHECK_EQ(time, 1.5f);
    float tint[4] = {};
    REQUIRE_EQ(shader.getFromVar("tint", tint, sizeof(tint)), int(sizeof(tint)));
    CHECK_EQ(tint[0], 0.2f);
    CHECK_EQ(tint[3], 1.0f);
}

TEST_CASE("graphics.Shader uniform: absent name and conflicting redeclaration reject") {
    SUB_CASE("send to unknown name") {
        eve::graphics::Shader shader;
        REQUIRE_EQ(shader.declareFloat("time"), 0);
        bool rejected = false;
        try {
            shader.sendFloat("missing", 1.0f);
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
    };
    SUB_CASE("same name with a different shape") {
        eve::graphics::Shader shader;
        REQUIRE_EQ(shader.declareFloat("time"), 0);
        bool rejected = false;
        try {
            shader.declareVec2("time");
        } catch (const eve::Exception&) {
            rejected = true;
        }
        CHECK(rejected);
    };
}
}  // namespace test_shader_contract
#endif
