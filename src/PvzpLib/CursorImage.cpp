#ifdef __linux__
#include "CursorImage.h"
#include <X11/Xcursor/Xcursor.h>
bool NativeCursorImage(bool hand, std::vector<std::uint32_t>& pixels, int& width, int& height)
{
    XcursorImage* image = XcursorLibraryLoadImage(hand ? "hand2" : "left_ptr", nullptr, 24);
    if (!image) return false;
    width = image->width; height = image->height;
    pixels.assign(image->pixels, image->pixels + width * height);
    XcursorImageDestroy(image);
    return true;
}
#endif
