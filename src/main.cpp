#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "desmos.hpp"
#include "filter.hpp"
#include "frame.hpp"
#include "pipeline.hpp"
#include "png.hpp"
#include "threshold.hpp"
#include "video.hpp"

namespace {

namespace fs = std::filesystem;
using namespace v2d;

std::string humanBytes(std::uintmax_t n) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(n);
    int u = 0;
    while (v >= 1024.0 && u < 3) {
        v /= 1024.0;
        ++u;
    }
    std::ostringstream s;
    s << std::fixed << std::setprecision(v < 10.0 && u > 0 ? 1 : 0) << v << ' ' << units[u];
    return s.str();
}

void resolveOutputSize(const VideoInfo& info, Options& opt) {
    const double aspect = static_cast<double>(info.width) / static_cast<double>(info.height);
    if (opt.width > 0 && opt.height == 0) {
        opt.height = std::max(1, static_cast<int>(std::lround(opt.width / aspect)));
    } else if (opt.height > 0 && opt.width == 0) {
        opt.width = std::max(1, static_cast<int>(std::lround(opt.height * aspect)));
    }
}

void drawProgress(long long done, long long total) {
    constexpr int kBarWidth = 20;
    const double frac = (total > 0) ? std::min(1.0, static_cast<double>(done) / total) : 0.0;
    const int filled = static_cast<int>(frac * kBarWidth);

    std::string bar;
    bar.reserve(kBarWidth * 3);
    for (int i = 0; i < kBarWidth; ++i) {
        bar += (i < filled) ? "#" : "-";
    }

    std::fprintf(stderr, "\r[%s] %3d%%  frame %lld", bar.c_str(),
                 static_cast<int>(frac * 100.0 + 0.5), done);
    if (total > 0) {
        std::fprintf(stderr, "/%lld", total);
    }
    std::fprintf(stderr, "   ");
    std::fflush(stderr);
}

int run(int argc, char** argv) {
    Options opt = parseArgs(argc, argv);
    if (opt.showHelp) {
        printHelp(argc > 0 ? argv[0] : "vid2desmos");
        return 0;
    }
    if (opt.showVersion) {
        printVersion();
        return 0;
    }

    if (!fs::exists(opt.input)) {
        throw std::runtime_error("Input file not found: '" + opt.input + "'");
    }

    const VideoInfo info = probeVideo(opt.input, opt.tools);
    resolveOutputSize(info, opt);

    const double effectiveFps = (opt.fps > 0.0) ? opt.fps : info.fps;
    long long expectedFrames = 0;
    if (info.duration > 0.0 && effectiveFps > 0.0) {
        expectedFrames = static_cast<long long>(info.duration * effectiveFps + 0.5);
    } else if (info.frames > 0 && info.fps > 0.0 && effectiveFps > 0.0) {
        expectedFrames = static_cast<long long>(info.frames * (effectiveFps / info.fps) + 0.5);
    }
    if (opt.maxFrames > 0) {
        expectedFrames = (expectedFrames > 0) ? std::min(expectedFrames, opt.maxFrames)
                                              : opt.maxFrames;
    }

    const std::vector<ChainOp> chain = parseChain(opt.chainSpec);

    std::cout << "Input: " << info.path << '\n';
    std::cout << "Resolution: " << info.width << 'x' << info.height;
    if (!info.codec.empty()) std::cout << " (" << info.codec << ')';
    std::cout << '\n';
    std::cout << "FPS: " << std::fixed << std::setprecision(2) << info.fps << '\n';
    if (info.duration > 0.0) {
        std::cout << "Duration: " << std::fixed << std::setprecision(1) << info.duration << "s\n";
    }
    std::cout << '\n';
    std::cout << "Output resolution: " << opt.width << 'x' << opt.height << '\n';
    std::cout << "Processing FPS: " << std::fixed << std::setprecision(2) << effectiveFps << '\n';
    std::cout << "Chain: " << describeChain(chain) << '\n';
    std::cout << "Threshold: " << thresholdModeName(opt.threshold.mode);
    if (opt.threshold.mode == ThresholdMode::Fixed) {
        std::cout << " @ " << opt.threshold.value;
    } else if (opt.threshold.mode == ThresholdMode::Adaptive) {
        std::cout << " block " << opt.threshold.blockSize << ", bias " << opt.threshold.bias;
    }
    if (opt.threshold.invert) std::cout << " (inverted)";
    std::cout << '\n';
    std::cout << "Encoding: " << encodeStrategyName(opt.strategy) << '\n';
    if (expectedFrames > 0) {
        std::cout << "Frames: ~" << expectedFrames << '\n';
    }
    std::cout << '\n';

    std::cout.flush();

    VideoReader reader(opt.input, opt.width, opt.height, opt.fps, opt.tools);
    if (opt.verbose) {
        std::cout << "ffmpeg: " << reader.command() << "\n\n";
    }

    DesmosOptions dopt;
    dopt.gridW = opt.width;
    dopt.gridH = opt.height;
    dopt.fps = effectiveFps;
    dopt.outDir = opt.outDir;
    dopt.baseName = opt.baseName;
    dopt.maxListLen = opt.maxList;
    dopt.partBudget = opt.partBudget;
    dopt.whiteOnBlack = !opt.darkInk;
    dopt.writeJson = opt.writeJson;
    DesmosWriter writer(dopt);

    if (opt.debugFrames > 0) {
        fs::create_directories(fs::path(opt.debugDir));
    }

    std::unique_ptr<VideoWriter> preview;
    if (!opt.previewPath.empty()) {
        const fs::path parent = fs::path(opt.previewPath).parent_path();
        if (!parent.empty()) {
            fs::create_directories(parent);
        }
        preview = std::make_unique<VideoWriter>(opt.previewPath, opt.width, opt.height,
                                                effectiveFps, opt.previewScale, opt.tools);
        std::cout << "Preview: " << opt.previewPath << " at " << preview->outWidth() << 'x'
                  << preview->outHeight() << " (" << preview->scale() << "x)\n";
        if (opt.verbose) {
            std::cout << "ffmpeg: " << preview->command() << '\n';
        }
        std::cout << '\n';
    }

    std::cout << "Processing:\n" << std::flush;

    Image raw;
    std::vector<Rect> regions;
    long long frameIndex = 0;
    std::size_t totalWhite = 0;
    const auto tStart = std::chrono::steady_clock::now();

    while (reader.readFrame(raw)) {
        Image processed = runChain(raw, chain);
        BinaryImage mask = threshold(processed, opt.threshold);

        totalWhite += countSet(mask);
        regions = encodeFrame(mask, opt.strategy);
        writer.addFrame(regions);

        if (preview) {

            const BinaryImage drawn = renderRegions(regions, opt.width, opt.height);
            Image frame = binaryToImage(drawn);
            if (opt.darkInk) {
                invert(frame);
            }
            preview->writeFrame(frame);
        }

        if (frameIndex < opt.debugFrames) {
            char name[64];
            std::snprintf(name, sizeof(name), "frame_%04lld", frameIndex);
            const fs::path dir(opt.debugDir);
            writeGrayPng((dir / (std::string(name) + "_gray.png")).string(), processed);
            writeGrayPng((dir / (std::string(name) + "_bw.png")).string(), binaryToImage(mask));
        }

        ++frameIndex;
        if (!opt.quiet && (frameIndex % 8 == 0 || frameIndex == expectedFrames)) {
            drawProgress(frameIndex, expectedFrames);
        }
        if (opt.maxFrames > 0 && frameIndex >= opt.maxFrames) {
            break;
        }
    }

    if (!opt.quiet) {

        drawProgress(frameIndex, frameIndex);
        std::fprintf(stderr, "\n");
    }

    if (frameIndex == 0) {
        throw std::runtime_error(
            "No frames were decoded. FFmpeg may have failed to open the input; "
            "rerun with --verbose and check its error output.");
    }

    if (preview) {
        preview->close();
    }

    const DesmosSummary summary = writer.finish();
    const auto elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - tStart)
                             .count();

    const double avgWhite = static_cast<double>(totalWhite) / frameIndex;
    const double avgRegions = static_cast<double>(summary.regions) / frameIndex;
    const double compression = (summary.regions > 0)
                                   ? static_cast<double>(totalWhite) / summary.regions
                                   : 0.0;

    std::cout << "\nGenerated:\n";
    std::cout << "  Frames: " << summary.frames << '\n';
    std::cout << "  Average white pixels: " << std::fixed << std::setprecision(0) << avgWhite
              << " / " << (opt.width * opt.height) << '\n';
    std::cout << "  Average Desmos regions: " << std::fixed << std::setprecision(0) << avgRegions
              << "  (" << std::setprecision(2) << compression << "x fewer than raw pixels)\n";
    std::cout << "  Largest frame: " << summary.largestFrameRegions << " regions\n";
    std::cout << "  Parts: " << summary.parts.size() << '\n';
    std::cout << "  Output size: " << humanBytes(summary.bytesWritten) << '\n';
    std::cout << "  Elapsed: " << std::fixed << std::setprecision(1) << elapsed << "s ("
              << std::setprecision(1) << (frameIndex / std::max(elapsed, 1e-6)) << " fps)\n";

    if (opt.verbose) {
        for (const PartInfo& p : summary.parts) {
            std::cout << "    part " << p.index << ": frames " << p.firstFrame << ".."
                      << (p.firstFrame + p.frameCount - 1) << ", " << p.regionCount
                      << " regions -> " << p.txtPath << '\n';
        }
    }

    std::cout << "\nFiles:\n";
    for (const PartInfo& p : summary.parts) {
        std::cout << "  " << p.txtPath << '\n';
    }
    std::cout << "  " << summary.manifestPath << '\n';
    if (preview) {
        std::cout << "  " << opt.previewPath << "   (" << preview->framesWritten()
                  << " frames, " << humanBytes(fs::file_size(opt.previewPath)) << ")\n";
    }

    if (summary.largestFrameRegions > 10000) {
        std::cout << "\nWarning: the busiest frame has " << summary.largestFrameRegions
                  << " regions. Desmos caps generated list ranges at 10000 elements, so that\n"
                     "frame will not render. Lower the resolution, blur before thresholding,\n"
                     "or use --encode rects.\n";
    }

    const int firstPartFrames =
        summary.parts.empty() ? summary.frames : summary.parts.front().frameCount;

    std::cout << "\nTo use in Desmos:\n"
                 "  1. Open a blank graph at desmos.com/calculator\n"
                 "  2. Open " << (summary.parts.empty() ? std::string("the part file")
                                                        : summary.parts.front().txtPath)
              << ",\n"
                 "     select all, copy, and paste into the first expression box.\n"
                 "     Desmos splits the pasted lines into separate expressions.\n"
                 "  3. Colour the last two expressions: the first polygon is the\n"
                 "     backdrop (set it "
              << (opt.darkInk ? "white" : "black")
              << "), the second is the picture (set it "
              << (opt.darkInk ? "black" : "white") << ").\n"
                 "     Click the coloured circle next to each to change it.\n"
                 "  4. Press play on the `f` slider. For an exact loop, click the\n"
                 "     slider and set min 0, max " << (firstPartFrames - 1)
              << ", step 1.\n";
    if (summary.parts.size() > 1) {
        std::cout << "  Note: this run produced " << summary.parts.size()
                  << " parts. Each is a separate graph; paste them one at a time.\n";
    }
    std::cout << "\nTo preview without Desmos:\n"
                 "  python loader/serve.py " << opt.outDir << "   (browser)\n"
                 "  python loader/view.py  " << opt.outDir << "   (matplotlib)\n";

    return 0;
}

}

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "\nvid2desmos: " << e.what() << '\n';
        return 1;
    }
}
