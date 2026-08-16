#include "cmdline.h"
#include "zip_writer.h"
#include <filesystem>
#include <CLI11.hpp>
#include <rang.hpp>

using namespace std::filesystem;
using namespace std;

namespace eve::cmd
{

struct ZipArgs : Handler {
    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto subcmd = app.add_subcommand("zip", "Zip the game to a .eve package");
        subcmd->allow_extras();
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto subcmd = app.get_subcommand("zip");
        if (subcmd->parsed()) {
            string name = cmd.get_remaining(subcmd, ".");
            int res = cmd.Zip(name);
            return res;
        }
        return -1; // not handle
    }
};

CMD_REG(ZipArgs);


int Cmdline::Zip(std::string path) {
    std::error_code ec;
    if (!is_directory(path, ec)) {
        cerr << rang::fg::red << "Not a directory: " << rang::fg::reset << path << endl;
        return 1;
    }

    std::string outName = path;
    // Normalize trailing separators so the output name is the game folder name.
    while (!outName.empty() && (outName.back() == '/' || outName.back() == '\\'))
        outName.pop_back();
    outName += ".eve";

    if (!cmdline::createGameArchive(path, outName)) {
        cerr << rang::fg::red << "Failed to write archive: " << rang::fg::reset << outName << endl;
        return 2;
    }

    cout << rang::fg::green << "Packaged game -> " << rang::fg::reset << outName << endl;
    return 0;
}

} // namespace eve
