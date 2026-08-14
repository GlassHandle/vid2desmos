#include "convolution.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

namespace v2d {
namespace kernels {

Kernel identity() {
    return Kernel{1, 1, {1.0f}, 1.0f, 0.0f, "identity"};
}

Kernel box(int n) {
    if (n < 1 || n % 2 == 0) {
        throw std::invalid_argument("box kernel size must be odd and >= 1");
    }
    Kernel k;
    k.w = k.h = n;
    k.data.assign(static_cast<std::size_t>(n) * n, 1.0f);
    k.divisor = static_cast<float>(n * n);
    k.name = "box" + std::to_string(n);
    return k;
}

Kernel gaussian(int n, float sigma) {
    if (n < 1 || n % 2 == 0) {
        throw std::invalid_argument("gaussian kernel size must be odd and >= 1");
    }
    if (sigma <= 0.0f) {

        sigma = 0.3f * ((n - 1) * 0.5f - 1.0f) + 0.8f;
    }
    Kernel k;
    k.w = k.h = n;
    k.data.resize(static_cast<std::size_t>(n) * n);
    const int r = n / 2;
    const float twoSigmaSq = 2.0f * sigma * sigma;
    float sum = 0.0f;
    for (int y = -r; y <= r; ++y) {
        for (int x = -r; x <= r; ++x) {
            const float v = std::exp(-static_cast<float>(x * x + y * y) / twoSigmaSq);
            k.data[static_cast<std::size_t>(y + r) * n + (x + r)] = v;
            sum += v;
        }
    }
    k.divisor = sum;
    k.name = "gaussian" + std::to_string(n);
    return k;
}

Kernel sobelX() {
    return Kernel{3, 3, {-1, 0, 1, -2, 0, 2, -1, 0, 1}, 1.0f, 0.0f, "sobelx"};
}

Kernel sobelY() {
    return Kernel{3, 3, {-1, -2, -1, 0, 0, 0, 1, 2, 1}, 1.0f, 0.0f, "sobely"};
}

Kernel prewittX() {
    return Kernel{3, 3, {-1, 0, 1, -1, 0, 1, -1, 0, 1}, 1.0f, 0.0f, "prewittx"};
}

Kernel prewittY() {
    return Kernel{3, 3, {-1, -1, -1, 0, 0, 0, 1, 1, 1}, 1.0f, 0.0f, "prewitty"};
}

Kernel scharrX() {
    return Kernel{3, 3, {-3, 0, 3, -10, 0, 10, -3, 0, 3}, 1.0f, 0.0f, "scharrx"};
}

Kernel scharrY() {
    return Kernel{3, 3, {-3, -10, -3, 0, 0, 0, 3, 10, 3}, 1.0f, 0.0f, "scharry"};
}

Kernel laplacian() {
    return Kernel{3, 3, {0, 1, 0, 1, -4, 1, 0, 1, 0}, 1.0f, 0.0f, "laplacian"};
}

Kernel sharpen() {
    return Kernel{3, 3, {0, -1, 0, -1, 5, -1, 0, -1, 0}, 1.0f, 0.0f, "sharpen"};
}

Kernel emboss() {
    return Kernel{3, 3, {-2, -1, 0, -1, 1, 1, 0, 1, 2}, 1.0f, 128.0f, "emboss"};
}

}

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool parseSizedName(const std::string& name, std::string_view prefix, int fallback, int& sizeOut) {
    if (name.rfind(prefix.data(), 0, prefix.size()) != 0) {
        return false;
    }
    const std::string suffix = name.substr(prefix.size());
    if (suffix.empty()) {
        sizeOut = fallback;
        return true;
    }
    for (char c : suffix) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    sizeOut = std::stoi(suffix);
    return sizeOut >= 1 && sizeOut % 2 == 1;
}

}

std::optional<Kernel> kernelByName(std::string_view rawName) {
    const std::string name = lower(rawName);

    if (name == "identity" || name == "none") return kernels::identity();
    if (name == "sobelx") return kernels::sobelX();
    if (name == "sobely") return kernels::sobelY();
    if (name == "prewittx") return kernels::prewittX();
    if (name == "prewitty") return kernels::prewittY();
    if (name == "scharrx") return kernels::scharrX();
    if (name == "scharry") return kernels::scharrY();
    if (name == "laplacian") return kernels::laplacian();
    if (name == "sharpen") return kernels::sharpen();
    if (name == "emboss") return kernels::emboss();

    int n = 0;
    if (parseSizedName(name, "gaussian", 3, n)) return kernels::gaussian(n);
    if (parseSizedName(name, "blur", 3, n)) return kernels::gaussian(n);
    if (parseSizedName(name, "box", 3, n)) return kernels::box(n);

    return std::nullopt;
}

std::vector<std::string> kernelNames() {
    return {"identity", "gaussian<N>", "blur<N>",  "box<N>",   "sobelx",  "sobely",
            "prewittx", "prewitty",    "scharrx",  "scharry",  "laplacian",
            "sharpen",  "emboss"};
}

FloatImage convolve(const FloatImage& src, const Kernel& k) {
    if (src.empty()) {
        return FloatImage{};
    }
    if (k.data.size() != static_cast<std::size_t>(k.w) * static_cast<std::size_t>(k.h)) {
        throw std::invalid_argument("convolve: kernel data size does not match dimensions");
    }

    const int w = src.width();
    const int h = src.height();
    FloatImage out(w, h);

    const int ax = k.w / 2;
    const int ay = k.h / 2;
    const float invDiv = (k.divisor != 0.0f) ? 1.0f / k.divisor : 1.0f;

#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < h; ++y) {
        float* dstRow = out.row(y);
        const bool interiorRow = (y >= ay) && (y + ay < h);
        for (int x = 0; x < w; ++x) {
            const bool interior = interiorRow && (x >= ax) && (x + ax < w);
            float acc = 0.0f;
            if (interior) {
                for (int ky = 0; ky < k.h; ++ky) {
                    const float* srcRow = src.row(y + ky - ay) + (x - ax);
                    const float* kRow = k.data.data() + static_cast<std::size_t>(ky) * k.w;
                    for (int kx = 0; kx < k.w; ++kx) {
                        acc += srcRow[kx] * kRow[kx];
                    }
                }
            } else {
                for (int ky = 0; ky < k.h; ++ky) {
                    const float* kRow = k.data.data() + static_cast<std::size_t>(ky) * k.w;
                    for (int kx = 0; kx < k.w; ++kx) {
                        acc += src.clamped(x + kx - ax, y + ky - ay) * kRow[kx];
                    }
                }
            }
            dstRow[x] = acc * invDiv + k.bias;
        }
    }
    return out;
}

FloatImage convolve(const Image& src, const Kernel& k) {
    return convolve(toFloat(src), k);
}

FloatImage magnitude(const FloatImage& a, const FloatImage& b) {
    if (a.width() != b.width() || a.height() != b.height()) {
        throw std::invalid_argument("magnitude: dimension mismatch");
    }
    FloatImage out(a.width(), a.height());
    const float* pa = a.data();
    const float* pb = b.data();
    float* dst = out.data();
    for (std::size_t i = 0; i < a.size(); ++i) {
        dst[i] = std::sqrt(pa[i] * pa[i] + pb[i] * pb[i]);
    }
    return out;
}

}
