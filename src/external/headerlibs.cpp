#define CUTE_FILES_IMPLEMENTATION
#include "cute_files.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

// Enables Vorbis decoding, header comes before mini audio
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"    
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
// The stb_vorbis implementation must come after the implementation of miniaudio.
#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"