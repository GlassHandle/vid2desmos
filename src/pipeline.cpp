#include "pipeline.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <stdexcept>

#include "filter.hpp"

namespace v2d {
namespace {

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(const std::string& s) {
    std::string out = s;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

float parseFloat(const std::string& token, const std::string& text) {
    try {
        return std::stof(text);
    } catch (const std::exception&) {
        throw std::runtime_error("chain step '" + token + "': '" + text +
                                 "' is not a number");
    }
}

ChainOp makeGradient(Kernel kx, Kernel ky, std::string label) {
    ChainOp op;
    op.kind = ChainOp::Kind::Gradient;
    op.kernel = std::move(kx);
    op.kernelY = std::move(ky);
    op.label = std::move(label);
    return op;
}

}

std::vector<ChainOp> parseChain(const std::string& spec) {
    std::vector<ChainOp> ops;
    std::istringstream stream(spec);
    std::string token;

    while (std::getline(stream, token, ',')) {
        token = trim(token);
        if (token.empty()) {
            continue;
        }

        std::string name = token;
        std::string arg;
        const std::size_t colon = token.find(':');
        if (colon != std::string::npos) {
            name = trim(token.substr(0, colon));
            arg = trim(token.substr(colon + 1));
        }
        name = lower(name);

        if (name == "none" || name == "identity") {
            continue;
        }
        if (name == "sobel" || name == "edge") {
            ops.push_back(makeGradient(kernels::sobelX(), kernels::sobelY(), "sobel"));
            continue;
        }
        if (name == "prewitt") {
            ops.push_back(makeGradient(kernels::prewittX(), kernels::prewittY(), "prewitt"));
            continue;
        }
        if (name == "scharr") {
            ops.push_back(makeGradient(kernels::scharrX(), kernels::scharrY(), "scharr"));
            continue;
        }
        if (name == "invert") {
            ChainOp op;
            op.kind = ChainOp::Kind::Invert;
            op.label = "invert";
            ops.push_back(std::move(op));
            continue;
        }
        if (name == "normalize" || name == "norm") {
            ChainOp op;
            op.kind = ChainOp::Kind::Normalize;
            op.label = "normalize";
            ops.push_back(std::move(op));
            continue;
        }
        if (name == "abs") {
            ChainOp op;
            op.kind = ChainOp::Kind::Abs;
            op.label = "abs";
            ops.push_back(std::move(op));
            continue;
        }
        if (name == "gamma") {
            ChainOp op;
            op.kind = ChainOp::Kind::Gamma;
            op.arg0 = arg.empty() ? 1.0f : parseFloat(token, arg);
            if (op.arg0 <= 0.0f) {
                throw std::runtime_error("chain step '" + token + "': gamma must be positive");
            }
            op.label = "gamma:" + std::to_string(op.arg0);
            ops.push_back(std::move(op));
            continue;
        }
        if (name == "contrast") {
            ChainOp op;
            op.kind = ChainOp::Kind::Contrast;
            op.arg0 = 1.0f;
            op.arg1 = 0.0f;
            if (!arg.empty()) {
                const std::size_t slash = arg.find('/');
                if (slash == std::string::npos) {
                    op.arg0 = parseFloat(token, arg);
                } else {
                    op.arg0 = parseFloat(token, trim(arg.substr(0, slash)));
                    op.arg1 = parseFloat(token, trim(arg.substr(slash + 1)));
                }
            }
            op.label = "contrast:" + std::to_string(op.arg0) + "/" + std::to_string(op.arg1);
            ops.push_back(std::move(op));
            continue;
        }

        if (auto k = kernelByName(name)) {
            ChainOp op;
            op.kind = ChainOp::Kind::Kernel;
            op.label = k->name;
            op.kernel = std::move(*k);
            ops.push_back(std::move(op));
            continue;
        }

        throw std::runtime_error("Unknown chain step '" + token +
                                 "'. Run --help for the list of accepted steps.");
    }

    return ops;
}

std::string describeChain(const std::vector<ChainOp>& ops) {
    if (ops.empty()) {
        return "none";
    }
    std::string out;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        if (i != 0) out += " -> ";
        out += ops[i].label;
    }
    return out;
}

std::vector<std::string> chainOpNames() {
    std::vector<std::string> names = {"sobel", "prewitt",  "scharr", "invert", "normalize",
                                      "abs",   "gamma:<x>", "contrast:<mul>[/<offset>]"};
    for (const std::string& k : kernelNames()) {
        names.push_back(k);
    }
    return names;
}

Image runChain(const Image& src, const std::vector<ChainOp>& ops) {
    if (ops.empty()) {
        return src;
    }

    FloatImage buf = toFloat(src);

    for (const ChainOp& op : ops) {
        switch (op.kind) {
            case ChainOp::Kind::Kernel:
                buf = convolve(buf, op.kernel);
                break;
            case ChainOp::Kind::Gradient: {
                FloatImage gx = convolve(buf, op.kernel);
                FloatImage gy = convolve(buf, op.kernelY);
                buf = magnitude(gx, gy);
                break;
            }
            case ChainOp::Kind::Invert:
                invert(buf);
                break;
            case ChainOp::Kind::Abs: {
                float* p = buf.data();
                for (std::size_t i = 0; i < buf.size(); ++i) {
                    p[i] = std::fabs(p[i]);
                }
                break;
            }
            case ChainOp::Kind::Normalize: {
                buf = toFloat(toImage(buf, true));
                break;
            }
            case ChainOp::Kind::Gamma: {
                Image tmp = toImage(buf, false);
                applyGamma(tmp, op.arg0);
                buf = toFloat(tmp);
                break;
            }
            case ChainOp::Kind::Contrast: {
                Image tmp = toImage(buf, false);
                applyContrast(tmp, op.arg0, op.arg1);
                buf = toFloat(tmp);
                break;
            }
        }
    }

    return toImage(buf, false);
}

}
