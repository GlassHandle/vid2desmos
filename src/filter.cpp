#include "filter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace v2d {

void invert(Image& img) {
    std::uint8_t* p = img.data();
    for (std::size_t i = 0; i < img.size(); ++i) {
        p[i] = static_cast<std::uint8_t>(255 - p[i]);
    }
}

void invert(FloatImage& img) {
    float* p = img.data();
    for (std::size_t i = 0; i < img.size(); ++i) {
        p[i] = 255.0f - p[i];
    }
}

void applyGamma(Image& img, float gamma) {
    if (gamma <= 0.0f) {
        throw std::invalid_argument("gamma must be positive");
    }
    std::array<std::uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const float v = std::pow(i / 255.0f, gamma) * 255.0f;
        lut[i] = static_cast<std::uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, v))));
    }
    std::uint8_t* p = img.data();
    for (std::size_t i = 0; i < img.size(); ++i) {
        p[i] = lut[p[i]];
    }
}

void applyContrast(Image& img, float contrast, float brightness) {
    std::array<std::uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        const float v = (i - 128.0f) * contrast + 128.0f + brightness;
        lut[i] = static_cast<std::uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, v))));
    }
    std::uint8_t* p = img.data();
    for (std::size_t i = 0; i < img.size(); ++i) {
        p[i] = lut[p[i]];
    }
}

void normalize(Image& img) {
    if (img.empty()) {
        return;
    }
    std::uint8_t* p = img.data();
    std::uint8_t lo = 255;
    std::uint8_t hi = 0;
    for (std::size_t i = 0; i < img.size(); ++i) {
        lo = std::min(lo, p[i]);
        hi = std::max(hi, p[i]);
    }
    if (hi <= lo) {
        return;
    }
    const float scale = 255.0f / static_cast<float>(hi - lo);
    for (std::size_t i = 0; i < img.size(); ++i) {
        p[i] = static_cast<std::uint8_t>(std::lround((p[i] - lo) * scale));
    }
}

Image rgbToGray(const std::uint8_t* rgb, int w, int h) {
    Image out(w, h);
    std::uint8_t* dst = out.data();
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        const int r = rgb[i * 3 + 0];
        const int g = rgb[i * 3 + 1];
        const int b = rgb[i * 3 + 2];

        dst[i] = static_cast<std::uint8_t>((r * 77 + g * 150 + b * 29) >> 8);
    }
    return out;
}

Image resizeArea(const Image& src, int dstW, int dstH) {
    if (dstW <= 0 || dstH <= 0) {
        throw std::invalid_argument("resizeArea: destination must be non-empty");
    }
    if (src.empty()) {
        throw std::invalid_argument("resizeArea: source is empty");
    }
    if (src.width() == dstW && src.height() == dstH) {
        return src;
    }

    Image out(dstW, dstH);
    const double sx = static_cast<double>(src.width()) / dstW;
    const double sy = static_cast<double>(src.height()) / dstH;

    for (int y = 0; y < dstH; ++y) {
        const int y0 = static_cast<int>(y * sy);
        int y1 = static_cast<int>((y + 1) * sy);
        if (y1 <= y0) y1 = y0 + 1;
        y1 = std::min(y1, src.height());

        std::uint8_t* dstRow = out.row(y);
        for (int x = 0; x < dstW; ++x) {
            const int x0 = static_cast<int>(x * sx);
            int x1 = static_cast<int>((x + 1) * sx);
            if (x1 <= x0) x1 = x0 + 1;
            x1 = std::min(x1, src.width());

            std::uint32_t sum = 0;
            for (int yy = y0; yy < y1; ++yy) {
                const std::uint8_t* srcRow = src.row(yy);
                for (int xx = x0; xx < x1; ++xx) {
                    sum += srcRow[xx];
                }
            }
            const std::uint32_t count = static_cast<std::uint32_t>(x1 - x0) *
                                        static_cast<std::uint32_t>(y1 - y0);
            dstRow[x] = static_cast<std::uint8_t>((sum + count / 2) / count);
        }
    }
    return out;
}

}
