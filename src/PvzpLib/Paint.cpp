// Native overlay resources belong to this App/plugin and are destroyed during
// explicit plugin revocation, outside both loader lock and the paint callback.
#include "Paint.h"
#include "CursorImage.h"
#include "LawnApp.h"
#include "Lawn/Board.h"
#include "Lawn/CursorObject.h"
#include "widget/WidgetManager.h"
#include "graphics/Graphics.h"
#include "graphics/MemoryImage.h"
#include <algorithm>
#include <array>
#include <new>
#include <climits>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PvzpPlugin {
namespace {
Sexy::Graphics* canvas = nullptr; // Borrowed only for the dynamic Paint scope.
Sexy::Rect originalClip;
std::vector<std::unique_ptr<Sexy::MemoryImage>> rasterImages;
#if defined(__linux__) || defined(__APPLE__)
std::array<std::unique_ptr<Sexy::MemoryImage>, 2> nativeCursors;
#endif
#if defined(_WIN32)
struct Font {
    std::u16string family;
    int pixels;
    bool bold;
    HFONT native = nullptr;
    std::unordered_map<wchar_t, std::unique_ptr<Sexy::MemoryImage>> glyphs;
    ~Font() { if (native) DeleteObject(native); }
};
struct Resources {
    std::vector<std::unique_ptr<Font>> fonts;
    std::wstring text;
    std::array<std::unique_ptr<Sexy::MemoryImage>,2> cursors;
    std::array<HCURSOR,2> cursor_shapes{};
};
Resources* resources = nullptr;
Font* FontFor(const char16_t* family, int pixels, bool bold) {
    if (!resources) resources = new Resources;
    for (auto& font : resources->fonts)
        if (font->family == family && font->pixels == pixels && font->bold == bold) return font.get();
    auto font = std::make_unique<Font>();
    font->family = family; font->pixels = pixels; font->bold = bold;
    font->native = CreateFontW(-pixels, 0, 0, 0, bold ? FW_BOLD : 100,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
        ANTIALIASED_QUALITY, FIXED_PITCH | FF_DONTCARE, reinterpret_cast<const wchar_t*>(family));
    if (!font->native) return nullptr;
    auto* result = font.get(); resources->fonts.push_back(std::move(font)); return result;
}
Sexy::MemoryImage* GlyphFor(Font& font, wchar_t character) {
    auto found = font.glyphs.find(character);
    if (found != font.glyphs.end()) return found->second.get();
    const int height = font.pixels * 11 / 10 + 1;
    struct Bitmap {
        HDC dc = CreateCompatibleDC(nullptr);
        HBITMAP bitmap = nullptr;
        HGDIOBJ oldBitmap = nullptr, oldFont = nullptr;
        ~Bitmap() {
            if (oldBitmap) SelectObject(dc, oldBitmap);
            if (oldFont) SelectObject(dc, oldFont);
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
        }
    } bitmap;
    if (!bitmap.dc) return nullptr;
    bitmap.oldFont = SelectObject(bitmap.dc, font.native);
    if (!bitmap.oldFont || bitmap.oldFont == HGDI_ERROR) return nullptr;
    RECT measured{0,0,font.pixels * 11 / 10,height};
    DrawTextW(bitmap.dc,&character,1,&measured,DT_CALCRECT | DT_NOPREFIX);
    const int width=std::max(1,static_cast<int>(measured.right));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    bitmap.bitmap = CreateDIBSection(bitmap.dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap.bitmap || !bits) return nullptr;
    bitmap.oldBitmap = SelectObject(bitmap.dc, bitmap.bitmap);
    if (!bitmap.oldBitmap || bitmap.oldBitmap == HGDI_ERROR) return nullptr;
    std::memset(bits,0,static_cast<std::size_t>(width)*height*4);
    SetTextColor(bitmap.dc, RGB(255,255,255)); SetBkMode(bitmap.dc,TRANSPARENT);
    RECT rect{0,1,width,height};
    DrawTextW(bitmap.dc,&character,1,&rect,DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX);
    GdiFlush();
    auto image = std::make_unique<Sexy::MemoryImage>(gLawnApp);
    auto* source = static_cast<std::uint32_t*>(bits);
    for (int i = 0; i < width * height; ++i)
        source[i] = ((source[i] & 0xff) << 24) | 0xffffff;
    // Create() is lazy and leaves mBits null. SetBits makes the owning copy,
    // including MemoryImage's sentinel, before the temporary DIB is destroyed.
    image->SetBits(source, width, height, true);
    auto* result = image.get(); font.glyphs.emplace(character, std::move(image)); return result;
}
#endif
Sexy::Color Color(std::uint32_t value) {
    return Sexy::Color((value >> 16) & 255, (value >> 8) & 255, value & 255, value >> 24);
}
}

bool SetPaintCallback(PaintCallback callback) {
    if (!gLawnApp || canvas || !gLawnApp->mPlugin.validated) return false;
    if (callback && (gLawnApp->mPlugin.stopRequested || gLawnApp->mPlugin.shutdownAttempted)) return false;
    gLawnApp->mPlugin.paintCallback = callback;
    if (!callback) ClearPaint();
    return true;
}
void ClearPaint() {
    if (canvas) return; // Host callback-depth gate prevents physical revocation here.
    if (gLawnApp) gLawnApp->mPlugin.paintCallback = nullptr;
    rasterImages.clear();
#if defined(__linux__) || defined(__APPLE__)
    for (auto& cursor : nativeCursors) cursor.reset();
#endif
    if (gLawnApp && gLawnApp->mWidgetManager) gLawnApp->mWidgetManager->MarkAllDirty();
#if defined(_WIN32)
    delete resources; resources = nullptr;
#endif
}
void PreparePaint() {
    if (!gLawnApp || !gLawnApp->mWidgetManager) return;
    const auto& host = gLawnApp->mPlugin;
    if (!host.enabled || host.stopRequested || host.callbackDepth || !host.paintCallback || canvas) return;
    // Overlays are drawn into the same framebuffer as the widgets. Repaint the
    // base before every overlay frame, including while native updates are paused,
    // so moved/disabled overlays never leave pixels from the preceding frame.
    gLawnApp->mWidgetManager->MarkAllDirty();
}
void Paint(Sexy::Graphics& graphics) {
    if (!gLawnApp) return;
    auto& host = gLawnApp->mPlugin;
    if (!host.enabled || host.stopRequested || host.callbackDepth || !host.paintCallback || canvas) return;
    CallbackScope callback;
    struct Scope {
        ~Scope() { canvas = nullptr; }
    } scope;
    // Caller gives us a separate Graphics, so color/clip changes never leak
    // into the game's existing Graphics state.
    canvas = &graphics; originalClip = graphics.mClipRect;
    graphics.SetLinearBlend(false);
#if defined(_WIN32)
    if(!resources)resources=new(std::nothrow) Resources;
    if(resources && (!resources->cursor_shapes[0]||!resources->cursor_shapes[1])
        && gLawnApp->mWidgetManager && gLawnApp->mWidgetManager->mMouseIn
        && (gLawnApp->mCursorNum==0||gLawnApp->mCursorNum==1)) {
        const int kind=gLawnApp->mCursorNum;
        CURSORINFO current{};current.cbSize=sizeof(current);
        if(!resources->cursor_shapes[kind] && GetCursorInfo(&current) && current.hCursor){
            resources->cursor_shapes[kind]=current.hCursor;resources->cursors[kind].reset();
        }
    }
#endif
    host.paintCallback();
}
bool PaintGridRow(int* row) {
    if (!canvas || !row || !gLawnApp->mBoard || !gLawnApp->mBoard->mCursorPreview) return false;
    *row=gLawnApp->mBoard->mCursorPreview->mGridY; return true;
}
bool PaintPointer(bool sprite,int* x,int* y) {
    if (!canvas || !x || !y) return false;
    if (sprite) {
        if (!gLawnApp->mBoard || !gLawnApp->mBoard->mCursorObject) return false;
        *x=gLawnApp->mBoard->mCursorObject->mX; *y=gLawnApp->mBoard->mCursorObject->mY;
    } else {
        if (!gLawnApp->mWidgetManager) return false;
        *x=gLawnApp->mWidgetManager->mLastMouseX; *y=gLawnApp->mWidgetManager->mLastMouseY;
    }
    return true;
}
bool PaintSize(std::uint32_t* width, std::uint32_t* height) {
    if (!canvas || !width || !height) return false;
    *width = gLawnApp->mWidth; *height = gLawnApp->mHeight; return true;
}
bool PaintRect(int x, int y, int width, int height, std::uint32_t argb) {
    if (!canvas) return false;
    if (width <= 0 || height <= 0) return true;
    const auto right = std::min<std::int64_t>(static_cast<std::int64_t>(x) + width, gLawnApp->mWidth);
    const auto bottom = std::min<std::int64_t>(static_cast<std::int64_t>(y) + height, gLawnApp->mHeight);
    x = std::max(x, 0); y = std::max(y, 0);
    if (right <= x || bottom <= y) return true;
    canvas->SetColor(Color(argb)); canvas->FillRect(x, y, static_cast<int>(right-x), static_cast<int>(bottom-y));
    return true;
}
std::uint32_t PaintCreateImage(const std::uint32_t* pixels, int width, int height) {
    if (!canvas || !pixels || width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    try {
        auto image = std::make_unique<Sexy::MemoryImage>(gLawnApp);
        image->SetBits(const_cast<std::uint32_t*>(pixels), width, height, true);
        rasterImages.push_back(std::move(image));
        return static_cast<std::uint32_t>(rasterImages.size());
    } catch (...) { return 0; }
}
bool PaintImage(std::uint32_t id, int x, int y, std::uint32_t argb) {
    if (!canvas || !id || id > rasterImages.size()) return false;
    canvas->SetColor(Color(argb)); canvas->SetColorizeImages(true);
    canvas->DrawImage(rasterImages[id - 1].get(), x, y);
    return true;
}
bool PaintClip(bool enabled, int x, int y, int width, int height) {
    if (!canvas) return false;
    canvas->mClipRect = originalClip;
    if (enabled) canvas->ClipRect(x,y,std::max(width,0),std::max(height,0));
    return true;
}
bool PaintText(int x, int y, const char* text, int length, const char16_t* family,
    int pixels, bool bold, std::uint32_t argb, bool measure, std::uint32_t* width, std::uint32_t* height) {
    if (!canvas || !text || length < 0 || !family || pixels <= 0 || pixels > 4096 || !width || !height) return false;
#if defined(_WIN32)
    try {
        auto* font = FontFor(family, pixels, bold);
        if (!font) return false;
        const int count = length ? MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0) : 0;
        if (length && !count) return false;
        resources->text.resize(count);
        if (count && !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, resources->text.data(), count)) return false;
        const int line = pixels;
        std::int64_t cx = 0, cy = 0, longest = 0;
        for (wchar_t character : resources->text) {
            if (character == L'\r') continue;
            if (character == L'\n') { longest = std::max(longest,cx); cx = 0; cy += line; continue; }
            if (cx > INT_MAX || cy > INT_MAX || static_cast<std::int64_t>(x)+cx > INT_MAX || static_cast<std::int64_t>(y)+cy > INT_MAX) return false;
            if (!measure) {
                auto* glyph = GlyphFor(*font, character);
                if (!glyph) return false;
                canvas->SetColor(Color(argb)); canvas->SetColorizeImages(true);
                canvas->DrawImage(glyph, x+static_cast<int>(cx), y+static_cast<int>(cy));
            }
            cx += character <= 0xff ? pixels / 2 + 1 : pixels;
        }
        longest = std::max(longest,cx);
        if (longest > UINT_MAX || cy + line > UINT_MAX) return false;
        *width = static_cast<std::uint32_t>(longest); *height = count ? static_cast<std::uint32_t>(cy + (resources->text.back()==L'\n' ? 0 : line)) : 0; return true;
    } catch (...) { return false; }
#else
    return false;
#endif
}
}

namespace PvzpPlugin {
bool PaintCursor(int x,int y,bool hand) {
    if(!canvas)return false;
#if defined(_WIN32)
    try {
        if(!resources)resources=new Resources;
        auto& image=resources->cursors[hand];
        if(!image){
            const int width=GetSystemMetrics(SM_CXCURSOR)+1,height=GetSystemMetrics(SM_CYCURSOR)+1;
            if(width<=0||height<=0)return false;
            HDC dc=CreateCompatibleDC(nullptr);if(!dc)return false;
            struct DC{HDC dc;~DC(){DeleteDC(dc);}}dc_guard{dc};
            BITMAPINFO info{};
            info.bmiHeader={sizeof(BITMAPINFOHEADER),width,-height,1,32,BI_RGB,0,0,0,0,0};
            void* pixels=nullptr;
            HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
            if(!bitmap||!pixels){if(bitmap)DeleteObject(bitmap);return false;}
            HGDIOBJ previous=SelectObject(dc,bitmap);
            struct Bitmap{HDC dc;HBITMAP bitmap;HGDIOBJ previous;~Bitmap(){SelectObject(dc,previous);DeleteObject(bitmap);}} bitmap_guard{dc,bitmap,previous};
            const auto count=static_cast<std::size_t>(width)*height;
            std::memset(pixels,0,count*4);
            HCURSOR standard=LoadCursorW(nullptr,hand?MAKEINTRESOURCEW(32649):MAKEINTRESOURCEW(32512));
            HCURSOR shape=resources->cursor_shapes[hand]?resources->cursor_shapes[hand]:standard;
            if(!DrawIconEx(dc,0,0,shape,0,0,0,nullptr,DI_NORMAL|DI_COMPAT))return false;
            GdiFlush();auto* bits=static_cast<DWORD*>(pixels);
            if(shape!=standard){
                if(hand){
                    for(int row=0;row<height;++row){DWORD* previous=bits;
                        for(int col=0;col<width;++col){auto* current=bits+row*width+col;
                            if(*previous!=0xffffff&&*current==0xffffff)*previous=0xff000000;
                            else if(*previous==0xffffff&&*current!=0xffffff)*current=0xff000000;
                            previous=current;
                        }
                    }
                    for(int col=0;col<width;++col){DWORD* previous=bits;
                        for(int row=0;row<height;++row){auto* current=bits+row*width+col;
                            if(*previous!=0xffffff&&*current==0xffffff)*previous=0xff000000;
                            else if(*previous==0xffffff&&*current!=0xffffff)*current=0xff000000;
                            previous=current;
                        }
                    }
                }
                for(std::size_t i=0;i<count;++i)bits[i]=bits[i]==0xffffff?0xffffffff:bits[i]==0?0:0xff000000;
            }
            auto created=std::make_unique<Sexy::MemoryImage>(gLawnApp);
            created->SetBits(reinterpret_cast<std::uint32_t*>(bits),width,height,true);
            image=std::move(created);
        }
        canvas->SetColor(Sexy::Color(255,255,255,255));canvas->SetColorizeImages(true);
        canvas->DrawImage(image.get(),x,y);return true;
    }catch(...){return false;}
#elif defined(__linux__) || defined(__APPLE__)
    try {
        auto& image = nativeCursors[hand];
        if (!image) {
            int width = 0, height = 0;
            std::vector<std::uint32_t> pixels;
            if (!NativeCursorImage(hand, pixels, width, height)) return false;
            for (auto& pixel : pixels) {
                const auto alpha = pixel >> 24;
                if (alpha && alpha < 255) {
                    const auto channel = [alpha](std::uint32_t value) { return std::min(255u, value * 255 / alpha); };
                    pixel = alpha << 24 | channel((pixel >> 16) & 255) << 16 | channel((pixel >> 8) & 255) << 8 | channel(pixel & 255);
                }
            }
            image = std::make_unique<Sexy::MemoryImage>(gLawnApp);
            image->SetBits(pixels.data(), width, height, true);
        }
        canvas->SetColor(Sexy::Color(255,255,255,255)); canvas->SetColorizeImages(true);
        canvas->DrawImage(image.get(),x,y); return true;
    } catch (...) { return false; }
#else
    return false;
#endif
}
}
