#include "cli.hpp"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "convolution.hpp"
#include "pipeline.hpp"

namespace v2d {
namespace {

constexpr const char* kVersion = "0.1.0";

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error(msg);
}

long long parseInt(const std::string& flag, const std::string& text) {
    try {
        std::size_t used = 0;
        const long long v = std::stoll(text, &used);
        if (used != text.size()) {
            fail(flag + ": '" + text + "' is not an integer");
        }
        return v;
    } catch (const std::invalid_argument&) {
        fail(flag + ": '" + text + "' is not an integer");
    } catch (const std::out_of_range&) {
        fail(flag + ": '" + text + "' is out of range");
    }
}

double parseDouble(const std::string& flag, const std::string& text) {
    try {
        std::size_t used = 0;
        const double v = std::stod(text, &used);
        if (used != text.size()) {
            fail(flag + ": '" + text + "' is not a number");
        }
        return v;
    } catch (const std::invalid_argument&) {
        fail(flag + ": '" + text + "' is not a number");
    } catch (const std::out_of_range&) {
        fail(flag + ": '" + text + "' is out of range");
    }
}

std::string chainFromKernel(const std::string& name) {
    const auto ops = parseChain(name);
    for (const ChainOp& op : ops) {
        if (op.kind == ChainOp::Kind::Gradient || op.label == "laplacian" ||
            op.label == "emboss") {
            return name + ",normalize";
        }
    }
    return name;
}

}

void printVersion() {
    std::cout << "vid2desmos " << kVersion << '\n';
}

void printHelp(const char* programName) {
    std::cout <<
        R"(vid2desmos - turn a video into a filled-square animation for Desmos.

USAGE
  )" << programName << R"( <input> [options]

  Decodes <input> with FFmpeg, reduces each frame to a black/white grid, packs
  the white areas into merged rectangles, and writes Desmos expressions that
  animate over a frame slider.

OUTPUT RESOLUTION
  --width <n>            Output grid width in cells.
  --height <n>           Output grid height in cells.
                         Supplying one preserves the source aspect ratio.
                         Default: --width 160.

TIMING
  --fps <n>              Frames per second to sample. Default: source fps.
  --max-frames <n>       Stop after n processed frames. Default: all.

IMAGE PROCESSING
  --kernel <name>        Shorthand for a one-step chain. Gradient kernels get a
                         normalise step appended automatically.
  --chain <spec>         Comma-separated processing chain, applied in order,
                         e.g. "gaussian3,sobel,normalize".
                         Steps: sobel, prewitt, scharr, invert, normalize, abs,
                                gamma:<x>, contrast:<mul>[/<offset>],
                                gaussian<N>, blur<N>, box<N>, sobelx, sobely,
                                prewittx, prewitty, scharrx, scharry,
                                laplacian, sharpen, emboss, identity
                         Default: none (straight grayscale).

THRESHOLD
  --threshold <n>        Fixed cut-off, 0-255. Default: 128.
  --threshold-mode <m>   fixed | otsu | adaptive | dither | bayer
                         Default: fixed.
  --block <n>            Adaptive window size, odd. Default: 15.
  --bias <n>             Adaptive offset subtracted from the local mean. Default: 5.
  --invert               Swap which side of the cut-off becomes white.

DESMOS OUTPUT
  --output <dir>         Output directory. Default: output/
  --name <base>          Base filename for the parts. Default: frames
  --encode <strategy>    pixels | runs | rects. Default: rects.
  --dark-ink             Draw black regions on a white backdrop
                         (default is white regions on black).
  --max-list <n>         Elements per emitted list before splitting with join().
                         Default: 10000.
  --part-budget <n>      Max packed regions per output part. A longer video is
                         split into several self-contained graphs.
                         Default: 40000.
  --no-json              Skip the Desmos graph-state .json files.

PREVIEW VIDEO
  --preview <file>       Also encode the result as a video. The container and
                         codec follow the extension (.mp4, .gif, .webm, .mkv...).
                         Frames are rendered from the encoded rectangles, so
                         this shows exactly what Desmos will draw.
  --preview-scale <n>    Integer magnification, nearest-neighbour, so each grid
                         cell becomes a crisp n x n block.
                         Default: chosen to land near 720px wide.

DEBUG
  --debug-frames <n>     Write the first n processed frames as PNGs.
  --debug-dir <dir>      Where to put them. Default: <output>/debug
  --verbose              Print the FFmpeg command lines and per-part detail.
  --quiet                Suppress the progress bar.

TOOLS
  --ffmpeg <path>        Path to the ffmpeg binary. Default: ffmpeg (from PATH).
  --ffprobe <path>       Path to the ffprobe binary. Default: ffprobe.

  -h, --help             Show this help.
  -V, --version          Show the version.

EXAMPLES
  )" << programName << R"( clip.mp4 --width 160 --fps 15
  )" << programName << R"( clip.mp4 --width 128 --fps 12 --chain gaussian3,sobel,normalize \
      --threshold-mode otsu --output out/
  )" << programName << R"( clip.mp4 --width 96 --threshold-mode bayer --debug-frames 5
  )" << programName << R"( clip.mp4 --width 160 --fps 15 --preview preview.mp4
)";
}

Options parseArgs(int argc, char** argv) {
    Options o;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            o.showHelp = true;
            return o;
        }
        if (arg == "-V" || arg == "--version") {
            o.showVersion = true;
            return o;
        }

        if (arg.rfind("--", 0) != 0) {
            positional.push_back(arg);
            continue;
        }

        std::string key = arg;
        std::string inlineValue;
        bool hasInline = false;
        const std::size_t eq = arg.find('=');
        if (eq != std::string::npos) {
            key = arg.substr(0, eq);
            inlineValue = arg.substr(eq + 1);
            hasInline = true;
        }

        auto needValue = [&]() -> std::string {
            if (hasInline) return inlineValue;
            if (i + 1 >= argc) fail(key + " requires a value");
            return argv[++i];
        };

        if (key == "--width") {
            o.width = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--height") {
            o.height = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--fps") {
            o.fps = parseDouble(key, needValue());
        } else if (key == "--max-frames") {
            o.maxFrames = parseInt(key, needValue());
        } else if (key == "--kernel") {
            o.kernelName = needValue();
        } else if (key == "--chain") {
            o.chainSpec = needValue();
        } else if (key == "--threshold") {
            o.threshold.value = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--threshold-mode") {
            const std::string v = needValue();
            const auto mode = thresholdModeByName(v);
            if (!mode) fail("--threshold-mode: unknown mode '" + v + "'");
            o.threshold.mode = *mode;
        } else if (key == "--block") {
            o.threshold.blockSize = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--bias") {
            o.threshold.bias = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--invert") {
            o.threshold.invert = true;
        } else if (key == "--output" || key == "-o") {
            o.outDir = needValue();
        } else if (key == "--name") {
            o.baseName = needValue();
        } else if (key == "--encode") {
            const std::string v = needValue();
            const auto s = encodeStrategyByName(v);
            if (!s) fail("--encode: unknown strategy '" + v + "'");
            o.strategy = *s;
        } else if (key == "--dark-ink") {
            o.darkInk = true;
        } else if (key == "--max-list") {
            o.maxList = static_cast<std::size_t>(parseInt(key, needValue()));
        } else if (key == "--part-budget") {
            o.partBudget = static_cast<std::size_t>(parseInt(key, needValue()));
        } else if (key == "--no-json") {
            o.writeJson = false;
        } else if (key == "--preview") {
            o.previewPath = needValue();
        } else if (key == "--preview-scale") {
            o.previewScale = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--debug-frames") {
            o.debugFrames = static_cast<int>(parseInt(key, needValue()));
        } else if (key == "--debug-dir") {
            o.debugDir = needValue();
        } else if (key == "--verbose") {
            o.verbose = true;
        } else if (key == "--quiet") {
            o.quiet = true;
        } else if (key == "--ffmpeg") {
            o.tools.ffmpeg = needValue();
        } else if (key == "--ffprobe") {
            o.tools.ffprobe = needValue();
        } else {
            fail("Unknown option '" + key + "'. Run --help for usage.");
        }
    }

    if (positional.empty()) {
        fail("No input file given. Run --help for usage.");
    }
    if (positional.size() > 1) {
        fail("Expected a single input file, got " + std::to_string(positional.size()) +
             ". Quote paths containing spaces.");
    }
    o.input = positional.front();

    if (o.width < 0 || o.height < 0) {
        fail("--width / --height must be positive");
    }
    if (o.width == 0 && o.height == 0) {
        o.width = 160;
    }
    if (o.fps < 0.0) {
        fail("--fps must be positive");
    }
    if (o.maxFrames < 0) {
        fail("--max-frames must be positive");
    }
    if (o.threshold.value < 0 || o.threshold.value > 255) {
        fail("--threshold must be between 0 and 255");
    }
    if (o.maxList < 1) {
        fail("--max-list must be at least 1");
    }
    if (o.partBudget < 1) {
        fail("--part-budget must be at least 1");
    }
    if (o.debugFrames < 0) {
        fail("--debug-frames must be positive");
    }
    if (o.previewScale < 0) {
        fail("--preview-scale must be positive");
    }
    if (o.previewScale > 0 && o.previewPath.empty()) {
        fail("--preview-scale has no effect without --preview");
    }

    if (o.chainSpec.empty() && !o.kernelName.empty()) {
        o.chainSpec = chainFromKernel(o.kernelName);
    }
    if (o.debugDir.empty()) {
        o.debugDir = o.outDir + "/debug";
    }

    return o;
}

}
