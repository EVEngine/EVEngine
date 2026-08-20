#include "macosx.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <sys/syslimits.h>

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
