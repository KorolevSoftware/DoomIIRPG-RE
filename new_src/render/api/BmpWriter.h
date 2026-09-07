#ifndef NEW_RENDER_API_BMPWRITER_H
#define NEW_RENDER_API_BMPWRITER_H

#include <cstdint>

namespace newcore {

// Writes w*h*4 RGBA bytes as a 24-bit BMP (alpha dropped). Set bottomUp when
// the source rows already run bottom-up, as glReadPixels returns them; BMP
// stores rows bottom-up, so top-down input is written in reverse.
bool writeBmp24(const char* path, const uint8_t* rgba, int w, int h, bool bottomUp);

} // namespace newcore

#endif // NEW_RENDER_API_BMPWRITER_H
