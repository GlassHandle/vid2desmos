#include "frame.hpp"

#include <algorithm>
#include <cmath>

namespace v2d {

FloatImage toFloat(const Image& src) {
    FloatImage out(src.width(), src.height());
    const std::uint8_t* in = src.data();
    float* dst = out.data();
    for (std::size_t i = 0; i < src.size(); ++i) {
        dst[i] = static_cast<float>(in[i]);
    }
    return out;
}

Image toImage(const FloatImage& src, bool normalize) {
    Image out(src.width(), src.height());
    if (src.empty()) {
        return out;
    }

    const float* in = src.data();
    std::uint8_t* dst = out.data();

    float scale = 1.0f;
    float offset = 0.0f;
    if (normalize) {
        float lo = in[0];
        float hi = in[0];
        for (std::size_t i = 1; i < src.size(); ++i) {
            lo = std::min(lo, in[i]);
            hi = std::max(hi, in[i]);
        }
        const float span = hi - lo;
        if (span > 1e-6f) {
            scale = 255.0f / span;
            offset = -lo * scale;
        } else {

            scale = 0.0f;
            offset = 0.0f;
        }
    }

    for (std::size_t i = 0; i < src.size(); ++i) {
        const float v = in[i] * scale + offset;
        dst[i] = static_cast<std::uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, v))));
    }
    return out;
}

Image binaryToImage(const BinaryImage& mask) {
    Image out(mask.width(), mask.height());
    const std::uint8_t* in = mask.data();
    std::uint8_t* dst = out.data();
    for (std::size_t i = 0; i < mask.size(); ++i) {
        dst[i] = in[i] ? 255 : 0;
    }
    return out;
}

std::size_t countSet(const BinaryImage& mask) {
    std::size_t n = 0;
    const std::uint8_t* in = mask.data();
    for (std::size_t i = 0; i < mask.size(); ++i) {
        n += (in[i] != 0) ? 1u : 0u;
    }
    return n;
}

}
