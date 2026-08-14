#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "desmos.hpp"
#include "threshold.hpp"
#include "video.hpp"

namespace v2d {

struct Options {
    std::string input;
    std::string outDir = "output";
    std::string baseName = "frames";

    int width = 0;
    int height = 0;
    double fps = 0.0;
    long long maxFrames = 0;

    std::string chainSpec;
    std::string kernelName;

    ThresholdOptions threshold;
    EncodeStrategy strategy = EncodeStrategy::Rects;

    bool darkInk = false;
    std::size_t maxList = 10000;
    std::size_t partBudget = 40000;

    int debugFrames = 0;
    std::string debugDir;

    std::string previewPath;
    int previewScale = 0;

    bool writeJson = true;
    bool verbose = false;
    bool quiet = false;

    ToolPaths tools;

    bool showHelp = false;
    bool showVersion = false;
};

Options parseArgs(int argc, char** argv);

void printHelp(const char* programName);
void printVersion();

}
