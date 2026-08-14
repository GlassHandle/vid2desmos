#include "desmos.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace v2d {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string intString(double v) {
    return std::to_string(static_cast<long long>(std::llround(v)));
}

struct Expr {
    std::string plain;
    std::string latex;
    std::string id;
    std::string color;
    bool fill = false;
    std::string sliderJson;
};

Expr makeExpr(std::string id, std::string plain, std::string latex) {
    Expr e;
    e.id = std::move(id);
    e.plain = std::move(plain);
    e.latex = std::move(latex);
    return e;
}

void emitList(const std::string& name, const std::vector<double>& values, std::size_t maxLen,
              std::vector<Expr>& out) {
    const std::size_t chunks = (values.size() + maxLen - 1) / maxLen;

    auto body = [&](std::size_t from, std::size_t to) {
        std::string s;
        s.reserve((to - from) * 8);
        for (std::size_t i = from; i < to; ++i) {
            if (i != from) s += ',';
            s += intString(values[i]);
        }
        return s;
    };

    if (chunks <= 1) {
        const std::string b = body(0, values.size());
        out.push_back(makeExpr(name, name + "=[" + b + "]",
                               name + "=\\left[" + b + "\\right]"));
        return;
    }

    std::string joinPlain = name + "=join(";
    std::string joinLatex = name + "=\\operatorname{join}\\left(";
    for (std::size_t c = 0; c < chunks; ++c) {
        const std::size_t from = c * maxLen;
        const std::size_t to = std::min(values.size(), from + maxLen);
        const std::string sub = name + "_{" + std::to_string(c + 1) + "}";
        const std::string b = body(from, to);
        out.push_back(makeExpr(sub, sub + "=[" + b + "]", sub + "=\\left[" + b + "\\right]"));
        if (c != 0) {
            joinPlain += ',';
            joinLatex += ',';
        }
        joinPlain += sub;
        joinLatex += sub;
    }
    joinPlain += ')';
    joinLatex += "\\right)";
    out.push_back(makeExpr(name, joinPlain, joinLatex));
}

std::vector<Expr> buildExpressions(const DesmosOptions& opt, const std::vector<double>& data,
                                   const std::vector<double>& offsets, int frameCount) {
    const std::string gw = std::to_string(opt.gridW);
    const std::string gh = std::to_string(opt.gridH);
    const std::string hRadix = "G_{h}+1";
    const std::string wRadix = "G_{w}+1";

    std::vector<Expr> e;
    e.push_back(makeExpr("gw", "G_{w}=" + gw, "G_{w}=" + gw));
    e.push_back(makeExpr("gh", "G_{h}=" + gh, "G_{h}=" + gh));

    emitList("D", data, opt.maxListLen, e);
    emitList("S", offsets, opt.maxListLen, e);

    const std::string nf = std::to_string(frameCount);
    e.push_back(makeExpr("nf", "N_{f}=" + nf, "N_{f}=" + nf));

    Expr slider = makeExpr("f", "f=0", "f=0");
    {
        const double seconds = (opt.fps > 0.0) ? frameCount / opt.fps : frameCount / 15.0;
        const long long periodMs = std::max(200LL, static_cast<long long>(seconds * 1000.0));
        std::ostringstream s;
        s << "{\"hardMin\":true,\"hardMax\":true,\"min\":\"0\",\"max\":\""
          << (frameCount - 1) << "\",\"step\":\"1\",\"animationPeriod\":" << periodMs
          << ",\"loopMode\":\"LOOP_FORWARD\",\"isPlaying\":false}";
        slider.sliderJson = s.str();
    }
    e.push_back(std::move(slider));

    e.push_back(makeExpr("n", "n=mod(floor(f),N_{f})",
                         "n=\\operatorname{mod}\\left(\\operatorname{floor}\\left(f\\right),"
                         "N_{f}\\right)"));

    e.push_back(makeExpr("I", "I=[S[n+1]...S[n+2]-1]",
                         "I=\\left[S\\left[n+1\\right]...S\\left[n+2\\right]-1\\right]"));
    e.push_back(makeExpr("R", "R=D[I]", "R=D\\left[I\\right]"));

    e.push_back(makeExpr("hr", "h_{r}=mod(R," + hRadix + ")",
                         "h_{r}=\\operatorname{mod}\\left(R," + hRadix + "\\right)"));
    e.push_back(makeExpr("tr", "t_{r}=floor(R/(" + hRadix + "))",
                         "t_{r}=\\operatorname{floor}\\left(\\frac{R}{" + hRadix + "}\\right)"));
    e.push_back(makeExpr("wr", "w_{r}=mod(t_{r}," + wRadix + ")",
                         "w_{r}=\\operatorname{mod}\\left(t_{r}," + wRadix + "\\right)"));
    e.push_back(makeExpr("qr", "q_{r}=floor(t_{r}/(" + wRadix + "))",
                         "q_{r}=\\operatorname{floor}\\left(\\frac{t_{r}}{" + wRadix + "}\\right)"));
    e.push_back(makeExpr("xr", "x_{r}=mod(q_{r},G_{w})",
                         "x_{r}=\\operatorname{mod}\\left(q_{r},G_{w}\\right)"));
    e.push_back(makeExpr("yr", "y_{r}=floor(q_{r}/G_{w})",
                         "y_{r}=\\operatorname{floor}\\left(\\frac{q_{r}}{G_{w}}\\right)"));

    Expr bg = makeExpr("bg", "polygon((0,0),(G_{w},0),(G_{w},G_{h}),(0,G_{h}))",
                       "\\operatorname{polygon}\\left(\\left(0,0\\right),\\left(G_{w},0\\right),"
                       "\\left(G_{w},G_{h}\\right),\\left(0,G_{h}\\right)\\right)");
    bg.fill = true;
    bg.color = opt.whiteOnBlack ? "#000000" : "#ffffff";
    e.push_back(std::move(bg));

    Expr poly = makeExpr(
        "px",
        "polygon((x_{r},y_{r}),(x_{r}+w_{r},y_{r}),(x_{r}+w_{r},y_{r}+h_{r}),(x_{r},y_{r}+h_{r}))",
        "\\operatorname{polygon}\\left(\\left(x_{r},y_{r}\\right),"
        "\\left(x_{r}+w_{r},y_{r}\\right),"
        "\\left(x_{r}+w_{r},y_{r}+h_{r}\\right),"
        "\\left(x_{r},y_{r}+h_{r}\\right)\\right)");
    poly.fill = true;
    poly.color = opt.whiteOnBlack ? "#ffffff" : "#000000";
    e.push_back(std::move(poly));

    return e;
}

std::string buildStateJson(const DesmosOptions& opt, const std::vector<Expr>& exprs) {
    std::ostringstream s;
    const double pad = std::max(1.0, opt.gridW * 0.02);
    s << "{\"version\":11,\"randomSeed\":\"vid2desmos\",\"graph\":{"
      << "\"viewport\":{\"xmin\":" << -pad << ",\"ymin\":" << -pad
      << ",\"xmax\":" << (opt.gridW + pad) << ",\"ymax\":" << (opt.gridH + pad) << "},"

      << "\"showGrid\":false,\"showXAxis\":false,\"showYAxis\":false,"
      << "\"xAxisNumbers\":false,\"yAxisNumbers\":false},"
      << "\"expressions\":{\"list\":[";

    bool first = true;
    for (const Expr& e : exprs) {
        if (!first) s << ',';
        first = false;
        s << "{\"type\":\"expression\",\"id\":\"" << jsonEscape(e.id) << "\",\"latex\":\""
          << jsonEscape(e.latex) << "\"";
        if (!e.color.empty()) {
            s << ",\"color\":\"" << e.color << "\"";
        }
        if (e.fill) {
            s << ",\"fill\":true,\"fillOpacity\":\"1\",\"lineOpacity\":\"0\",\"lineWidth\":\"0\"";
        }
        if (!e.sliderJson.empty()) {
            s << ",\"slider\":" << e.sliderJson;
        }
        s << "}";
    }
    s << "]}}";
    return s.str();
}

std::uintmax_t writeTextFile(const fs::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Cannot write '" + path.string() + "'");
    }
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!f) {
        throw std::runtime_error("Write failed for '" + path.string() + "'");
    }
    return content.size();
}

}

std::optional<EncodeStrategy> encodeStrategyByName(std::string_view rawName) {
    const std::string name = lower(rawName);
    if (name == "pixels" || name == "pixel") return EncodeStrategy::Pixels;
    if (name == "runs" || name == "rle") return EncodeStrategy::Runs;
    if (name == "rects" || name == "rect" || name == "merged") return EncodeStrategy::Rects;
    return std::nullopt;
}

std::string encodeStrategyName(EncodeStrategy s) {
    switch (s) {
        case EncodeStrategy::Pixels: return "pixels";
        case EncodeStrategy::Runs: return "runs";
        case EncodeStrategy::Rects: return "rects";
    }
    return "unknown";
}

double packRect(const Rect& r, int gridW, int gridH) {
    if (r.x < 0 || r.y < 0 || r.x >= gridW || r.y >= gridH || r.w < 0 || r.h < 0 ||
        r.w > gridW || r.h > gridH) {
        throw std::invalid_argument("packRect: region outside grid");
    }
    const double q = static_cast<double>(r.y) * gridW + r.x;
    const double t = q * (gridW + 1) + r.w;
    return t * (gridH + 1) + r.h;
}

Rect unpackRect(double packed, int gridW, int gridH) {
    const double hR = gridH + 1;
    const double wR = gridW + 1;
    Rect r;
    r.h = static_cast<int>(std::fmod(packed, hR));
    const double t = std::floor(packed / hR);
    r.w = static_cast<int>(std::fmod(t, wR));
    const double q = std::floor(t / wR);
    r.x = static_cast<int>(std::fmod(q, static_cast<double>(gridW)));
    r.y = static_cast<int>(std::floor(q / gridW));
    return r;
}

std::vector<Rect> encodeFrame(const BinaryImage& mask, EncodeStrategy strategy) {
    const int w = mask.width();
    const int h = mask.height();
    std::vector<Rect> out;

    if (strategy == EncodeStrategy::Pixels) {
        for (int row = 0; row < h; ++row) {
            const std::uint8_t* src = mask.row(row);
            const int y = h - 1 - row;
            for (int x = 0; x < w; ++x) {
                if (src[x]) out.push_back(Rect{x, y, 1, 1});
            }
        }
        return out;
    }

    if (strategy == EncodeStrategy::Runs) {
        for (int row = 0; row < h; ++row) {
            const std::uint8_t* src = mask.row(row);
            const int y = h - 1 - row;
            int x = 0;
            while (x < w) {
                if (!src[x]) {
                    ++x;
                    continue;
                }
                const int start = x;
                while (x < w && src[x]) ++x;
                out.push_back(Rect{start, y, x - start, 1});
            }
        }
        return out;
    }

    struct OpenRun {
        int x;
        int len;
        int startRow;
    };

    std::vector<OpenRun> open;
    std::vector<OpenRun> next;
    std::vector<std::pair<int, int>> runs;

    auto close = [&](const OpenRun& o, int endRow) {
        out.push_back(Rect{o.x, h - 1 - endRow, o.len, endRow - o.startRow + 1});
    };

    for (int row = 0; row < h; ++row) {
        runs.clear();
        const std::uint8_t* src = mask.row(row);
        int x = 0;
        while (x < w) {
            if (!src[x]) {
                ++x;
                continue;
            }
            const int start = x;
            while (x < w && src[x]) ++x;
            runs.emplace_back(start, x - start);
        }

        next.clear();
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < open.size() || j < runs.size()) {
            const bool haveOpen = i < open.size();
            const bool haveRun = j < runs.size();
            if (haveOpen && (!haveRun || open[i].x < runs[j].first)) {
                close(open[i], row - 1);
                ++i;
            } else if (haveRun && (!haveOpen || runs[j].first < open[i].x)) {
                next.push_back(OpenRun{runs[j].first, runs[j].second, row});
                ++j;
            } else {
                if (open[i].len == runs[j].second) {
                    next.push_back(open[i]);
                } else {
                    close(open[i], row - 1);
                    next.push_back(OpenRun{runs[j].first, runs[j].second, row});
                }
                ++i;
                ++j;
            }
        }
        open.swap(next);
    }
    for (const OpenRun& o : open) {
        close(o, h - 1);
    }
    return out;
}

BinaryImage renderRegions(const std::vector<Rect>& regions, int w, int h) {
    BinaryImage out(w, h);
    for (const Rect& r : regions) {
        for (int dy = 0; dy < r.h; ++dy) {
            const int row = h - 1 - (r.y + dy);
            if (row < 0 || row >= h) continue;
            std::uint8_t* dst = out.row(row);
            const int x0 = std::max(0, r.x);
            const int x1 = std::min(w, r.x + r.w);
            for (int x = x0; x < x1; ++x) {
                dst[x] = 1;
            }
        }
    }
    return out;
}

DesmosWriter::DesmosWriter(DesmosOptions options) : opt_(std::move(options)) {
    if (opt_.gridW <= 0 || opt_.gridH <= 0) {
        throw std::invalid_argument("DesmosWriter: grid must be positive");
    }
    if (opt_.maxListLen == 0) {
        opt_.maxListLen = 10000;
    }
    if (opt_.partBudget == 0) {
        opt_.partBudget = 40000;
    }
    fs::create_directories(fs::path(opt_.outDir));
}

void DesmosWriter::addFrame(const std::vector<Rect>& regions) {
    if (finished_) {
        throw std::logic_error("DesmosWriter::addFrame called after finish()");
    }

    const std::size_t cost = regions.empty() ? 1 : regions.size();
    if (current_.frames > 0 && current_.data.size() + cost > opt_.partBudget) {
        rotatePart();
    }
    if (current_.frames == 0) {
        current_.firstFrame = summary_.frames;
    }

    if (regions.empty()) {

        current_.data.push_back(0.0);
    } else {
        for (const Rect& r : regions) {
            current_.data.push_back(packRect(r, opt_.gridW, opt_.gridH));
        }
    }

    current_.offsets.push_back(static_cast<double>(current_.data.size() + 1));
    ++current_.frames;
    ++summary_.frames;
    summary_.regions += regions.size();
    summary_.largestFrameRegions = std::max(summary_.largestFrameRegions, regions.size());
}

void DesmosWriter::mergeInto(Part& dst, const Part& src) {
    const double base = static_cast<double>(dst.data.size());
    dst.data.insert(dst.data.end(), src.data.begin(), src.data.end());

    for (std::size_t i = 1; i < src.offsets.size(); ++i) {
        dst.offsets.push_back(src.offsets[i] + base);
    }
    dst.frames += src.frames;
}

void DesmosWriter::rotatePart() {
    if (current_.frames == 0) {
        return;
    }
    if (held_) {
        writePart(*held_);
    }
    held_ = std::move(current_);
    current_ = Part{};
}

void DesmosWriter::writePart(const Part& part) {
    if (part.frames == 0) {
        return;
    }

    const std::vector<Expr> exprs =
        buildExpressions(opt_, part.data, part.offsets, part.frames);

    std::string plain;
    plain.reserve(part.data.size() * 8 + 1024);
    for (const Expr& e : exprs) {
        plain += e.plain;
        plain += '\n';
    }

    const fs::path dir(opt_.outDir);
    char suffix[16];
    std::snprintf(suffix, sizeof(suffix), "_part%02d", nextPartIndex_);

    PartInfo info;
    info.index = nextPartIndex_;
    info.firstFrame = part.firstFrame;
    info.frameCount = part.frames;
    info.regionCount = part.data.size();

    const fs::path txt = dir / (opt_.baseName + suffix + ".txt");
    info.txtPath = txt.string();
    summary_.bytesWritten += writeTextFile(txt, plain);

    if (opt_.writeJson) {
        const fs::path json = dir / (opt_.baseName + suffix + ".json");
        info.jsonPath = json.string();
        summary_.bytesWritten += writeTextFile(json, buildStateJson(opt_, exprs));
    }

    summary_.parts.push_back(info);
    ++nextPartIndex_;
}

DesmosSummary DesmosWriter::finish() {
    if (finished_) {
        return summary_;
    }

    if (current_.frames > 0) {
        const bool runt = held_.has_value() && current_.data.size() * 4 < opt_.partBudget;
        if (runt) {
            mergeInto(*held_, current_);
        } else {
            if (held_) {
                writePart(*held_);
            }
            held_ = std::move(current_);
        }
        current_ = Part{};
    }
    if (held_) {
        writePart(*held_);
        held_.reset();
    }
    finished_ = true;

    const fs::path dir(opt_.outDir);

    std::ostringstream man;
    man << "{\n  \"gridWidth\": " << opt_.gridW << ",\n  \"gridHeight\": " << opt_.gridH
        << ",\n  \"fps\": " << opt_.fps << ",\n  \"frames\": " << summary_.frames
        << ",\n  \"regions\": " << summary_.regions
        << ",\n  \"largestFrameRegions\": " << summary_.largestFrameRegions
        << ",\n  \"parts\": [\n";
    for (std::size_t i = 0; i < summary_.parts.size(); ++i) {
        const PartInfo& p = summary_.parts[i];
        man << "    {\"index\": " << p.index << ", \"firstFrame\": " << p.firstFrame
            << ", \"frameCount\": " << p.frameCount << ", \"regions\": " << p.regionCount
            << ", \"expressions\": \"" << jsonEscape(fs::path(p.txtPath).filename().string())
            << "\"}";
        if (i + 1 < summary_.parts.size()) man << ',';
        man << '\n';
    }
    man << "  ]\n}\n";

    const fs::path manifest = dir / "manifest.json";
    summary_.manifestPath = manifest.string();
    summary_.bytesWritten += writeTextFile(manifest, man.str());

    return summary_;
}

}
