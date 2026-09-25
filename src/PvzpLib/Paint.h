#pragma once
#include "Plugin.h"
namespace Sexy { class Graphics; }
namespace PvzpPlugin {
using PaintCallback = void (*)();
PVZP_API bool SetPaintCallback(PaintCallback callback);
PVZP_API bool PaintGridRow(int* row);
PVZP_API bool PaintPointer(bool sprite,int* x,int* y);
PVZP_API bool PaintSize(std::uint32_t* width, std::uint32_t* height);
PVZP_API bool PaintCursor(int x,int y,bool hand);
PVZP_API bool PaintRect(int x, int y, int width, int height, std::uint32_t argb);
PVZP_API bool PaintClip(bool enabled, int x, int y, int width, int height);
PVZP_API bool PaintText(int x, int y, const char* text, int length, const char16_t* family,
    int pixels, bool bold, std::uint32_t argb, bool measure, std::uint32_t* width, std::uint32_t* height);
void Paint(Sexy::Graphics& graphics);
void ClearPaint();
}
