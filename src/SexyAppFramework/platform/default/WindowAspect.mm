#include <SDL.h>
#include <SDL_syswm.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
SDL_Rect GetUIKitSafeArea(SDL_Window* window)
{
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return {0, 0, width, height};
    UIWindow* native = info.info.uikit.window;
    const auto insets = native.safeAreaInsets;
    const auto bounds = native.bounds.size;
    if (bounds.width <= 0 || bounds.height <= 0) return {0, 0, width, height};
    const int left = int(insets.left * width / bounds.width);
    const int top = int(insets.top * height / bounds.height);
    const int right = int(insets.right * width / bounds.width);
    const int bottom = int(insets.bottom * height / bounds.height);
    return {left, top, width - left - right, height - top - bottom};
}
#else
#import <AppKit/AppKit.h>
void SetCocoaWindowAspect(SDL_Window* window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_COCOA)
        [info.info.cocoa.window setContentAspectRatio:NSMakeSize(0, 0)];
}
#endif
