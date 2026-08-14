#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame.hpp"

namespace v2d {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class EncodeStrategy {
    Pixels,
    Runs,
    Rects,
};

std::optional<EncodeStrategy> encodeStrategyByName(std::string_view name);
std::string encodeStrategyName(EncodeStrategy s);

std::vector<Rect> encodeFrame(const BinaryImage& mask, EncodeStrategy strategy);

BinaryImage renderRegions(const std::vector<Rect>& regions, int w, int h);

double packRect(const Rect& r, int gridW, int gridH);
Rect unpackRect(double packed, int gridW, int gridH);

struct DesmosOptions {
    int gridW = 160;
    int gridH = 90;
    double fps = 15.0;
    std::string outDir = "output";
    std::string baseName = "frames";

    std::size_t maxListLen = 10000;

    std::size_t partBudget = 40000;

    bool whiteOnBlack = true;
    bool writeJson = true;
};

struct PartInfo {
    int index = 1;
    int firstFrame = 0;
    int frameCount = 0;
    std::size_t regionCount = 0;
    std::string txtPath;
    std::string jsonPath;
};

struct DesmosSummary {
    int frames = 0;
    std::size_t regions = 0;
    std::size_t largestFrameRegions = 0;
    std::vector<PartInfo> parts;
    std::uintmax_t bytesWritten = 0;
    std::string manifestPath;
};

class DesmosWriter {
public:
    explicit DesmosWriter(DesmosOptions options);

    void addFrame(const std::vector<Rect>& regions);

    DesmosSummary finish();

private:

    struct Part {
        std::vector<double> data;
        std::vector<double> offsets{1.0};
        int frames = 0;
        int firstFrame = 0;
    };

    void rotatePart();
    void writePart(const Part& part);
    static void mergeInto(Part& dst, const Part& src);

    DesmosOptions opt_;
    Part current_;

    std::optional<Part> held_;
    int nextPartIndex_ = 1;
    bool finished_ = false;
    DesmosSummary summary_;
};

}
