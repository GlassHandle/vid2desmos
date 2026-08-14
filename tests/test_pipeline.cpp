#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "convolution.hpp"
#include "desmos.hpp"
#include "filter.hpp"
#include "frame.hpp"
#include "pipeline.hpp"
#include "png.hpp"
#include "threshold.hpp"

namespace fs = std::filesystem;
using namespace v2d;

namespace {

int gFailures = 0;
int gChecks = 0;

void check(bool condition, const std::string& what) {
    ++gChecks;
    if (!condition) {
        ++gFailures;
        std::cout << "  FAIL: " << what << '\n';
    }
}

void checkNear(double a, double b, double tol, const std::string& what) {
    check(std::fabs(a - b) <= tol,
          what + " (got " + std::to_string(a) + ", want " + std::to_string(b) + ")");
}

BinaryImage rasterize(const std::vector<Rect>& rects, int w, int h) {
    BinaryImage out(w, h);
    for (const Rect& r : rects) {
        for (int dy = 0; dy < r.h; ++dy) {
            const int row = h - 1 - (r.y + dy);
            if (row < 0 || row >= h) continue;
            for (int dx = 0; dx < r.w; ++dx) {
                const int x = r.x + dx;
                if (x < 0 || x >= w) continue;
                out.at(x, row) = 1;
            }
        }
    }
    return out;
}

bool masksEqual(const BinaryImage& a, const BinaryImage& b) {
    if (a.width() != b.width() || a.height() != b.height()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if ((a.data()[i] != 0) != (b.data()[i] != 0)) return false;
    }
    return true;
}

void testConvolution() {
    std::cout << "convolution\n";

    Image img(5, 5);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            img.at(x, y) = static_cast<std::uint8_t>(x * 10 + y);
        }
    }

    const FloatImage same = convolve(img, kernels::identity());
    bool identityHolds = true;
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (std::fabs(same.at(x, y) - img.at(x, y)) > 1e-4) identityHolds = false;
        }
    }
    check(identityHolds, "identity kernel is a no-op");

    Image flat(7, 7);
    for (std::size_t i = 0; i < flat.size(); ++i) flat.data()[i] = 100;
    const FloatImage blurred = convolve(flat, kernels::gaussian(5));
    bool flatHolds = true;
    for (std::size_t i = 0; i < blurred.size(); ++i) {
        if (std::fabs(blurred.data()[i] - 100.0f) > 0.01f) flatHolds = false;
    }
    check(flatHolds, "gaussian preserves a uniform field at the borders");

    Image edge(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            edge.at(x, y) = static_cast<std::uint8_t>(x < 4 ? 0 : 255);
        }
    }
    const FloatImage gx = convolve(edge, kernels::sobelX());
    const FloatImage gy = convolve(edge, kernels::sobelY());
    check(std::fabs(gx.at(3, 4)) > 500.0f, "sobelX responds to a vertical edge");
    checkNear(gy.at(3, 4), 0.0, 1e-3, "sobelY ignores a vertical edge");

    check(kernelByName("gaussian5").has_value(), "kernelByName parses a sized name");
    check(!kernelByName("nonsense").has_value(), "kernelByName rejects unknown names");
}

void testThreshold() {
    std::cout << "threshold\n";

    Image img(4, 4);
    for (int i = 0; i < 16; ++i) {
        img.data()[i] = static_cast<std::uint8_t>(i * 16);
    }

    ThresholdOptions fixed;
    fixed.mode = ThresholdMode::Fixed;
    fixed.value = 128;
    const BinaryImage mask = threshold(img, fixed);
    check(mask.data()[0] == 0, "fixed threshold: dark pixel is 0");
    check(mask.data()[15] == 1, "fixed threshold: bright pixel is 1");

    fixed.invert = true;
    const BinaryImage inverted = threshold(img, fixed);
    check(inverted.data()[0] == 1 && inverted.data()[15] == 0, "--invert swaps the mask");

    Image bimodal(10, 10);
    for (std::size_t i = 0; i < bimodal.size(); ++i) {
        bimodal.data()[i] = static_cast<std::uint8_t>(i < bimodal.size() / 2 ? 20 : 230);
    }
    const int t = otsuThreshold(bimodal);
    check(t > 20 && t < 230, "otsu separates a bimodal histogram");

    for (ThresholdMode mode : {ThresholdMode::Fixed, ThresholdMode::Otsu,
                               ThresholdMode::Adaptive, ThresholdMode::Dither,
                               ThresholdMode::Bayer}) {
        ThresholdOptions o;
        o.mode = mode;
        const BinaryImage m = threshold(img, o);
        bool binary = true;
        for (std::size_t i = 0; i < m.size(); ++i) {
            if (m.data()[i] > 1) binary = false;
        }
        check(binary, "mode '" + thresholdModeName(mode) + "' emits only 0 and 1");
    }
}

void testFilters() {
    std::cout << "filters\n";

    Image img(4, 4);
    for (std::size_t i = 0; i < img.size(); ++i) img.data()[i] = 10;
    invert(img);
    check(img.data()[0] == 245, "invert");

    Image ramp(4, 4);
    for (int i = 0; i < 16; ++i) ramp.data()[i] = static_cast<std::uint8_t>(100 + i);
    normalize(ramp);
    check(ramp.data()[0] == 0 && ramp.data()[15] == 255, "normalize stretches to full range");

    Image big(4, 4);
    for (std::size_t i = 0; i < big.size(); ++i) big.data()[i] = 80;
    const Image small = resizeArea(big, 2, 2);
    check(small.width() == 2 && small.height() == 2, "resizeArea produces the requested size");
    check(small.at(0, 0) == 80 && small.at(1, 1) == 80, "resizeArea averages correctly");
}

void testPacking() {
    std::cout << "packing\n";

    const int gw = 160;
    const int gh = 90;
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> dx(0, gw - 1);
    std::uniform_int_distribution<int> dy(0, gh - 1);

    bool allOk = true;
    for (int i = 0; i < 20000; ++i) {
        Rect r;
        r.x = dx(rng);
        r.y = dy(rng);
        r.w = std::uniform_int_distribution<int>(0, gw)(rng);
        r.h = std::uniform_int_distribution<int>(0, gh)(rng);
        const Rect back = unpackRect(packRect(r, gw, gh), gw, gh);
        if (back.x != r.x || back.y != r.y || back.w != r.w || back.h != r.h) {
            allOk = false;
            break;
        }
    }
    check(allOk, "packRect/unpackRect round-trip over the whole grid");

    checkNear(packRect(Rect{0, 0, 0, 0}, gw, gh), 0.0, 0.0, "empty-frame sentinel packs to 0");

    bool threw = false;
    try {
        packRect(Rect{gw, 0, 1, 1}, gw, gh);
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "packRect rejects an out-of-grid region");
}

void testEncoding() {
    std::cout << "encoding\n";

    BinaryImage block(8, 8);
    for (int y = 2; y < 4; ++y) {
        for (int x = 1; x < 5; ++x) {
            block.at(x, y) = 1;
        }
    }

    const auto pixels = encodeFrame(block, EncodeStrategy::Pixels);
    const auto runs = encodeFrame(block, EncodeStrategy::Runs);
    const auto rects = encodeFrame(block, EncodeStrategy::Rects);

    check(pixels.size() == 8, "pixels strategy emits one region per set pixel");
    check(runs.size() == 2, "runs strategy merges each row into one region");
    check(rects.size() == 1, "rects strategy merges identical stacked runs");
    check(rects[0].w == 4 && rects[0].h == 2, "merged region has the right extent");

    std::mt19937 rng(99);
    bool allExact = true;
    std::size_t pixelTotal = 0;
    std::size_t rectTotal = 0;

    for (int trial = 0; trial < 12; ++trial) {
        const int w = 5 + trial * 3;
        const int h = 4 + trial * 2;
        BinaryImage mask(w, h);

        std::uniform_int_distribution<int> pick(0, 99);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool on = ((x / 3 + y / 3) % 2 == 0) ? pick(rng) < 85 : pick(rng) < 15;
                mask.at(x, y) = on ? 1 : 0;
            }
        }

        for (EncodeStrategy s : {EncodeStrategy::Pixels, EncodeStrategy::Runs,
                                 EncodeStrategy::Rects}) {
            const auto regions = encodeFrame(mask, s);
            if (!masksEqual(rasterize(regions, w, h), mask)) {
                allExact = false;
            }
            if (s == EncodeStrategy::Pixels) pixelTotal += regions.size();
            if (s == EncodeStrategy::Rects) rectTotal += regions.size();
        }
    }
    check(allExact, "all strategies reconstruct the source mask exactly");
    check(rectTotal < pixelTotal, "rects strategy emits fewer regions than pixels");

    const BinaryImage empty(8, 8);
    check(encodeFrame(empty, EncodeStrategy::Rects).empty(), "an all-black frame emits nothing");

    bool renderAgrees = true;
    for (EncodeStrategy s : {EncodeStrategy::Pixels, EncodeStrategy::Runs,
                             EncodeStrategy::Rects}) {
        const auto regions = encodeFrame(block, s);
        if (!masksEqual(renderRegions(regions, 8, 8), block)) renderAgrees = false;
        if (!masksEqual(renderRegions(regions, 8, 8), rasterize(regions, 8, 8))) {
            renderAgrees = false;
        }
    }
    check(renderAgrees, "renderRegions reproduces the source frame");
    check(countSet(renderRegions({}, 8, 8)) == 0, "renderRegions of nothing is blank");

    BinaryImage checker(6, 6);
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 6; ++x) {
            checker.at(x, y) = static_cast<std::uint8_t>((x + y) % 2);
        }
    }
    const auto checkerRects = encodeFrame(checker, EncodeStrategy::Rects);
    check(checkerRects.size() == 18, "checkerboard cannot be merged");
    check(masksEqual(rasterize(checkerRects, 6, 6), checker), "checkerboard survives round-trip");
}

void testChainParsing() {
    std::cout << "chain parsing\n";

    const auto ops = parseChain("gaussian3,sobel,normalize,gamma:0.8");
    check(ops.size() == 4, "parseChain reads every step");
    check(ops[1].kind == ChainOp::Kind::Gradient, "sobel becomes a gradient step");
    checkNear(ops[3].arg0, 0.8, 1e-6, "gamma argument is parsed");

    check(parseChain("").empty(), "an empty chain is valid");
    check(parseChain(" identity , none ").empty(), "identity steps are dropped");

    bool threw = false;
    try {
        parseChain("gaussian3,notakernel");
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "parseChain rejects an unknown step");

    Image edge(16, 16);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            edge.at(x, y) = static_cast<std::uint8_t>(x < 8 ? 0 : 255);
        }
    }
    const Image processed = runChain(edge, parseChain("sobel,normalize"));
    check(processed.at(7, 8) > processed.at(1, 8), "sobel chain highlights the edge");
    check(runChain(edge, {}).at(0, 0) == edge.at(0, 0), "an empty chain is a no-op");
}

void testCli() {
    std::cout << "cli\n";

    const char* argv[] = {"vid2desmos", "in.mp4",  "--width", "128",
                          "--fps",      "12",      "--kernel", "sobel",
                          "--threshold-mode", "otsu", "--encode", "runs"};
    Options o = parseArgs(12, const_cast<char**>(argv));
    check(o.input == "in.mp4", "positional input is captured");
    check(o.width == 128, "--width is parsed");
    checkNear(o.fps, 12.0, 1e-9, "--fps is parsed");
    check(o.threshold.mode == ThresholdMode::Otsu, "--threshold-mode is parsed");
    check(o.strategy == EncodeStrategy::Runs, "--encode is parsed");
    check(o.chainSpec == "sobel,normalize", "--kernel sobel appends a normalise step");

    const char* argvEq[] = {"vid2desmos", "in.mp4", "--width=64", "--invert"};
    o = parseArgs(4, const_cast<char**>(argvEq));
    check(o.width == 64, "--flag=value form is accepted");
    check(o.threshold.invert, "--invert is a flag");
    check(o.height == 0, "height is left for the aspect-ratio pass");

    bool threw = false;
    try {
        const char* bad[] = {"vid2desmos", "--nope"};
        parseArgs(2, const_cast<char**>(bad));
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "unknown options are rejected");

    threw = false;
    try {
        const char* bad[] = {"vid2desmos"};
        parseArgs(1, const_cast<char**>(bad));
    } catch (const std::exception&) {
        threw = true;
    }
    check(threw, "a missing input file is rejected");
}

void testOutput() {
    std::cout << "output\n";

    const fs::path dir = fs::temp_directory_path() / "vid2desmos_test";
    fs::remove_all(dir);

    DesmosOptions opt;
    opt.gridW = 16;
    opt.gridH = 8;
    opt.fps = 10.0;
    opt.outDir = dir.string();
    opt.baseName = "t";
    opt.partBudget = 8;
    opt.maxListLen = 3;

    DesmosWriter writer(opt);
    for (int i = 0; i < 12; ++i) {
        BinaryImage mask(16, 8);
        for (int x = 0; x < 4; ++x) {
            mask.at(x + i, 3) = 1;
        }
        writer.addFrame(encodeFrame(mask, EncodeStrategy::Rects));
    }
    writer.addFrame({});
    const DesmosSummary summary = writer.finish();

    check(summary.frames == 13, "every frame is accounted for");
    check(summary.parts.size() > 1, "the part budget splits the output");
    check(fs::exists(summary.manifestPath), "manifest.json is written");

    int frameTotal = 0;
    int expectedFirst = 0;
    bool contiguous = true;
    for (const PartInfo& p : summary.parts) {
        check(fs::exists(p.txtPath), "part .txt exists");
        check(fs::exists(p.jsonPath), "part .json exists");
        if (p.firstFrame != expectedFirst) contiguous = false;
        expectedFirst += p.frameCount;
        frameTotal += p.frameCount;
    }
    check(frameTotal == summary.frames, "parts cover every frame exactly once");
    check(contiguous, "part frame ranges are contiguous and in order");

    std::string text;
    {
        std::ifstream f(summary.parts.front().txtPath);
        text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    check(text.find("polygon(") != std::string::npos, "expressions contain a polygon call");
    check(text.find("join(") != std::string::npos, "long lists are split with join()");
    check(text.find("f=0") != std::string::npos, "a frame slider is emitted");
    check(text.find("G_{w}=16") != std::string::npos, "grid width is emitted");

    Image img(8, 4);
    for (std::size_t i = 0; i < img.size(); ++i) {
        img.data()[i] = static_cast<std::uint8_t>(i * 8);
    }
    const fs::path png = dir / "t.png";
    writeGrayPng(png.string(), img);
    check(fs::exists(png) && fs::file_size(png) > 40, "PNG is written");

    std::ifstream pf(png, std::ios::binary);
    unsigned char sig[8] = {0};
    pf.read(reinterpret_cast<char*>(sig), 8);
    check(sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G',
          "PNG signature is correct");
    pf.close();

    fs::remove_all(dir);
}

std::vector<double> listOf(const std::string& path, const std::string& name) {
    std::ifstream f(path);
    const std::string key = name + "=[";
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) == 0 && !line.empty() && line.back() == ']') {
            const std::string body = line.substr(key.size(), line.size() - key.size() - 1);
            std::vector<double> out;
            std::istringstream ss(body);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                out.push_back(std::stod(tok));
            }
            return out;
        }
    }
    return {};
}

void testPartMerging() {
    std::cout << "part merging\n";

    const fs::path dir = fs::temp_directory_path() / "vid2desmos_merge";

    auto runCase = [&](int frameCount, std::size_t budget) {
        fs::remove_all(dir);
        DesmosOptions opt;
        opt.gridW = 16;
        opt.gridH = 8;
        opt.outDir = dir.string();
        opt.baseName = "m";
        opt.partBudget = budget;
        opt.maxListLen = 100000;
        DesmosWriter w(opt);
        for (int i = 0; i < frameCount; ++i) {
            BinaryImage mask(16, 8);
            mask.at(i % 16, 3) = 1;
            w.addFrame(encodeFrame(mask, EncodeStrategy::Rects));
        }
        return w.finish();
    };

    const DesmosSummary tail = runCase(22, 20);
    check(tail.parts.size() == 1, "a short tail is folded into the previous part");
    check(!tail.parts.empty() && tail.parts[0].frameCount == 22,
          "the merged part still holds every frame");

    if (!tail.parts.empty()) {
        const auto D = listOf(tail.parts[0].txtPath, "D");
        const auto S = listOf(tail.parts[0].txtPath, "S");
        check(S.size() == 23, "merged part has one offset per frame plus a tail");
        check(!S.empty() && S.front() == 1.0, "offsets start at 1");
        check(!S.empty() && !D.empty() && S.back() == static_cast<double>(D.size()) + 1,
              "the final offset points just past the data");
        bool monotonic = true;
        for (std::size_t i = 0; i + 1 < S.size(); ++i) {
            if (S[i + 1] < S[i]) monotonic = false;
        }
        check(monotonic, "merged offsets stay ordered");
        check(D.size() == 22, "merged part holds every region");
    }

    const DesmosSummary split = runCase(30, 20);
    check(split.parts.size() == 2, "a substantial tail stays a separate part");
    check(split.parts.size() == 2 && split.parts[1].firstFrame == split.parts[0].frameCount,
          "the second part starts where the first ends");

    const DesmosSummary tiny = runCase(3, 20);
    check(tiny.parts.size() == 1 && tiny.parts[0].frameCount == 3,
          "a video shorter than one part still emits a part");

    fs::remove_all(dir);
}

}

int main() {
    std::cout << "vid2desmos tests\n\n";
    try {
        testConvolution();
        testThreshold();
        testFilters();
        testPacking();
        testEncoding();
        testChainParsing();
        testCli();
        testOutput();
        testPartMerging();
    } catch (const std::exception& e) {
        std::cout << "\nunexpected exception: " << e.what() << '\n';
        return 1;
    }

    std::cout << "\n" << (gChecks - gFailures) << "/" << gChecks << " checks passed\n";
    return gFailures == 0 ? 0 : 1;
}
