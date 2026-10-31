#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdio>
#include <cstring>

int main() {
    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device) return 2;
        std::fprintf(stderr, "Metal render probe: device=%s class=%s\n", device.name.UTF8String,
                     NSStringFromClass(device.class).UTF8String);
        NSString*      source  = @"#include <metal_stdlib>\n"
                                  "using namespace metal;\n"
                                  "vertex float4 generated(uint i [[vertex_id]]) {\n"
                                  "  const float2 p[] = {float2(-1,-1),float2(3,-1),float2(-1,3)};\n"
                                  "  return float4(p[i],0,1);\n"
                                  "}\n"
                                  "vertex float4 uploaded(uint i [[vertex_id]], const device float4* p [[buffer(0)]]) {\n"
                                  "  return p[i];\n"
                                  "}\n"
                                  "fragment float4 red() { return float4(1,0,0,1); }\n";
        NSError*       error   = nil;
        id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
        if (!library) {
            std::fprintf(stderr, "Metal shader compilation failed: %s\n", error.description.UTF8String);
            return 3;
        }
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue) return 4;
        constexpr NSUInteger size        = 64;
        const float          positions[] = {-1, -1, 0, 1, 3, -1, 0, 1, -1, 3, 0, 1};
        id<MTLBuffer>        vertices    = [device newBufferWithBytes:positions
                                                     length:sizeof(positions)
                                                    options:MTLResourceStorageModeShared];
        id<MTLBuffer>        pixels = [device newBufferWithLength:size * size * 4 options:MTLResourceStorageModeShared];
        if (!vertices || !pixels) return 5;
        MTLTextureDescriptor* textureDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                               width:size
                                                              height:size
                                                           mipmapped:NO];
        textureDesc.usage       = MTLTextureUsageRenderTarget;
        textureDesc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> target   = [device newTextureWithDescriptor:textureDesc];
        if (!target) return 6;
        bool passed = true;
        for (NSString* entry in @[ @"generated", @"uploaded" ]) {
            MTLRenderPipelineDescriptor* pipelineDesc    = [[MTLRenderPipelineDescriptor alloc] init];
            pipelineDesc.vertexFunction                  = [library newFunctionWithName:entry];
            pipelineDesc.fragmentFunction                = [library newFunctionWithName:@"red"];
            pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
            id<MTLRenderPipelineState> pipeline          = [device newRenderPipelineStateWithDescriptor:pipelineDesc
                                                                                         error:&error];
            if (!pipeline) {
                std::fprintf(stderr, "Metal pipeline %s failed: %s\n", entry.UTF8String, error.description.UTF8String);
                passed = false;
                continue;
            }
            std::memset(pixels.contents, 0, size * size * 4);
            id<MTLCommandBuffer>     commands    = [queue commandBuffer];
            MTLRenderPassDescriptor* pass        = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture     = target;
            pass.colorAttachments[0].loadAction  = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor  = MTLClearColorMake(0, 0, 1, 1);
            id<MTLRenderCommandEncoder> render   = [commands renderCommandEncoderWithDescriptor:pass];
            [render setRenderPipelineState:pipeline];
            [render setViewport:MTLViewport{0, 0, double(size), double(size), 0, 1}];
            [render setCullMode:MTLCullModeNone];
            [render setVertexBuffer:vertices offset:0 atIndex:0];
            [render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [render endEncoding];
            id<MTLBlitCommandEncoder> copy = [commands blitCommandEncoder];
            [copy copyFromTexture:target
                             sourceSlice:0
                             sourceLevel:0
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(size, size, 1)
                                toBuffer:pixels
                       destinationOffset:0
                  destinationBytesPerRow:size * 4
                destinationBytesPerImage:size * size * 4];
            [copy endEncoding];
            [commands commit];
            [commands waitUntilCompleted];
            const auto*      rgba   = static_cast<const unsigned char*>(pixels.contents);
            const NSUInteger center = (size / 2 * size + size / 2) * 4;
            const bool       valid  = commands.status == MTLCommandBufferStatusCompleted && rgba[center] > 240 &&
                               rgba[center + 1] < 10 && rgba[center + 2] < 10;
            std::fprintf(stderr, "Metal %s: status=%lu center=%u,%u,%u,%u result=%s error=%s\n", entry.UTF8String,
                         static_cast<unsigned long>(commands.status), rgba[center], rgba[center + 1], rgba[center + 2],
                         rgba[center + 3], valid ? "PASS" : "FAIL",
                         commands.error ? commands.error.description.UTF8String : "none");
            passed &= valid;
        }
        return passed ? 0 : 1;
    }
}
