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


TEST_CASE("resource.requestAsyncThenWaitFor") {
    Reset reset;
    provider();

    std::mutex gate;
    gate.lock();
    class DelayedExecutor final : public eve::caps::IAsyncWorkExecutor {
    public:
        explicit DelayedExecutor(std::mutex *gate) : gate_(gate) {}
        eve::Result<void> submit(std::function<void()> work) override {
            std::thread([work = std::move(work), gate = gate_]() {
                std::lock_guard<std::mutex> hold(*gate);
                work();
            }).detach();
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        std::mutex *gate_ = nullptr;
    } executor{&gate};
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("async.dat");
    CHECK(queued.ok());
    CHECK_EQ(queued.code(), eve::StatusCode::Applied);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    CHECK(!eve::ResourceManager::getInstance().peek("async.dat").has_value());

    gate.unlock();
    auto ready = eve::ResourceManager::getInstance().waitFor("async.dat");
    REQUIRE(ready.ok());
    CHECK_EQ(static_cast<TestResource &>(ready.value().get()).value, 1);
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 0u);
}

TEST_CASE("resource.getJoinsInFlightRequest") {
    Reset reset;
    provider();

    std::mutex gate;
    gate.lock();
    class DelayedExecutor final : public eve::caps::IAsyncWorkExecutor {
    public:
        explicit DelayedExecutor(std::mutex *gate) : gate_(gate) {}
        eve::Result<void> submit(std::function<void()> work) override {
            std::thread([work = std::move(work), gate = gate_]() {
                std::lock_guard<std::mutex> hold(*gate);
                work();
            }).detach();
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        std::mutex *gate_ = nullptr;
    } executor{&gate};
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("join.dat");
    CHECK(queued.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);
    gate.unlock();
    eve::Resource *loaded = get("join.dat");
    REQUIRE(loaded != nullptr);
    CHECK_EQ(static_cast<TestResource *>(loaded)->value, 1);
}


TEST_CASE("resource.asyncLoadFailureSurfacesInWaitFor") {
    Reset reset;
    provider();

    // Failure injection on the asynchronous path: the worker fails to produce the
    // resource, so waitFor must report a failure and leave no pending job behind.
    TestProvider::failures.insert("async-fail.dat");
    std::mutex gate;
    gate.lock();
    class DelayedExecutor final : public eve::caps::IAsyncWorkExecutor {
    public:
        explicit DelayedExecutor(std::mutex *gate) : gate_(gate) {}
        eve::Result<void> submit(std::function<void()> work) override {
            std::thread([work = std::move(work), gate = gate_]() {
                std::lock_guard<std::mutex> hold(*gate);
                work();
            }).detach();
            return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
        }
        std::mutex *gate_ = nullptr;
    } executor{&gate};
    eve::cap::provide<eve::caps::IAsyncWorkExecutor>(&executor);

    auto queued = eve::ResourceManager::getInstance().request("async-fail.dat");
    CHECK(queued.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 1u);

    gate.unlock();
    auto ready = eve::ResourceManager::getInstance().waitFor("async-fail.dat");
    CHECK(!ready.ok());
    CHECK_EQ(eve::ResourceManager::getInstance().pendingCount(), 0u);
    CHECK_EQ(eve::ResourceManager::getInstance().count(), 0u);
    CHECK(!eve::ResourceManager::getInstance().peek("async-fail.dat").has_value());
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

TEST_CASE("resourceFormats.image.png.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.tga.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "tga", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.bmp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "bmp", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.jpeg.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "jpeg", false, 3.f / 255.f);
}
TEST_CASE("resourceFormats.image.gif.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "gif", false, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.webp.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "webp", true, 1.f / 255.f);
}
TEST_CASE("resourceFormats.image.hdr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "hdr", false, 1.f / 256.f, 256.f);
}
TEST_CASE("resourceFormats.image.exr.independentPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "exr", false, 2.f / 255.f);
}
TEST_CASE("resourceFormats.image.png.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA8", "rgba8.png");
}
TEST_CASE("resourceFormats.image.png.rgba16Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_PNG, "RGBA16", "rgba16.png");
}
TEST_CASE("resourceFormats.image.tga.rgba8Export") {
    exportPattern(_ZEROERR_TEST_CONTEXT, medialoader::FormatHandler::ENCODED_TGA, "RGBA8", "rgba8.tga");
}

TEST_CASE("resourceFormats.image.truncatedHeadersRejectWithoutPoisoningDecoder") {
    auto *module = eve::image::Image::create();
    for (const auto *extension : {"png", "tga", "bmp", "jpg", "exr", "hdr", "gif", "webp"}) {
        auto bytes = fixture(_ZEROERR_TEST_CONTEXT, std::string("pattern.") + extension);
        bytes.resize(4);
        eve::data::ByteData source(bytes.data(), bytes.size());
        bool                rejected = false;
        try {
            std::unique_ptr<eve::image::ImageData> unexpected(module->newImageData(&source));
        } catch (const std::exception &) {
            rejected = true;
        }
        CHECK(rejected);
    }
    decodePattern(_ZEROERR_TEST_CONTEXT, "png", true, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.gif.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.gif", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.animatedFirstFrame") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "animated.webp", false, 1.f / 255.f);
}

TEST_CASE("resourceFormats.image.webp.lossyPixels") {
    decodePattern(_ZEROERR_TEST_CONTEXT, "lossy.webp", false, 0.15f);
}

TEST_CASE("resourceFormats.image.hdr.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.hdr");
    bytes.pop_back();
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}

TEST_CASE("resourceFormats.image.webp.truncatedPayloadIsRejected") {
    auto bytes = fixture(_ZEROERR_TEST_CONTEXT, "pattern.webp");
    bytes.resize(bytes.size() / 2);
    eve::data::ByteData source(bytes.data(), bytes.size());
    bool                rejected = false;
    try {
        std::unique_ptr<eve::image::ImageData> unexpected(eve::image::Image::create()->newImageData(&source));
    } catch (const std::exception &) {
        rejected = true;
    }
    REQUIRE(rejected);
}

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
