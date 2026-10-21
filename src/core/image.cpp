#include "image.h"
#include "util.h"
#include "stb_impl.h"

#include <cmath>

namespace cp77 {

bool LoadImage(const std::string& path, Image& out, std::string& error) {
    out = Image();
    std::string data;
    if (!ReadFile(path, data)) { error = "cannot read " + path; return false; }
    int w = 0, h = 0, n = 0;
    unsigned char* p = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(data.data()), static_cast<int>(data.size()), &w, &h, &n, 4);
    if (!p) { error = path + ": " + (stbi_failure_reason() ? stbi_failure_reason() : "unknown format"); return false; }
    out.w = w;
    out.h = h;
    out.px.assign(p, p + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    stbi_image_free(p);
    return true;
}

static void Append(void* ctx, void* data, int size) {
    static_cast<std::string*>(ctx)->append(static_cast<const char*>(data), static_cast<size_t>(size));
}

bool SavePng(const std::string& path, const Image& img, std::string& error) {
    if (img.empty()) { error = "empty image"; return false; }
    std::string buf;
    if (!stbi_write_png_to_func(Append, &buf, img.w, img.h, 4, img.px.data(), img.w * 4)) { error = "png encode failed"; return false; }
    MakeDirs(DirName(path));
    if (!WriteFile(path, buf)) { error = "cannot write " + path; return false; }
    return true;
}

double SampleBilinear(const Image& img, double u, double v, int c) {
    if (img.empty()) return 0.0;
    const double x = (u - std::floor(u)) * img.w - 0.5, y = (v - std::floor(v)) * img.h - 0.5;
    const double fx = std::floor(x), fy = std::floor(y);
    const double ax = x - fx, ay = y - fy;
    auto px = [&](double xi, double yi) {
        int ix = static_cast<int>(xi) % img.w, iy = static_cast<int>(yi) % img.h;
        if (ix < 0) ix += img.w;
        if (iy < 0) iy += img.h;
        return img.at(ix, iy)[c] / 255.0;
    };
    return (px(fx, fy) * (1 - ax) + px(fx + 1, fy) * ax) * (1 - ay) + (px(fx, fy + 1) * (1 - ax) + px(fx + 1, fy + 1) * ax) * ay;
}

} // namespace cp77
