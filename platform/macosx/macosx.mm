#include "macosx.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <sys/syslimits.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>


namespace eve {
namespace macosx {

std::string getResources() {
    @autoreleasepool {
        NSString *resPath = [[NSBundle mainBundle] resourcePath];
        if (resPath == nil)
            return {};

        NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:resPath error:nil];
        if (contents == nil)
            return {};

        for (NSString *file in contents) {
            NSString *lower = [file lowercaseString];
            if ([lower hasSuffix:@".love"] || [lower hasSuffix:@".eve"]) {
                NSString *full = [resPath stringByAppendingPathComponent:file];
                return std::string([full UTF8String]);
            }
        }

        // Fall back to the Resources directory itself when no game archive is present.
        return std::string([resPath UTF8String]);
    }
}

std::string checkDropEvents() {
    // Drop-file handling is not wired into the event loop yet.
    return {};
}

std::string getExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};

    char resolved[PATH_MAX];
    if (realpath(buffer.data(), resolved) != nullptr)
        return std::string(resolved);

    return std::string(buffer.data());
}

std::string bootstrapBundledVulkan() {
    const std::string exe = getExecutablePath();
    if (exe.empty())
        return {};

    const auto slash = exe.find_last_of('/');
    if (slash == std::string::npos)
        return {};
    const std::string exeDir = exe.substr(0, slash);

    // Packaged EVEngine distributions ship the loader + MoltenVK next to the
    // executable: ../lib in the flat SDK layout (dist/eve-sdk/macosx), lib in
    // a packaged game (eve package copies the SDK lib/ tree to <out>/lib), or
    // ../Frameworks inside an .app bundle.
    const std::string dir = [&]() -> std::string {
        for (const char *rel : {"../lib", "lib", "../Frameworks"}) {
            const std::string candidate = exeDir + "/" + rel;
            if (::access((candidate + "/libvulkan.1.dylib").c_str(), R_OK) == 0)
                return candidate;
        }
        return {};
    }();
    if (dir.empty())
        return {};

    // Make the bundled loader discover the bundled MoltenVK. The manifest's
    // library_path is relative to the manifest directory, so pointing
    // VK_ICD_FILENAMES at <dir>/MoltenVK_icd.json is enough. Respect an
    // explicit user override.
    const std::string icd = dir + "/MoltenVK_icd.json";
    if (::access(icd.c_str(), R_OK) == 0 && ::getenv("VK_ICD_FILENAMES") == nullptr) {
        // The Khronos loader resolves library_path with dlopen() and searches
        // the process cwd / system directories — NOT the manifest directory —
        // so a relative "libMoltenVK.dylib" never loads and the ICD is ignored
        // (surface extensions then appear missing). Rewrite the manifest with
        // the absolute dylib path before pointing the loader at it.
        std::string json;
        if (FILE *f = std::fopen(icd.c_str(), "rb")) {
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
            std::fclose(f);
        }
        const std::string absLib = dir + "/libMoltenVK.dylib";
        const std::string key = "\"library_path\"";
        const auto keyPos = json.find(key);
        if (keyPos != std::string::npos) {
            const auto colonPos = json.find(':', keyPos);
            const auto q1 = json.find('"', colonPos);
            const auto q2 = json.find('"', q1 + 1);
            if (colonPos != std::string::npos && q1 != std::string::npos &&
                q2 != std::string::npos && q2 > q1) {
                json.replace(q1, q2 - q1 + 1, "\"" + absLib + "\"");
                if (FILE *f = std::fopen(icd.c_str(), "wb")) {
                    std::fwrite(json.data(), 1, json.size(), f);
                    std::fclose(f);
                }
            }
        }
        ::setenv("VK_ICD_FILENAMES", icd.c_str(), 1);
    }

    return dir;
}

void requestAttention(bool continuous) {
    @autoreleasepool {
        NSApplication *app = [NSApplication sharedApplication];
        if (app == nil)
            return;

        NSRequestUserAttentionType type =
            continuous ? NSCriticalRequest : NSInformationalRequest;
        [app requestUserAttention:type];
    }
}

void setIconRGBA(const uint8_t *rgba, int width, int height) {
    if (rgba == nullptr)
        return;

    @autoreleasepool {
        const int w = width;
        const int h = height;
        if (w <= 0 || h <= 0)
            return;

        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc]
            initWithBitmapDataPlanes:nullptr
                          pixelsWide:w
                          pixelsHigh:h
                       bitsPerSample:8
                     samplesPerPixel:4
                            hasAlpha:YES
                            isPlanar:NO
                      colorSpaceName:NSDeviceRGBColorSpace
                         bytesPerRow:w * 4
                        bitsPerPixel:32];
        if (rep == nil)
            return;

        memcpy([rep bitmapData], rgba, static_cast<size_t>(w) * h * 4);

        NSImage *nsimage = [[NSImage alloc] initWithSize:NSMakeSize(w, h)];
        [nsimage addRepresentation:rep];
        [rep release];

        NSApplication *app = [NSApplication sharedApplication];
        if (app != nil)
            [app setApplicationIconImage:nsimage];

        [nsimage release];
    }
}

}  // namespace macosx
}  // namespace eve
