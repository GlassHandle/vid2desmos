#pragma once

#include <string>
#include <vector>

#include "convolution.hpp"
#include "frame.hpp"

namespace v2d {

struct ChainOp {
    enum class Kind {
        Kernel,
        Gradient,
        Invert,
        Normalize,
        Abs,
        Gamma,
        Contrast,
    };

    Kind kind = Kind::Kernel;
    Kernel kernel;
    Kernel kernelY;
    float arg0 = 0.0f;
    float arg1 = 0.0f;
    std::string label;
};

std::vector<ChainOp> parseChain(const std::string& spec);

std::string describeChain(const std::vector<ChainOp>& ops);

std::vector<std::string> chainOpNames();

Image runChain(const Image& src, const std::vector<ChainOp>& ops);

}
