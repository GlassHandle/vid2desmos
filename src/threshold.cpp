#include "threshold.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace v2d {
namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

const std::array<int, 64>& bayer8() {
    static const std::array<int, 64> m = {
        0,  32, 8,  40, 2,  34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44, 4,  36, 14, 46, 6,  38,
        60, 28, 52, 20, 62, 30, 54, 22,
        3,  35, 11, 43, 1,  33, 9,  41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47, 7,  39, 13, 45, 5,  37,
        63, 31, 55, 23, 61, 29, 53, 21,
    };
    return m;
}

std::vector<std::uint32_t> integralImage(const Image& src) {
    const int w = src.width();
    const int h = src.height();
    std::vector<std::uint32_t> sum(static_cast<std::size_t>(w + 1) * (h + 1), 0);
    for (int y = 0; y < h; ++y) {
        std::uint32_t rowAcc = 0;
        const std::uint8_t* srcRow = src.row(y);
        const std::uint32_t* prev = sum.data() + static_cast<std::size_t>(y) * (w + 1);
        std::uint32_t* cur = sum.data() + static_cast<std::size_t>(y + 1) * (w + 1);
        for (int x = 0; x < w; ++x) {
            rowAcc += srcRow[x];
            cur[x + 1] = prev[x + 1] + rowAcc;
        }
    }
    return sum;
}

BinaryImage thresholdFixed(const Image& src, int value, bool inv) {
    BinaryImage out(src.width(), src.height());
    const std::uint8_t* in = src.data();
    std::uint8_t* dst = out.data();
    for (std::size_t i = 0; i < src.size(); ++i) {
        const bool on = in[i] >= value;
        dst[i] = static_cast<std::uint8_t>(on != inv);
    }
    return out;
}

BinaryImage thresholdAdaptive(const Image& src, int blockSize, int bias, bool inv) {
    if (blockSize < 3) blockSize = 3;
    if (blockSize % 2 == 0) ++blockSize;

    const int w = src.width();
    const int h = src.height();
    const auto sum = integralImage(src);
    const int r = blockSize / 2;

    BinaryImage out(w, h);
    for (int y = 0; y < h; ++y) {
        const int y0 = std::max(0, y - r);
        const int y1 = std::min(h - 1, y + r);
        const std::uint8_t* srcRow = src.row(y);
        std::uint8_t* dstRow = out.row(y);
        for (int x = 0; x < w; ++x) {
            const int x0 = std::max(0, x - r);
            const int x1 = std::min(w - 1, x + r);
            const std::uint32_t total = sum[static_cast<std::size_t>(y1 + 1) * (w + 1) + (x1 + 1)] -
                                        sum[static_cast<std::size_t>(y0) * (w + 1) + (x1 + 1)] -
                                        sum[static_cast<std::size_t>(y1 + 1) * (w + 1) + x0] +
                                        sum[static_cast<std::size_t>(y0) * (w + 1) + x0];
            const int count = (x1 - x0 + 1) * (y1 - y0 + 1);
            const int mean = static_cast<int>(total / static_cast<std::uint32_t>(count));
            const bool on = srcRow[x] >= (mean - bias);
            dstRow[x] = static_cast<std::uint8_t>(on != inv);
        }
    }
    return out;
}

BinaryImage thresholdFloydSteinberg(const Image& src, bool inv) {
    const int w = src.width();
    const int h = src.height();
    BinaryImage out(w, h);

    std::vector<float> curr(static_cast<std::size_t>(w) + 2, 0.0f);
    std::vector<float> next(static_cast<std::size_t>(w) + 2, 0.0f);

    for (int y = 0; y < h; ++y) {
        const std::uint8_t* srcRow = src.row(y);
        std::uint8_t* dstRow = out.row(y);
        std::fill(next.begin(), next.end(), 0.0f);

        for (int x = 0; x < w; ++x) {
            const float value = srcRow[x] + curr[x + 1];
            const bool on = value >= 128.0f;
            dstRow[x] = static_cast<std::uint8_t>(on != inv);

            const float err = value - (on ? 255.0f : 0.0f);
            curr[x + 2] += err * (7.0f / 16.0f);
            next[x] += err * (3.0f / 16.0f);
            next[x + 1] += err * (5.0f / 16.0f);
            next[x + 2] += err * (1.0f / 16.0f);
        }
        curr.swap(next);
    }
    return out;
}

BinaryImage thresholdBayer(const Image& src, bool inv) {
    const auto& m = bayer8();
    BinaryImage out(src.width(), src.height());
    for (int y = 0; y < src.height(); ++y) {
        const std::uint8_t* srcRow = src.row(y);
        std::uint8_t* dstRow = out.row(y);
        for (int x = 0; x < src.width(); ++x) {

            const int t = (m[static_cast<std::size_t>(y & 7) * 8 + (x & 7)] * 4) + 2;
            const bool on = srcRow[x] > t;
            dstRow[x] = static_cast<std::uint8_t>(on != inv);
        }
    }
    return out;
}

}

std::optional<ThresholdMode> thresholdModeByName(std::string_view rawName) {
    const std::string name = lower(rawName);
    if (name == "fixed") return ThresholdMode::Fixed;
    if (name == "otsu" || name == "auto") return ThresholdMode::Otsu;
    if (name == "adaptive" || name == "local") return ThresholdMode::Adaptive;
    if (name == "dither" || name == "floyd") return ThresholdMode::Dither;
    if (name == "bayer" || name == "ordered") return ThresholdMode::Bayer;
    return std::nullopt;
}

std::string thresholdModeName(ThresholdMode mode) {
    switch (mode) {
        case ThresholdMode::Fixed: return "fixed";
        case ThresholdMode::Otsu: return "otsu";
        case ThresholdMode::Adaptive: return "adaptive";
        case ThresholdMode::Dither: return "dither";
        case ThresholdMode::Bayer: return "bayer";
    }
    return "unknown";
}

int otsuThreshold(const Image& src) {
    if (src.empty()) {
        return 128;
    }
    std::array<std::size_t, 256> hist{};
    hist.fill(0);
    const std::uint8_t* in = src.data();
    for (std::size_t i = 0; i < src.size(); ++i) {
        ++hist[in[i]];
    }

    const double total = static_cast<double>(src.size());
    double sumAll = 0.0;
    for (int i = 0; i < 256; ++i) {
        sumAll += static_cast<double>(i) * hist[i];
    }

    double sumB = 0.0;
    double wB = 0.0;
    double best = -1.0;
    int bestT = 128;
    for (int t = 0; t < 256; ++t) {
        wB += static_cast<double>(hist[t]);
        if (wB == 0.0) continue;
        const double wF = total - wB;
        if (wF == 0.0) break;

        sumB += static_cast<double>(t) * hist[t];
        const double mB = sumB / wB;
        const double mF = (sumAll - sumB) / wF;
        const double between = wB * wF * (mB - mF) * (mB - mF);
        if (between > best) {
            best = between;
            bestT = t;
        }
    }

    return bestT + 1;
}

BinaryImage threshold(const Image& src, const ThresholdOptions& opts) {
    if (src.empty()) {
        throw std::invalid_argument("threshold: empty source image");
    }
    switch (opts.mode) {
        case ThresholdMode::Fixed:
            return thresholdFixed(src, opts.value, opts.invert);
        case ThresholdMode::Otsu:
            return thresholdFixed(src, otsuThreshold(src), opts.invert);
        case ThresholdMode::Adaptive:
            return thresholdAdaptive(src, opts.blockSize, opts.bias, opts.invert);
        case ThresholdMode::Dither:
            return thresholdFloydSteinberg(src, opts.invert);
        case ThresholdMode::Bayer:
            return thresholdBayer(src, opts.invert);
    }
    throw std::invalid_argument("threshold: unknown mode");
}

}
