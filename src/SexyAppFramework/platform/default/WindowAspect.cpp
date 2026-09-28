#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_system.h>
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
#include <commctrl.h>
#endif
#if defined(SDL_VIDEO_DRIVER_X11) && defined(PVZP_X11_WINDOW_ASPECT)
#include <X11/Xutil.h>
#endif

#if defined(SDL_VIDEO_DRIVER_WINDOWS)
// SDL2 has no aspect-ratio API. Constrain the proposed client size before SDL
// handles it; the title bar and borders are not part of the game canvas.
static LRESULT CALLBACK ConstrainWindow(HWND hwnd, UINT message, WPARAM edge,
    LPARAM parameter, UINT_PTR id, DWORD_PTR)
{
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ConstrainWindow, id);
    if (message != WM_SIZING) return DefSubclassProc(hwnd, message, edge, parameter);
    RECT outer{}, client{};
    GetWindowRect(hwnd, &outer);
    GetClientRect(hwnd, &client);
    const int borderWidth = outer.right - outer.left - client.right;
    const int borderHeight = outer.bottom - outer.top - client.bottom;
    auto& rect = *reinterpret_cast<RECT*>(parameter);
    if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM)
        rect.right = rect.left + (rect.bottom - rect.top - borderHeight) * 4 / 3 + borderWidth;
    else
    {
        const int height = (rect.right - rect.left - borderWidth) * 3 / 4 + borderHeight;
        if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) rect.top = rect.bottom - height;
        else rect.bottom = rect.top + height;
    }
    return TRUE;
}
#endif

#if defined(SDL_VIDEO_DRIVER_COCOA)
void SetCocoaWindowAspect(SDL_Window* window);
#endif

void SetGameWindowAspect(SDL_Window* window)
{
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        SetWindowSubclass(info.info.win.window, ConstrainWindow, 1, 0);
#elif defined(SDL_VIDEO_DRIVER_COCOA)
    SetCocoaWindowAspect(window);
#elif defined(SDL_VIDEO_DRIVER_X11) && defined(PVZP_X11_WINDOW_ASPECT)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_X11)
    {
        XSizeHints hints{};
        long supplied = 0;
        XGetWMNormalHints(info.info.x11.display, info.info.x11.window, &hints, &supplied);
        hints.flags |= PAspect;
        hints.min_aspect.x = hints.max_aspect.x = 4;
        hints.min_aspect.y = hints.max_aspect.y = 3;
        XSetWMNormalHints(info.info.x11.display, info.info.x11.window, &hints);
    }
#endif
    // Wayland and mobile keep the existing proportional viewport/letterboxing.
}
