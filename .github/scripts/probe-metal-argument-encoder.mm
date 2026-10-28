#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 2;
        const NSUInteger count = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 1;
        std::fprintf(stderr, "Metal device=%s class=%s argumentBuffersTier=%lu resources=%lu\n", device.name.UTF8String,
                     NSStringFromClass(device.class).UTF8String,
                     static_cast<unsigned long>(device.argumentBuffersSupport), static_cast<unsigned long>(count));
        std::fflush(stderr);
        NSMutableArray<MTLArgumentDescriptor*>* arguments = [NSMutableArray array];
        NSUInteger                              index     = 0;
        for (NSNumber* type in @[ @(MTLDataTypePointer), @(MTLDataTypeTexture), @(MTLDataTypeSampler) ]) {
            MTLArgumentDescriptor* argument = [MTLArgumentDescriptor argumentDescriptor];
            argument.index                  = index;
            argument.dataType               = static_cast<MTLDataType>(type.unsignedIntegerValue);
            argument.arrayLength            = count;
            argument.access                 = MTLArgumentAccessReadOnly;
            argument.textureType            = MTLTextureType2D;
            [arguments addObject:argument];
            index += count;
        }
        id<MTLArgumentEncoder> encoder = [device newArgumentEncoderWithArguments:arguments];
        if (!encoder) return 3;
        std::fprintf(stderr, "Metal argument encoder length=%lu\n", static_cast<unsigned long>(encoder.encodedLength));
    }
    return 0;
}
