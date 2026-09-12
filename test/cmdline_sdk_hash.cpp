#include "Fixtures.h"
#include "cmdline/sdk_tools.h"
#include "zeroerr/unittest.h"

#include <array>
#include <barrier>
#include <fstream>
#include <future>
#include <string>
#include <vector>

TEST_CASE("cmdline.sdkSha256IndependentConcurrentReads") {
    TempDir dir;
    REQUIRE(!dir.path().empty());
    std::string binary;
    for (int i = 0; i < 10000; ++i) binary.append("\0\xff", 2);
    const std::array<std::string, 4> contents{"", "abc", "def", binary};
    const std::array<std::string, 4> expected{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                              "cb8379ac2098aa165029e3938a51da0bcecfc008fd6795f401178647f96c5b34",
                                              "42f918becde1d7a197cb96baebd4e9f7217e4492414f277e39198c4a7d17721d"};
    std::array<std::string, 4>       paths;
    for (size_t i = 0; i < paths.size(); ++i) {
        paths[i] = (dir.path() / (std::to_string(i) + ".bin")).string();
        std::ofstream out(paths[i], std::ios::binary);
        out.write(contents[i].data(), static_cast<std::streamsize>(contents[i].size()));
        REQUIRE(out.good());
    }
    // Calls on different files must not share a subprocess output file. The
    // larger binary also crosses the hash reader's internal chunk boundary.
    std::barrier                                         start(8);
    std::vector<std::future<std::array<std::string, 4>>> reads;
    for (int worker = 0; worker < 8; ++worker) {
        reads.push_back(std::async(std::launch::async, [&, worker] {
            std::array<std::string, 4> result;
            start.arrive_and_wait();
            for (size_t i = 0; i < result.size(); ++i)
                result[i] = eve::cmd::sdk::fileSha256(paths[(i + static_cast<size_t>(worker)) % paths.size()]);
            return result;
        }));
    }
    for (size_t worker = 0; worker < reads.size(); ++worker) {
        const auto result = reads[worker].get();
        for (size_t i = 0; i < result.size(); ++i) REQUIRE_EQ(result[i], expected[(i + worker) % expected.size()]);
    }
    REQUIRE(eve::cmd::sdk::fileSha256((dir.path() / "missing.bin").string()).empty());
}
