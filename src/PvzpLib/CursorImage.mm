#include "CursorImage.h"
#import <AppKit/AppKit.h>
bool NativeCursorImage(bool hand, std::vector<std::uint32_t>& pixels, int& width, int& height)
{
    @autoreleasepool {
        NSImage* image = [(hand ? [NSCursor pointingHandCursor] : [NSCursor arrowCursor]) image];
        CGImageRef source = [image CGImageForProposedRect:nullptr context:nil hints:nil];
        if (!source) return false;
        width = static_cast<int>(CGImageGetWidth(source));
        height = static_cast<int>(CGImageGetHeight(source));
        std::vector<unsigned char> rgba(static_cast<std::size_t>(width) * height * 4);
        CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
        CGContextRef context = CGBitmapContextCreate(rgba.data(), width, height, 8, width * 4, space, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(space);
        if (!context) return false;
        CGContextDrawImage(context, CGRectMake(0, 0, width, height), source);
        CGContextRelease(context);
        pixels.resize(static_cast<std::size_t>(width) * height);
        for (std::size_t i = 0; i < pixels.size(); ++i)
            pixels[i] = std::uint32_t(rgba[i * 4 + 3]) << 24 | std::uint32_t(rgba[i * 4]) << 16 | std::uint32_t(rgba[i * 4 + 1]) << 8 | rgba[i * 4 + 2];
        return true;
    }
}
