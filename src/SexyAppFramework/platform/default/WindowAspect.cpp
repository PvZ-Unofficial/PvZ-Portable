#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_system.h>
#include <algorithm>
#ifdef __ANDROID__
#include <jni.h>
extern "C" JNIEXPORT void JNICALL Java_io_github_wszqkzqk_pvzportable_PvZPortableActivity_nativeViewportChanged(JNIEnv*, jclass)
{
    SDL_Event event{};
    event.type = SDL_WINDOWEVENT;
    event.window.event = SDL_WINDOWEVENT_SIZE_CHANGED;
    SDL_PushEvent(&event);
}
#endif
#ifdef SDL_VIDEO_DRIVER_UIKIT
SDL_Rect GetUIKitSafeArea(SDL_Window* window);
#endif
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
    LPARAM parameter, UINT_PTR id, DWORD_PTR data)
{
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hwnd, ConstrainWindow, id);
    auto* window = reinterpret_cast<SDL_Window*>(data);
    int minWidth, minHeight, maxWidth, maxHeight;
    SDL_GetWindowMinimumSize(window, &minWidth, &minHeight);
    SDL_GetWindowMaximumSize(window, &maxWidth, &maxHeight);
    const bool horizontalOnly = minHeight > 0 && minHeight == maxHeight;
    if (message == WM_NCHITTEST && horizontalOnly) {
        const auto hit = DefSubclassProc(hwnd, message, edge, parameter);
        if (hit == HTTOPLEFT || hit == HTBOTTOMLEFT) return HTLEFT;
        if (hit == HTTOPRIGHT || hit == HTBOTTOMRIGHT) return HTRIGHT;
        if (hit == HTTOP || hit == HTBOTTOM) return HTBORDER;
        return hit;
    }
    if (message != WM_SIZING) return DefSubclassProc(hwnd, message, edge, parameter);
    RECT outer{}, client{};
    GetWindowRect(hwnd, &outer);
    GetClientRect(hwnd, &client);
    const int borderWidth = outer.right - outer.left - client.right;
    const int borderHeight = outer.bottom - outer.top - client.bottom;
    auto& rect = *reinterpret_cast<RECT*>(parameter);
    if (horizontalOnly) {
        const int width = std::clamp(int(rect.right - rect.left - borderWidth), minWidth, maxWidth);
        if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
            rect.left = rect.right - width - borderWidth;
        else rect.right = rect.left + width + borderWidth;
        rect.top = outer.top;
        rect.bottom = outer.bottom;
        if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) rect = outer;
        return TRUE;
    }
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
        SetWindowSubclass(info.info.win.window, ConstrainWindow, 1, reinterpret_cast<DWORD_PTR>(window));
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
        hints.flags &= ~PAspect;
        hints.min_aspect.x = hints.max_aspect.x = 4;
        hints.min_aspect.y = hints.max_aspect.y = 3;
        XSetWMNormalHints(info.info.x11.display, info.info.x11.window, &hints);
    }
#endif
    // Size bounds are maintained by the native viewport; Wayland may ignore them.
}

SDL_Rect GetGameSafeArea(SDL_Window* window)
{
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
#ifdef SDL_VIDEO_DRIVER_UIKIT
    return GetUIKitSafeArea(window);
#elif defined(__ANDROID__)
    auto* env = static_cast<JNIEnv*>(SDL_AndroidGetJNIEnv());
    jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
    if (env && activity) {
        jclass type = env->GetObjectClass(activity);
        jmethodID method = env->GetMethodID(type, "getGameSafeInsets", "()[I");
        if (method) {
            auto values = static_cast<jintArray>(env->CallObjectMethod(activity, method));
            if (values && env->GetArrayLength(values) == 4) {
                jint insets[4]{};
                env->GetIntArrayRegion(values, 0, 4, insets);
                env->DeleteLocalRef(values);
                env->DeleteLocalRef(type);
                env->DeleteLocalRef(activity);
                return {insets[0], insets[1], std::max(0,width-insets[0]-insets[2]), std::max(0,height-insets[1]-insets[3])};
            }
            if (values) env->DeleteLocalRef(values);
        }
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteLocalRef(type);
        env->DeleteLocalRef(activity);
    }
#endif
    return {0, 0, width, height};
}

double GetGameWindowDpiScale(SDL_Window* window)
{
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info) && info.subsystem == SDL_SYSWM_WINDOWS)
        return std::max(96u, GetDpiForWindow(info.info.win.window)) / 96.0;
#endif
    // SDL window coordinates are points on Cocoa/UIKit and window units on X11/Wayland.
    return 1.0;
}
