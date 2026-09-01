#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "data/ByteData.h"
#include "filesystem/Filesystem.h"
#include "filesystem/File.h"
#include "filesystem/FileData.h"
#include "filesystem/FileWatch.h"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

eve::filesystem::Filesystem* fs() {
    return eve::filesystem::Filesystem::create();
}

// Unique identity per case to avoid cross-talk on the singleton write dir.
void useIdentity(const char* id) {
    auto* f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

// Bit-by-bit CRC-32 (zip polynomial); payloads in these tests are tiny so
// this is fine without pulling in a zlib dependency for the test binary.
uint32_t crc32Of(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    uint32_t    crc    = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return ~crc;
}

void put16(std::vector<unsigned char>& out, uint16_t v) {
    out.push_back(static_cast<unsigned char>(v & 0xFF));
    out.push_back(static_cast<unsigned char>((v >> 8) & 0xFF));
}

void put32(std::vector<unsigned char>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((v >> (8 * i)) & 0xFF));
}

// Hand-rolled minimal single-entry, uncompressed (stored) ZIP archive, so
// filesystem.mount(Data*, ...) can be exercised without a real archive file
// or an extra zip-writer dependency in the test binary.
std::vector<unsigned char> buildMinimalZip(const std::string& entryName, const std::string& content) {
    std::vector<unsigned char> out;
    const uint32_t              crc          = crc32Of(content.data(), content.size());
    const uint32_t               localOffset = 0;

    // Local file header.
    put32(out, 0x04034b50u);
    put16(out, 20);  // version needed
    put16(out, 0);   // flags
    put16(out, 0);   // compression: stored
    put16(out, 0);   // mod time
    put16(out, 0);   // mod date
    put32(out, crc);
    put32(out, static_cast<uint32_t>(content.size()));  // compressed size
    put32(out, static_cast<uint32_t>(content.size()));  // uncompressed size
    put16(out, static_cast<uint16_t>(entryName.size()));
    put16(out, 0);  // extra field length
    out.insert(out.end(), entryName.begin(), entryName.end());
    out.insert(out.end(), content.begin(), content.end());

    const size_t centralDirOffset = out.size();

    // Central directory file header.
    put32(out, 0x02014b50u);
    put16(out, 20);  // version made by
    put16(out, 20);  // version needed
    put16(out, 0);   // flags
    put16(out, 0);   // compression: stored
    put16(out, 0);   // mod time
    put16(out, 0);   // mod date
    put32(out, crc);
    put32(out, static_cast<uint32_t>(content.size()));
    put32(out, static_cast<uint32_t>(content.size()));
    put16(out, static_cast<uint16_t>(entryName.size()));
    put16(out, 0);  // extra field length
    put16(out, 0);  // comment length
    put16(out, 0);  // disk number start
    put16(out, 0);  // internal attributes
    put32(out, 0);  // external attributes
    put32(out, localOffset);
    out.insert(out.end(), entryName.begin(), entryName.end());

    const size_t centralDirSize = out.size() - centralDirOffset;

    // End of central directory record.
    put32(out, 0x06054b50u);
    put16(out, 0);  // disk number
    put16(out, 0);  // disk with central dir
    put16(out, 1);  // entries on this disk
    put16(out, 1);  // total entries
    put32(out, static_cast<uint32_t>(centralDirSize));
    put32(out, static_cast<uint32_t>(centralDirOffset));
    put16(out, 0);  // comment length

    return out;
}

}  // namespace

TEST_CASE("filesystem.identityAndPaths") {
    useIdentity("ev_ut_fs_paths");
    auto* f = fs();
    CHECK_EQ(f->getIdentity(), std::string("ev_ut_fs_paths"));
    CHECK(!f->getWorkingDirectory().empty());
    CHECK(!f->getUserDirectory().empty());
    CHECK(!f->getAppdataDirectory().empty());
    CHECK(!f->getSaveDirectory().empty());
    CHECK(!f->getExecutablePath().empty());

    f->setFused(true);
    CHECK(f->isFused());
    // fusedSet is sticky — second call ignored; still true
    f->setFused(false);
    CHECK(f->isFused());

    f->setSymlinksEnabled(false);
    CHECK(!f->areSymlinksEnabled());
    f->setSymlinksEnabled(true);
    CHECK(f->areSymlinksEnabled());

    auto& req = f->getRequirePath();
    CHECK_GE(req.size(), 0u);
    (void)f->getCRequirePath();

    // Android-only APIs: exercise default getters/setters on desktop
    f->setAndroidSaveExternal(true);
    CHECK(f->isAndroidSaveExternal());
    f->setAndroidSaveExternal(false);
    CHECK(!f->isAndroidSaveExternal());
}

TEST_CASE("filesystem.writeReadAppendRemove") {
    useIdentity("ev_ut_fs_rw");
    auto* f = fs();
    const char* name = "ut_rw.txt";
    const char* payload = "hello-fs";
    f->write(name, payload, static_cast<int64_t>(std::strlen(payload)));

    std::unique_ptr<eve::filesystem::FileData> data(f->read(name));
    REQUIRE(data.get() != nullptr);
    CHECK_EQ(data->getSize(), std::strlen(payload));
    CHECK(std::memcmp(data->getData(), payload, data->getSize()) == 0);

    const char* more = "!";
    f->append(name, more, 1);
    std::unique_ptr<eve::filesystem::FileData> data2(f->read(name));
    REQUIRE(data2.get() != nullptr);
    CHECK_EQ(data2->getSize(), std::strlen(payload) + 1);

    eve::filesystem::Filesystem::Info fileInfo{};
    REQUIRE(f->getInfo(name, fileInfo));
    CHECK_EQ(fileInfo.type, std::string("file"));
    CHECK_GE(fileInfo.size, 0);

    CHECK(f->createDirectory("ut_subdir"));
    auto items = f->getDirectoryItems("");
    bool found = false;
    for (const auto& it : items) {
        if (it == name || it == "ut_subdir" || it == "ut_subdir/") found = true;
    }
    CHECK(found);

    CHECK(f->remove(name));
    CHECK(!f->getInfo(name, fileInfo));

    bool threw = false;
    try {
        f->read("definitely_missing_ut_file_zzz.dat");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_CASE("filesystem.atomicTextReplacePreservesContract") {
    useIdentity("ev_ut_fs_atomic_text");
    auto* f = fs();
    const std::string name = "slot1.sav";
    REQUIRE(f->writeTextAtomic(name, "version=1").ok());
    CHECK_EQ(f->readText(name), std::string("version=1"));
    REQUIRE(f->writeTextAtomic(name, "version=2\n角色=勇者").ok());
    CHECK_EQ(f->readText(name), std::string("version=2\n角色=勇者"));

    auto rejected = f->writeTextAtomic("../outside.sav", "invalid");
    CHECK(!rejected.ok());
    CHECK_EQ(f->readText(name), std::string("version=2\n角色=勇者"));

    bool sawTemporary = false;
    for (const auto& item : f->getDirectoryItems("")) {
        if (item.find("slot1.sav.tmp.") != std::string::npos) sawTemporary = true;
    }
    CHECK(!sawTemporary);
    CHECK(f->remove(name));
}

TEST_CASE("filesystem.setSourceSecondCallFails") {
    auto* f = fs();
    // `game_source` is a sticky, process-wide singleton field: only the very
    // first call across the whole test binary can succeed. We are the only
    // TEST_CASE that calls setSource(), so this exercises the happy path.
    std::string cwd      = f->getWorkingDirectory();
    bool        firstOk  = f->setSource(cwd);
    if (firstOk) {
        CHECK_EQ(f->getSource(), cwd);
        // getSourceBaseDirectory() strips the last path component of the source.
        std::string base = f->getSourceBaseDirectory();
        CHECK(!base.empty());
        CHECK(cwd.rfind(base, 0) == 0);
    } else {
        // Defensive fallback in case some earlier test already set the source.
        CHECK(!f->getSource().empty());
    }
    // Second call (regardless of which path above ran) must always fail.
    CHECK(!f->setSource("somewhere_else"));
}

TEST_CASE("filesystem.File.openReadWriteSeek") {
    useIdentity("ev_ut_fs_file");
    auto* f = fs();
    const char* name = "ev_sub/ut_file_api.bin";
    CHECK(f->createDirectory("ev_sub"));
    f->write(name, "0123456789", 10);

    std::unique_ptr<eve::filesystem::FileData> viaFs(f->read(name));
    REQUIRE(viaFs.get() != nullptr);
    CHECK_EQ(viaFs->getSize(), 10u);
    CHECK(std::memcmp(viaFs->getData(), "0123456789", 10) == 0);

    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    REQUIRE(file.get() != nullptr);
    CHECK_EQ(file->getFilename(), std::string(name));
    CHECK(file->open("rb"));
    CHECK(file->isOpen());
    CHECK_EQ(file->getMode(), std::string("rb"));
    CHECK_EQ(file->getSize(), 10);
    CHECK_EQ(file->tell(), 0);

    char buf[4] = {};
    // CHECK_EQ evaluates lhs twice (compare + print) — do not put side-effecting reads there.
    const int64_t nRead = file->read(buf, 4);
    CHECK_EQ(nRead, 4);
    CHECK(std::memcmp(buf, "0123", 4) == 0);
    CHECK(file->seek(0));
    CHECK_EQ(file->tell(), 0);

    std::unique_ptr<eve::filesystem::FileData> head(file->read(4));
    REQUIRE(head.get() != nullptr);
    CHECK_EQ(head->getSize(), 4u);
    CHECK(std::memcmp(head->getData(), "0123", 4) == 0);
    CHECK(file->seek(0));
    CHECK_EQ(file->tell(), 0);
    std::unique_ptr<eve::filesystem::FileData> chunk(file->read(2));
    REQUIRE(chunk.get() != nullptr);
    CHECK_EQ(chunk->getSize(), 2u);
    CHECK(std::memcmp(chunk->getData(), "01", 2) == 0);

    // Love-style "r" alias must open for reading.
    CHECK(file->close());
    CHECK(file->open("r"));
    CHECK_EQ(file->getMode(), std::string("rb"));
    CHECK(file->close());
    CHECK(!file->isOpen());

    bool badMode = false;
    try {
        std::unique_ptr<eve::filesystem::File> f2(f->newFile(name));
        if (!f2->open("not-a-mode"))
            badMode = true;
    } catch (const eve::Exception&) {
        badMode = true;
    }
    CHECK(badMode);

    f->remove(name);
}

TEST_CASE("filesystem.FileData.metaAndClone") {
    useIdentity("ev_ut_fs_filedata");
    auto* f = fs();
    const char raw[] = "meta";
    std::unique_ptr<eve::filesystem::FileData> fd(
        f->newFileData(raw, "folder/shot.png", 4));
    REQUIRE(fd.get() != nullptr);
    CHECK_EQ(fd->getSize(), 4u);
    CHECK(std::memcmp(fd->getData(), raw, 4) == 0);
    CHECK_EQ(fd->getFilename(), std::string("folder/shot.png"));
    CHECK_EQ(fd->getExtension(), std::string("png"));
    CHECK_EQ(fd->getName(), std::string("folder/shot"));
    std::unique_ptr<eve::filesystem::FileData> cloned(fd->clone());
    CHECK_EQ(cloned->getExtension(), std::string("png"));
}

TEST_CASE("filesystem.mount.missingReturnsFalse") {
    auto* f = fs();
    CHECK(!f->mount("Z:/evengine_definitely_missing_archive_xxx.zip", "/m", false));
    CHECK(!f->unmount("Z:/evengine_definitely_missing_archive_xxx.zip"));
}

TEST_CASE("filesystem.mount.realDirRoundTrip") {
    useIdentity("ev_ut_fs_mount");
    auto* f = fs();
    f->write("mounted_marker.txt", "M", 1);
    std::string realSave = f->getSaveDirectory();
    f->allowMountingForPath(realSave);
    REQUIRE(f->isRealDirectory(realSave));
    REQUIRE(f->mount(realSave, "/mnt", false));
    CHECK(f->unmount(realSave));
    f->remove("mounted_marker.txt");
}

TEST_CASE("filesystem.mount.memoryDataRoundTrip") {
    useIdentity("ev_ut_fs_mount_mem");
    auto* f = fs();

    const std::string content = "hi-from-memory-zip";
    auto               zipBytes = buildMinimalZip("hello.txt", content);
    eve::data::ByteData zipData(zipBytes.data(), zipBytes.size());

    REQUIRE(f->mount(&zipData, "ut_mem.zip", "/memzip", false));

    eve::filesystem::Filesystem::Info zipEntryInfo{};
    REQUIRE(f->getInfo("/memzip/hello.txt", zipEntryInfo));
    CHECK_EQ(zipEntryInfo.type, std::string("file"));

    std::unique_ptr<eve::filesystem::FileData> read(f->read("/memzip/hello.txt"));
    REQUIRE(read.get() != nullptr);
    CHECK_EQ(read->getSize(), content.size());
    CHECK(std::memcmp(read->getData(), content.data(), content.size()) == 0);

    CHECK(f->unmount(&zipData));
    // Once unmounted, the mounted content should no longer resolve.
    CHECK(!f->getInfo("/memzip/hello.txt", zipEntryInfo));
}

TEST_CASE("filesystem.getRealDirectory") {
    useIdentity("ev_ut_fs_realdir");
    auto* f = fs();
    f->write("ut_realdir_marker.txt", "x", 1);

    std::string real = f->getRealDirectory("ut_realdir_marker.txt");
    CHECK(!real.empty());
    CHECK_EQ(real, f->getSaveDirectory());

    bool threw = false;
    try {
        f->getRealDirectory("ut_realdir_definitely_missing_zzz.dat");
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);

    f->remove("ut_realdir_marker.txt");
}

TEST_CASE("filesystem.File.extensionAndEof") {
    useIdentity("ev_ut_fs_ext_eof");
    auto* f = fs();

    std::unique_ptr<eve::filesystem::File> withExt(f->newFile("dir/archive.tar.gz"));
    CHECK_EQ(withExt->getExtension(), std::string("gz"));

    std::unique_ptr<eve::filesystem::File> noExt(f->newFile("dir/README"));
    CHECK_EQ(noExt->getExtension(), std::string());

    const char* name = "ut_eof.bin";
    f->write(name, "ab", 2);
    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    // Not yet open: contract treats an unopened file as EOF.
    CHECK(file->isEOF());

    REQUIRE(file->open("rb"));
    CHECK(!file->isEOF());

    char buf[2] = {};
    CHECK_EQ(file->read(buf, 2), 2);
    CHECK(file->isEOF());

    CHECK(file->close());
    f->remove(name);
}

TEST_CASE("filesystem.File.bufferAndFlush") {
    useIdentity("ev_ut_fs_buf");
    auto* f = fs();
    const char* name = "ut_buf.txt";
    std::unique_ptr<eve::filesystem::File> file(f->newFile(name));
    CHECK(file->setBuffer("full", 4096));
    REQUIRE(file->open("wb"));
    int64_t sz = 0;
    CHECK_EQ(file->getBuffer(sz), std::string("full"));
    CHECK_EQ(sz, 4096);
    CHECK(file->write("x", 1));
    CHECK(file->flush());
    CHECK(file->close());
    f->remove(name);
}

TEST_CASE("filesystem.watch.fileModified") {
    useIdentity("ev_ut_fs_watch");
    auto* f = fs();
    f->unwatchAll();

    const char* name = "ut_watch.txt";
    f->write(name, "v1", 2);
    REQUIRE(f->watch(name));
    CHECK_EQ(f->getWatchCount(), 1);

    // Darwin DirectoryWatcher baselines via periodic scan; wait one interval.
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    while (!f->pollWatch().empty()) {
    }

    // Prefer OS write through the resolved save path so notifications fire reliably.
    std::string real = f->getSaveDirectory() + "/" + name;
    {
        std::ofstream out(real, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    bool saw = false;
    for (int i = 0; i < 60 && !saw; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (;;) {
            std::string kind = f->pollWatch();
            if (kind.empty()) break;
            if (kind == "modified" || kind == "added") {
                CHECK_EQ(f->getLastWatchPath(), std::string(name));
                saw = true;
            }
        }
    }
    CHECK(saw);

    CHECK(f->unwatch(name));
    CHECK_EQ(f->getWatchCount(), 0);
    f->remove(name);
}

TEST_CASE("filesystem.watch.coalescesOverlappingRegistrations") {
    const char *name = "ut_watch_coalesce.txt";
    const auto  dir = std::filesystem::temp_directory_path() / "ev_ut_fs_watch_coalesce";
    std::filesystem::remove_all(dir);
    REQUIRE(std::filesystem::create_directories(dir));
    {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v1", 2);
    }

    eve::filesystem::FileWatch watch;
    REQUIRE(watch.add(dir.string(), "", "."));
    REQUIRE(watch.add(dir.string(), name, name));
    CHECK_EQ(watch.count(), 2);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    eve::filesystem::FileWatch::Event event;
    while (watch.poll(event)) {
    }

    {
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    int matchingEvents = 0;
    while (watch.poll(event)) {
        if (event.realPath.find(name) != std::string::npos) {
            ++matchingEvents;
            CHECK_EQ(event.path, std::string(name));
        }
    }
    CHECK_EQ(matchingEvents, 1);
    watch.clear();
    std::filesystem::remove_all(dir);
}

TEST_CASE("filesystem.watch.unwatchAll") {
    useIdentity("ev_ut_fs_watch_all");
    auto* f = fs();
    f->unwatchAll();
    f->write("a.txt", "a", 1);
    f->write("b.txt", "b", 1);
    CHECK(f->watch("a.txt"));
    CHECK(f->watch("b.txt"));
    CHECK_GE(f->getWatchCount(), 2);
    f->unwatchAll();
    CHECK_EQ(f->getWatchCount(), 0);
    f->remove("a.txt");
    f->remove("b.txt");
}

TEST_CASE("filesystem.watch.dotResolvesToCwd") {
    useIdentity("ev_ut_fs_watch_dot");
    auto *f = fs();
    f->unwatchAll();

    std::string cwd = f->getWorkingDirectory();
    REQUIRE(!cwd.empty());
    (void)f->setSource(cwd);

    REQUIRE(f->watch("."));
    CHECK_GE(f->getWatchCount(), 1);

    const char *name = "ut_watch_dot.txt";
    {
        std::ofstream out(cwd + "/" + name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v1", 2);
        out.flush();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    while (!f->pollWatch().empty()) {
    }

    {
        std::ofstream out(cwd + "/" + name, std::ios::binary | std::ios::trunc);
        REQUIRE(out.good());
        out.write("v2-changed", 10);
        out.flush();
    }

    bool saw = false;
    for (int i = 0; i < 60 && !saw; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        for (;;) {
            std::string kind = f->pollWatch();
            if (kind.empty()) break;
            if (kind == "modified" || kind == "added") {
                std::string p = f->getLastWatchPath();
                if (p == name || p == std::string("./") + name ||
                    p.find(name) != std::string::npos)
                    saw = true;
            }
        }
    }
    CHECK(saw);

    f->unwatch(".");
    std::remove((cwd + "/" + name).c_str());
}

