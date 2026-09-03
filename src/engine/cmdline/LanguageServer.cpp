#include "cmdline.h"
#include "common/config.h"

#include <CLI11.hpp>

#include <cstdio>
#include <iostream>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
#include "devtools/LanguageServer.hpp"

#include <filesystem>
#endif

namespace eve::cmd {

struct LanguageServerArgs : Handler {
    std::string root;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto command = app.add_subcommand("language-server", "Run the EveScript LSP server over stdio");
        command->formatter(formatter);
        command->add_option("-r,--root", root, "Project root used to resolve game:/ modules");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto command = app.get_subcommand("language-server");
        return command->parsed() ? cmd.LanguageServer(root) : -1;
    }
};

CMD_REG(LanguageServerArgs);

int Cmdline::LanguageServer(std::string path) {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS) || defined(EVENGINE_WEBGPU)
    (void)path;
    std::cerr << "eve language-server: desktop-only" << std::endl;
    return 4;
#else
#if defined(_WIN32)
    if (_setmode(_fileno(stdin), _O_BINARY) == -1 || _setmode(_fileno(stdout), _O_BINARY) == -1) {
        std::cerr << "eve language-server: failed to configure binary stdio" << std::endl;
        return 4;
    }
#endif
    std::error_code error;
    if (path.empty()) path = std::filesystem::current_path(error).string();
    eve::dev::LanguageServer server(std::move(path));
    return server.runStdio(std::cin, std::cout);
#endif
}

}  // namespace eve::cmd
