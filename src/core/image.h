#pragma once
// ============================================================
//  Image - 8-bit RGBA load/save (png, tga, jpg, bmp) through
//  stb_image, with file access through util (UTF-8 paths).
// ============================================================
#include <cstdint>
#include <string>
#include <vector>

namespace cp77 {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;     // RGBA, row 0 = top
    bool empty() const { return w <= 0 || h <= 0; }
    const uint8_t* at(int x, int y) const { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]; }
    uint8_t* at(int x, int y) { return &px[(static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4]; }
};

bool LoadImage(const std::string& path, Image& out, std::string& error);
bool SavePng(const std::string& path, const Image& img, std::string& error);
// Bilinear sample of channel c at UV (0..1, wraps), result 0..1.
double SampleBilinear(const Image& img, double u, double v, int c);

} // namespace cp77
