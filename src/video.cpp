#include "video.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define V2D_POPEN _popen
#define V2D_PCLOSE _pclose
#else
#include <csignal>
#define V2D_POPEN popen
#define V2D_PCLOSE pclose
#endif

namespace v2d {
namespace {

std::string quoteArg(const std::string& s) {
#if defined(_WIN32)

    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += '\\';
        out += c;
    }
    out += '"';
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
#endif
}

std::string finalizeCommand(const std::string& cmd) {
#if defined(_WIN32)
    return "\"" + cmd + "\"";
#else
    return cmd;
#endif
}

std::string runCapture(const std::string& cmd, int& exitCode) {
    std::FILE* pipe = V2D_POPEN(finalizeCommand(cmd).c_str(), "r");
    if (pipe == nullptr) {
        exitCode = -1;
        return {};
    }
    std::string out;
    std::array<char, 4096> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        out.append(buf.data(), n);
    }
    exitCode = V2D_PCLOSE(pipe);
    return out;
}

double parseRational(const std::string& s) {
    const std::size_t slash = s.find('/');
    if (slash == std::string::npos) {
        try {
            return std::stod(s);
        } catch (...) {
            return 0.0;
        }
    }
    try {
        const double num = std::stod(s.substr(0, slash));
        const double den = std::stod(s.substr(slash + 1));
        return (den != 0.0) ? num / den : 0.0;
    } catch (...) {
        return 0.0;
    }
}

std::string extensionOf(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t slash = path.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    std::string ext = path.substr(dot);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

int roundUpEven(int v) {
    return (v % 2 == 0) ? v : v + 1;
}

std::string trim(const std::string& s) {
    std::size_t a = 0;
    std::size_t b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\r' || s[a] == '\n' || s[a] == '\t')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\r' || s[b - 1] == '\n' || s[b - 1] == '\t')) --b;
    return s.substr(a, b - a);
}

}

VideoInfo probeVideo(const std::string& path, const ToolPaths& tools) {
    std::ostringstream cmd;
    cmd << quoteArg(tools.ffprobe) << " -v error -select_streams v:0"
        << " -show_entries stream=width,height,r_frame_rate,avg_frame_rate,nb_frames,codec_name"
        << " -show_entries format=duration"
        << " -of default=noprint_wrappers=1" << ' ' << quoteArg(path);

    int exitCode = 0;
    const std::string output = runCapture(cmd.str(), exitCode);

    if (output.empty()) {
        throw std::runtime_error(
            "ffprobe produced no output for '" + path +
            "'. Check that the file exists and that ffprobe is on PATH "
            "(or pass --ffprobe <path>).");
    }

    VideoInfo info;
    info.path = path;

    double rFps = 0.0;
    double avgFps = 0.0;

    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (value.empty() || value == "N/A") continue;

        try {
            if (key == "width") {
                info.width = std::stoi(value);
            } else if (key == "height") {
                info.height = std::stoi(value);
            } else if (key == "codec_name") {
                info.codec = value;
            } else if (key == "r_frame_rate") {
                rFps = parseRational(value);
            } else if (key == "avg_frame_rate") {
                avgFps = parseRational(value);
            } else if (key == "nb_frames") {
                info.frames = std::stoll(value);
            } else if (key == "duration") {
                info.duration = std::stod(value);
            }
        } catch (const std::exception&) {

        }
    }

    info.fps = (avgFps > 0.0) ? avgFps : rFps;

    if (info.width <= 0 || info.height <= 0) {
        throw std::runtime_error("No video stream found in '" + path + "'.");
    }
    if (info.frames <= 0 && info.duration > 0.0 && info.fps > 0.0) {
        info.frames = static_cast<long long>(info.duration * info.fps + 0.5);
    }
    return info;
}

VideoReader::VideoReader(const std::string& path, int outW, int outH, double outFps,
                         const ToolPaths& tools)
    : w_(outW), h_(outH) {
    if (outW <= 0 || outH <= 0) {
        throw std::invalid_argument("VideoReader: output dimensions must be positive");
    }

    std::ostringstream vf;
    if (outFps > 0.0) {
        vf << "fps=" << outFps << ',';
    }
    vf << "scale=" << outW << ':' << outH << ":flags=area,format=gray";

    std::ostringstream cmd;
    cmd << quoteArg(tools.ffmpeg) << " -v error -nostdin"
        << " -i " << quoteArg(path) << " -an -sn -dn"
        << " -vf " << quoteArg(vf.str()) << " -f rawvideo -pix_fmt gray -";
    command_ = cmd.str();

    pipe_ = V2D_POPEN(finalizeCommand(command_).c_str(), "rb");
    if (pipe_ == nullptr) {
        throw std::runtime_error(
            "Failed to start ffmpeg. Check that it is on PATH (or pass --ffmpeg <path>).");
    }
}

VideoReader::~VideoReader() {
    if (pipe_ != nullptr) {
        V2D_PCLOSE(pipe_);
        pipe_ = nullptr;
    }
}

bool VideoReader::readFrame(Image& out) {
    if (pipe_ == nullptr) {
        return false;
    }
    if (out.width() != w_ || out.height() != h_) {
        out.resize(w_, h_);
    }

    const std::size_t need = static_cast<std::size_t>(w_) * static_cast<std::size_t>(h_);
    std::size_t got = 0;
    std::uint8_t* dst = out.data();

    while (got < need) {
        const std::size_t n = std::fread(dst + got, 1, need - got, pipe_);
        if (n == 0) {
            if (got != 0) {
                throw std::runtime_error(
                    "ffmpeg stream ended mid-frame (" + std::to_string(got) + " of " +
                    std::to_string(need) + " bytes). The input may be truncated.");
            }
            return false;
        }
        got += n;
    }

    ++framesRead_;
    return true;
}

VideoWriter::VideoWriter(const std::string& path, int w, int h, double fps, int scale,
                         const ToolPaths& tools)
    : path_(path), w_(w), h_(h) {
    if (w <= 0 || h <= 0) {
        throw std::invalid_argument("VideoWriter: frame dimensions must be positive");
    }
    if (fps <= 0.0) {
        fps = 15.0;
    }

    scale_ = (scale > 0) ? scale : std::max(1, static_cast<int>(720.0 / w + 0.5));

    outW_ = roundUpEven(w_ * scale_);
    outH_ = roundUpEven(h_ * scale_);

    const std::string ext = extensionOf(path);
    const bool isGif = (ext == ".gif");

    std::ostringstream vf;
    vf << "scale=" << outW_ << ':' << outH_ << ":flags=neighbor";

    std::ostringstream cmd;
    cmd << quoteArg(tools.ffmpeg) << " -v error -y -nostdin"
        << " -f rawvideo -pix_fmt gray -s " << w_ << 'x' << h_ << " -r " << fps << " -i -"
        << " -an -vf " << quoteArg(vf.str());
    if (isGif) {
        cmd << " -loop 0";
    } else {

        cmd << " -pix_fmt yuv420p -movflags +faststart";
    }
    cmd << ' ' << quoteArg(path);
    command_ = cmd.str();

#if !defined(_WIN32)

    std::signal(SIGPIPE, SIG_IGN);
#endif

    pipe_ = V2D_POPEN(finalizeCommand(command_).c_str(), "wb");
    if (pipe_ == nullptr) {
        throw std::runtime_error(
            "Failed to start ffmpeg for '" + path +
            "'. Check that it is on PATH (or pass --ffmpeg <path>).");
    }
}

VideoWriter::~VideoWriter() {
    if (pipe_ != nullptr) {
        V2D_PCLOSE(pipe_);
        pipe_ = nullptr;
    }
}

void VideoWriter::writeFrame(const Image& img) {
    if (pipe_ == nullptr) {
        throw std::logic_error("VideoWriter::writeFrame called after close()");
    }
    if (img.width() != w_ || img.height() != h_) {
        throw std::invalid_argument("VideoWriter: frame size does not match the stream");
    }

    const std::size_t need = img.size();
    const std::size_t wrote = std::fwrite(img.data(), 1, need, pipe_);
    if (wrote != need) {
        throw std::runtime_error(
            "ffmpeg stopped accepting frames while writing '" + path_ +
            "'. Rerun with --verbose to see its error output.");
    }
    ++framesWritten_;
}

void VideoWriter::close() {
    if (pipe_ == nullptr) {
        return;
    }
    const int status = V2D_PCLOSE(pipe_);
    pipe_ = nullptr;
    if (status != 0) {
        throw std::runtime_error("ffmpeg exited with status " + std::to_string(status) +
                                 " while writing '" + path_ +
                                 "'. Rerun with --verbose to see its command line.");
    }
}

}
