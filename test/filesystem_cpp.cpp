#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "filesystem/Filesystem.h"
#include "filesystem/File.h"
#include "filesystem/FileData.h"

#include <chrono>
#include <cstring>
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

TEST_CASE("filesystem.setSourceSecondCallFails") {
    auto* f = fs();
    // First call may succeed or already be set by earlier process use.
    (void)f->setSource(".");
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

