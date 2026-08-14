#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "frame.hpp"

namespace v2d {

enum class ThresholdMode {
    Fixed,
    Otsu,
    Adaptive,
    Dither,
    Bayer,

};

struct ThresholdOptions {
    ThresholdMode mode = ThresholdMode::Fixed;
    int value = 128;
    int blockSize = 15;
    int bias = 5;
    bool invert = false;
};

std::optional<ThresholdMode> thresholdModeByName(std::string_view name);
std::string thresholdModeName(ThresholdMode mode);

int otsuThreshold(const Image& src);

BinaryImage threshold(const Image& src, const ThresholdOptions& opts);

}
