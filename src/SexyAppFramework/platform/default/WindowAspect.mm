#include <SDL.h>
#include <SDL_syswm.h>
#import <AppKit/AppKit.h>

void SetCocoaWindowAspect(SDL_Window* window)
{
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_COCOA)
        [info.info.cocoa.window setContentAspectRatio:NSMakeSize(4, 3)];
}
