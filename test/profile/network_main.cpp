// clang-format off
// eve-test-policy: {"schema":"evengine.test-policy","version":1,"role":"compile-contract","module":"network","default":"module-composition"}
// clang-format on
#include <iostream>
#include <string>
#include "common/CrashHandler.h"
#include "network/Network.h"
#include "zeroerr/unittest.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    eve::installCrashHandler();
#endif
    std::string url;
    std::string ca;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.starts_with("--https-url=")) url = argument.substr(12);
        if (argument.starts_with("--ca-location=")) ca = argument.substr(14);
    }
    if (!url.empty()) {
        eve::network::Network network;
        if (!ca.empty()) network.setCaLocation(ca);
        int         status = 0;
        std::string body;
        const auto  received = network.httpRequest("GET", url, "", 15000, status, body);
        std::cout << "Engine HTTPS verified=" << network.getVerifySsl() << " received=" << received
                  << " status=" << status << " bytes=" << body.size() << '\n';
        return received && status == 200 && !body.empty() ? 0 : 1;
    }
    zeroerr::UnitTest test;
    test.parseArgs(argc, const_cast<const char**>(argv));
    return test.run();
}
