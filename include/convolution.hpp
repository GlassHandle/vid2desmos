#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame.hpp"

namespace v2d {

struct Kernel {
    int w = 1;
    int h = 1;
    std::vector<float> data{1.0f};

    float divisor = 1.0f;
    float bias = 0.0f;
    std::string name = "identity";

    float at(int x, int y) const { return data[static_cast<std::size_t>(y) * w + x]; }
};

namespace kernels {

Kernel identity();
Kernel box(int n);

Kernel gaussian(int n, float sigma = 0.0f);
Kernel sobelX();
Kernel sobelY();
Kernel prewittX();
Kernel prewittY();
Kernel scharrX();
Kernel scharrY();
Kernel laplacian();
Kernel sharpen();
Kernel emboss();

}

std::optional<Kernel> kernelByName(std::string_view name);

std::vector<std::string> kernelNames();

FloatImage convolve(const FloatImage& src, const Kernel& k);
FloatImage convolve(const Image& src, const Kernel& k);

FloatImage magnitude(const FloatImage& a, const FloatImage& b);

}
