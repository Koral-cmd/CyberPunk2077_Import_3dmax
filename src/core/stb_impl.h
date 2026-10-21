#pragma once
// stb is third-party code: no warnings from it in our -Werror builds.
#if defined(__GNUC__)
#  pragma GCC system_header
#elif defined(_MSC_VER)
#  pragma warning(push, 0)
#endif
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "../../third_party/stb/stb_image.h"
#define STBI_WRITE_NO_STDIO
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "../../third_party/stb/stb_image_write.h"
#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
