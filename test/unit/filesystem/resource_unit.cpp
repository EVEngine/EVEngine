#include "common/Resource.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "PathBesideSource.h"
#include "common/AsyncWork.h"
#include "common/Capability.h"
#include "common/ResourceRef.h"
#include "data/ByteData.h"
#include "filesystem/FileData.h"
#include "filesystem/ResourceReader.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// resource.cpp
namespace {

/** Payload = a per-path generation counter observed at load time. */
class TestResource : public eve::Resource {
public:
    TestResource() : eve::Resource("") { ++alive; }
    ~TestResource() override { --alive; }

    void adopt(eve::Resource &replacement) override {
        auto &other = static_cast<TestResource &>(replacement);
        if (other.failOnAdopt) throw std::runtime_error("injected adopt failure");
        std::swap(value, other.value);
    }

    int  value       = 0;
    bool failOnAdopt = false;

    /** @brief Live instances, so a case can assert "destroyed exactly once". */
    static int alive;
};

/** Fake asset provider: claims "*.dat" and serves a fresh generation each load. */
class TestProvider : public eve::caps::IAssetReloader {
public:
    const char *reloadKind() const override { return "test"; }

    bool handlesPath(const std::string &normPath) const override {
        const std::string path = eve::ResourceManager::pathOfKey(normPath);
        return path.size() >= 4 && path.compare(path.size() - 4, 4, ".dat") == 0;
    }

    eve::Result<bool> reload(const std::string &) override {
        // ResourceManager owns the test cache refresh; this provider only
        // supplies detached candidates through load().
        return eve::Result<bool>::success(false);
    }

    eve::Resource *load(const std::string &key) override {
        if (!handlesPath(key)) return nullptr;
        // Failure injection: throw before any allocation, so the cache must end up
        // unchanged and no half-built candidate may leak.
        if (throwing.count(key)) throw std::runtime_error("injected provider failure");
        if (failures.count(key)) return nullptr;
        auto *r        = new TestResource();
        r->value       = ++generation[key];
        r->failOnAdopt = adoptFailures.count(key) != 0;
        return r;
    }

    static std::map<std::string, int> generation;
    static std::set<std::string>      failures;
    static std::set<std::string>      adoptFailures;
    static std::set<std::string>      throwing;
};

std::map<std::string, int> TestProvider::generation;
std::set<std::string>      TestProvider::failures;
std::set<std::string>      TestProvider::adoptFailures;
std::set<std::string>      TestProvider::throwing;
int                        TestResource::alive = 0;

/** Every case starts from an empty cache + empty capability registry. */
struct Reset {
    Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    ~Reset() {
        eve::ResourceManager::getInstance().clear();
        eve::cap::detail::clearAllRaw();
    }
    Reset(const Reset &)            = delete;
    Reset &operator=(const Reset &) = delete;
};

[[maybe_unused]] TestProvider &provider() {
    static TestProvider p;
    eve::cap::addListener<eve::caps::IAssetReloader>(&p, eve::caps::IAssetReloader::kCache);
    TestProvider::generation.clear();
    TestProvider::failures.clear();
    TestProvider::adoptFailures.clear();
    TestProvider::throwing.clear();
    return p;
}

[[maybe_unused]] eve::Resource *get(const std::string &key) { return eve::ResourceManager::getInstance().get(key); }

}  // namespace

TEST_CASE("resource.normalizePath") {
    CHECK_EQ(eve::ResourceManager::normalizePath("./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(eve::ResourceManager::normalizePath("a\\b.json"), std::string("a/b.json"));
    CHECK_EQ(eve::ResourceManager::normalizePath("x/"), std::string("x"));
}

TEST_CASE("resource.makeKeyAndPathOfKey") {
    CHECK_EQ(eve::ResourceManager::makeKey("a/b.png"), std::string("a/b.png"));
    CHECK_EQ(eve::ResourceManager::makeKey("a\\b.png", "size=16"), std::string("a/b.png?size=16"));
    CHECK_EQ(eve::ResourceManager::pathOfKey("a/b.png?size=16"), std::string("a/b.png"));
    CHECK_EQ(eve::ResourceManager::pathOfKey("./a/b.png?size=16"), std::string("a/b.png"));
}

TEST_CASE("resource.getUnknownKeyIsNull") {
    Reset reset;
    provider();
    CHECK(get("no_such_kind.zzz") == nullptr);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
}

TEST_CASE("resource.getCachesSameInstance") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("a.dat");
    REQUIRE(a != nullptr);
    CHECK(a == b);
    CHECK_EQ(static_cast<TestResource *>(a)->value, 1);
    CHECK_EQ(TestProvider::generation.size(), 1u);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.paramsAreDistinctEntries") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat?size=16");
    eve::Resource *b = get("a.dat?size=32");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a != b);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 2u);
}

TEST_CASE("resource.unloadDropsEntryButKeepsHoldersAlive") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    REQUIRE(a != nullptr);
    // An external holder pins the payload; the pin is what survives unload() now.
    auto holder = eve::ResourceManager::getInstance().pin(a);
    REQUIRE(holder.ok());
    eve::ResourcePin keepAlive = std::move(holder).takeValue();
    eve::ResourceManager::getInstance().unload("a.dat");
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
    CHECK_EQ(static_cast<TestResource *>(keepAlive.get())->value, 1);

    // A later get() loads a fresh instance.
    eve::Resource *b = get("a.dat");
    REQUIRE(b != nullptr);
    CHECK(b != a);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
}

TEST_CASE("resource.unloadPathMatchesAnyParams") {
    Reset reset;
    provider();

    REQUIRE(get("a.dat?size=16") != nullptr);
    REQUIRE(get("a.dat?size=32") != nullptr);
    REQUIRE(get("b.dat") != nullptr);
    eve::ResourceManager::getInstance().unloadPath("a.dat");
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.reloadRefreshesInPlace") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    REQUIRE(a != nullptr);
    TestResource *ptr = static_cast<TestResource *>(a);
    CHECK_EQ(ptr->value, 1);

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK(a == ptr);          // identity stays stable
    CHECK_EQ(ptr->value, 2);  // fresh payload adopted
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}

TEST_CASE("resource.reloadWithoutCachedEntryIsNoop") {
    Reset reset;
    provider();

    CHECK(!eve::ResourceManager::getInstance().handlesPath("a.dat"));
    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
}

TEST_CASE("resource.reloadTriggersDependents") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("b.dat");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(b->addDependency(*a).ok());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 1);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 1);

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 2);
    // The dependent entry refreshed transitively, in place.
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 2u);
}

TEST_CASE("resource.reloadDependencyCycleIsSafe") {
    Reset reset;
    provider();

    eve::Resource *a = get("a.dat");
    eve::Resource *b = get("b.dat");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->addDependency(*b).ok());
    CHECK(b->addDependency(*a).ok());

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(result.value());
    CHECK_EQ(static_cast<TestResource *>(a)->value, 2);
    CHECK_EQ(static_cast<TestResource *>(b)->value, 2);
}

TEST_CASE("resource.reloadAllVariantsIsTransactional") {
    Reset reset;
    provider();

    auto *small = static_cast<TestResource *>(get("a.dat?size=16"));
    auto *large = static_cast<TestResource *>(get("a.dat?size=32"));
    REQUIRE(small != nullptr);
    REQUIRE(large != nullptr);
    TestProvider::failures.insert("a.dat?size=32");

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(small->value, 1);
    CHECK_EQ(large->value, 1);
}

TEST_CASE("resource.reloadDependentFailureRollsBackWholeGraph") {
    Reset reset;
    provider();

    auto *source  = static_cast<TestResource *>(get("source.dat"));
    auto *derived = static_cast<TestResource *>(get("derived.dat"));
    REQUIRE(source != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(derived->addDependency(*source).ok());
    TestProvider::failures.insert("derived.dat");

    auto result = eve::ResourceManager::getInstance().reload("source.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(source->value, 1);
    CHECK_EQ(derived->value, 1);
}

TEST_CASE("resource.reloadCommitFailureRollsBackEarlierEntries") {
    Reset reset;
    provider();

    auto *first  = static_cast<TestResource *>(get("a.dat?variant=1"));
    auto *second = static_cast<TestResource *>(get("a.dat?variant=2"));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    TestProvider::adoptFailures.insert("a.dat?variant=2");

    auto result = eve::ResourceManager::getInstance().reload("a.dat");
    CHECK(result.ok());
    CHECK(!result.value());
    CHECK_EQ(first->value, 1);
    CHECK_EQ(second->value, 1);
}

TEST_CASE("resource.requestNoOpWhenCached") {
    Reset reset;
    provider();
    REQUIRE(get("a.dat") != nullptr);
    auto queued = eve::ResourceManager::getInstance().request("a.dat");
    CHECK(queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::NoOp);
}

TEST_CASE("resource.requestUnknownIsNotFound") {
    Reset reset;
    provider();
    auto queued = eve::ResourceManager::getInstance().request("no_such_kind.zzz");
    CHECK(!queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::NotFound);
}

TEST_CASE("resource.peekDoesNotLoad") {
    Reset reset;
    provider();
    CHECK(!eve::ResourceManager::getInstance().peek("a.dat").has_value());
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
}


TEST_CASE("resource.pinKeepsPayloadAliveAcrossUnload") {
    Reset reset;
    provider();

    auto *sound = static_cast<TestResource *>(get("sound.dat"));
    REQUIRE(sound != nullptr);
    const int liveBefore = TestResource::alive;

    auto pinned = eve::ResourceManager::getInstance().pin(sound);
    REQUIRE(pinned.ok());
    eve::ResourcePin keepAlive = std::move(pinned).takeValue();
    CHECK_EQ(keepAlive.get(), sound);

    // The cache entry is gone, but the payload survives for the pin holder: this is
    // what keeps a SoundData alive while a Source still plays it (regression #1 of
    // the migration plan, which the previous ref<T> graph provided).
    eve::ResourceManager::getInstance().unload("sound.dat");
    CHECK(!eve::ResourceManager::getInstance().peek("sound.dat").has_value());
    CHECK_EQ(static_cast<TestResource *>(keepAlive.get())->value, 1);
    CHECK_EQ(TestResource::alive, liveBefore);

    // Releasing the last pin destroys the orphaned payload exactly once.
    keepAlive = eve::ResourcePin();
    CHECK_EQ(TestResource::alive, liveBefore - 1);
}

TEST_CASE("resource.pinRejectsUncachedResource") {
    Reset reset;
    provider();

    TestResource detached;
    auto         pinned = eve::ResourceManager::getInstance().pin(&detached);
    CHECK(!pinned.ok());
    CHECK_EQ(pinned.code(), eve::StatusCode::NotFound);
}

TEST_CASE("resource.dependencyPinOutlivesDependencyCacheEntry") {
    Reset reset;
    provider();

    auto *source  = static_cast<TestResource *>(get("source.dat"));
    auto *derived = static_cast<TestResource *>(get("derived.dat"));
    REQUIRE(source != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(derived->addDependency(*source).ok());
    const int liveBefore = TestResource::alive;

    // Dropping the dependency's own cache entry must not destroy it while the
    // dependent entry is alive (regression #2 of the migration plan).
    eve::ResourceManager::getInstance().unload("source.dat");
    CHECK_EQ(TestResource::alive, liveBefore);
    auto dependencies = derived->getDependencies();
    REQUIRE(dependencies.size() == 1u);
    CHECK_EQ(dependencies.front(), source);

    // clear() retires every slot, so the two keep-alives resolve in either slot
    // order and each payload is destroyed exactly once.
    eve::ResourceManager::getInstance().clear();
    CHECK_EQ(TestResource::alive, liveBefore - 2);
}

TEST_CASE("resource.providerThrowLeavesCacheEmpty") {
    Reset reset;
    provider();

    // Failure injection: the provider throws where it would normally hand over a
    // candidate. The cache must stay empty, nothing may leak, and get() must surface
    // the failure instead of silently returning a stale entry.
    TestProvider::throwing.insert("boom.dat");
    bool threw = false;
    try {
        (void)get("boom.dat");
    } catch (const std::exception &) {
        threw = true;
    }
    CHECK(threw);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
    CHECK_EQ(TestResource::alive, 0);

    // The retry path still works once the injection is removed.
    TestProvider::throwing.erase("boom.dat");
    CHECK(get("boom.dat") != nullptr);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 1u);
}


// resource_format_image.cpp
namespace {
EVE_DEFINE_PATH_BESIDE_SOURCE()

[[maybe_unused]] std::vector<char> fixture(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &name) {
    std::ifstream input(pathBesideThisSource(("fixtures/resource_formats/" + name).c_str()), std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[maybe_unused]] void decodePattern(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, const std::string &extension, bool alpha,
                   float tolerance, float divisor = 255.f) {
    const auto                             bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern." + extension);
    eve::data::ByteData                    source(bytes.data(), bytes.size());
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(&source));
    REQUIRE(decoded != nullptr);
    REQUIRE_EQ(decoded->getWidth(), 5);
    REQUIRE_EQ(decoded->getHeight(), 3);
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 5; ++x) {
            const auto pixel = decoded->getPixel(x, y);
            CHECK(std::fabs(pixel.r - (17 + x * 37) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.g - (23 + y * 71) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.b - (11 + x * 13 + y * 29) / divisor) <= tolerance);
            CHECK(std::fabs(pixel.a - (alpha ? (x + y * 5) * 17 / 255.f : 1.f)) <= tolerance);
        }
    }
}

[[maybe_unused]] void exportPattern(zeroerr::TestContext *_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::EncodedFormat format,
                   const std::string &pixelFormat, const std::string &filename) {
    auto                                  *module = eve::image::Image::create();
    std::unique_ptr<eve::image::ImageData> source(module->newImageData(5, 3, pixelFormat));
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x) {
            const float lowBits = pixelFormat == "RGBA16" ? (x + 1) / 65535.f : 0.f;
            source->setPixel(x, y,
                             {(17 + x * 37) / 255.f + lowBits, (23 + y * 71) / 255.f + lowBits,
                              (11 + x * 13 + y * 29) / 255.f + lowBits, (x + y * 5) * 17 / 255.f + lowBits});
        }
    std::unique_ptr<eve::filesystem::FileData> encoded(source->encode(format, filename.c_str(), false));
    REQUIRE(encoded != nullptr);
    std::unique_ptr<eve::image::ImageData> decoded(module->newImageData(encoded.get()));
    REQUIRE_EQ(decoded->getWidth(), source->getWidth());
    REQUIRE_EQ(decoded->getHeight(), source->getHeight());
    REQUIRE_EQ(decoded->getFormat(), pixelFormat);
    REQUIRE_EQ(decoded->getSize(), source->getSize());
    CHECK_EQ(std::memcmp(decoded->getData(), source->getData(), source->getSize()), 0);
    if (const char *directory = std::getenv("EVENGINE_FORMAT_EXPORT_DIR")) {
        std::filesystem::create_directories(directory);
        std::ofstream output(std::filesystem::path(directory) / filename, std::ios::binary);
        REQUIRE(output.good());
        output.write(static_cast<const char *>(encoded->getData()), static_cast<std::streamsize>(encoded->getSize()));
        REQUIRE(output.good());
    }
}
}  // namespace


// resource_uri.cpp
namespace {

class MemoryFileSystem final : public eve::service::IFileSystem {
public:
    bool readFile(const std::string &path, std::vector<std::uint8_t> &out) override {
        lastPath         = path;
        const auto found = files.find(path);
        if (found == files.end()) return false;
        out = found->second;
        return true;
    }

    bool writeFile(const std::string &, const void *, size_t) override { return false; }

    bool fileExists(const std::string &path) override { return files.find(path) != files.end(); }

    std::map<std::string, std::vector<std::uint8_t>> files;
    std::string                                      lastPath;
};

template <typename T>
[[maybe_unused]] T take(eve::Result<T> result) {
    REQUIRE(result.ok());
    return std::move(result).takeValue();
}

}  // namespace

TEST_CASE("resourceUri.acceptsOnlyKnownVirtualSchemes") {
    for (const auto text :
         {"asset://01020304-0506-0708-090a-0b0c0d0e0f10", "project://textures/wood.png?quality=high#albedo",
          "builtin://ui/default-font", "generated://terrain/seed-1", "memory://session/preview"}) {
        auto uri = eve::ResourceUri::parse(text);
        REQUIRE(uri.ok());
        CHECK(!uri.value().format().empty());
    }

    CHECK(!eve::ResourceUri::parse("textures/wood.png").ok());
    CHECK(!eve::ResourceUri::parse("file://textures/wood.png").ok());
    CHECK(!eve::ResourceUri::parse("http://example.com/a").ok());
    CHECK(!eve::ResourceUri::parse("project:///absolute").ok());
    CHECK(!eve::ResourceUri::parse("project://../escape").ok());
    CHECK(!eve::ResourceUri::parse("project://a\\b").ok());
}

TEST_CASE("resourceUri.canonicalizesSchemeAndLegacyPathExplicitly") {
    const auto uri = take(eve::ResourceUri::parse("PROJECT://textures/wood.png"));
    CHECK_EQ(uri.format(), std::string("project://textures/wood.png"));

    const auto legacy = take(eve::ResourceUri::fromLegacyProjectPath("./textures\\wood.png"));
    CHECK_EQ(legacy.format(), std::string("project://textures/wood.png"));
    CHECK(!eve::ResourceUri::fromLegacyProjectPath("file://textures/wood.png").ok());
}

TEST_CASE("resourceRef.keepsIdentityDomainsDistinct") {
    static_assert(!std::is_convertible_v<eve::AssetRef, eve::ObjectRef>);
    static_assert(!std::is_convertible_v<eve::AssetRef, eve::ResourceUri>);
    static_assert(!std::is_convertible_v<eve::DefinitionRef, eve::ResourceUri>);

    const auto asset = take(eve::AssetRef::parse("asset://01020304-0506-0708-090a-0b0c0d0e0f10"));
    CHECK_EQ(asset.format(), std::string("asset://01020304-0506-0708-090a-0b0c0d0e0f10"));
    CHECK(!eve::AssetRef::parse("asset://textures/wood.png").ok());
    CHECK(!eve::AssetRef::parse("project://textures/wood.png").ok());

    const auto definition = take(eve::DefinitionRef::parse("rpg:skill.fire"));
    CHECK_EQ(definition.format(), std::string("rpg:skill.fire"));
    CHECK(!eve::DefinitionRef::parse("rpg/skill.fire").ok());

    const auto object = take(eve::ObjectRef::parse("11111111-2222-3333-4444-555555555555"));
    CHECK_EQ(object.format(), std::string("11111111-2222-3333-4444-555555555555"));

    const auto genericAsset = take(eve::ResourceRef::parse(asset.format()));
    CHECK(genericAsset.asset() != nullptr);
    CHECK(genericAsset.uri() == nullptr);
    CHECK_EQ(genericAsset.kind(), eve::ResourceRefKind::Asset);

    const auto genericUri = take(eve::ResourceRef::parse("project://textures/wood.png"));
    CHECK(genericUri.asset() == nullptr);
    CHECK(genericUri.uri() != nullptr);
    CHECK_EQ(genericUri.kind(), eve::ResourceRefKind::Uri);
}

TEST_CASE("resourceReader.typedUriIsSourceOfTruth") {
    MemoryFileSystem filesystem;
    filesystem.files["textures/wood.bin"] = {1, 2, 3};
    eve::filesystem::ResourceReader reader(filesystem);

    const auto uri   = take(eve::ResourceUri::parse("project://textures/wood.bin"));
    const auto bytes = take(reader.read(uri));
    CHECK_EQ(bytes, std::vector<std::uint8_t>({1, 2, 3}));
    CHECK_EQ(filesystem.lastPath, std::string("textures/wood.bin"));

    const auto legacyBytes = take(reader.read("./textures/wood.bin"));
    CHECK_EQ(legacyBytes, std::vector<std::uint8_t>({1, 2, 3}));
    CHECK(!reader.read("file://textures/wood.bin").ok());
}

TEST_CASE("resourceReader.doesNotSilentlyFallbackAcrossNamespaces") {
    MemoryFileSystem filesystem;
    filesystem.files["textures/wood.bin"] = {1};
    eve::filesystem::ResourceReader reader(filesystem);

    const auto builtin = take(eve::ResourceUri::parse("builtin://textures/wood.bin"));
    auto       result  = reader.read(builtin);
    CHECK(!result.ok());
    CHECK_EQ(result.code(), eve::StatusCode::Unsupported);
    CHECK(filesystem.lastPath.empty());

    auto missing = reader.read("project://missing.bin");
    CHECK(!missing.ok());
    CHECK_EQ(missing.code(), eve::StatusCode::Failed);
}
