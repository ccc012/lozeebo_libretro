#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_PNG
// GIF is needed for guest resources such as the Z-Wheel launcher splash
// (opening_low.gif). This is the translation unit that compiles the stb_image
// implementation, so the STBI_ONLY_* set here is what actually decides which
// decoders exist; without GIF, stbi_load_from_memory fails and BrewImage falls
// back to a 1x1 image (splash renders as a single pixel on black).
#define STBI_ONLY_GIF
#define STBI_NO_FAILURE_STRINGS

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "third_party/stb_image.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
