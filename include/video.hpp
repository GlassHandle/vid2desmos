#pragma once

#include <cstdio>
#include <string>

#include "frame.hpp"

namespace v2d {

struct ToolPaths {
    std::string ffmpeg = "ffmpeg";
    std::string ffprobe = "ffprobe";
};

struct VideoInfo {
    std::string path;
    std::string codec;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;
    long long frames = 0;
};

VideoInfo probeVideo(const std::string& path, const ToolPaths& tools);

class VideoReader {
public:
    VideoReader(const std::string& path, int outW, int outH, double outFps,
                const ToolPaths& tools);
    ~VideoReader();

    VideoReader(const VideoReader&) = delete;
    VideoReader& operator=(const VideoReader&) = delete;
    VideoReader(VideoReader&&) = delete;
    VideoReader& operator=(VideoReader&&) = delete;

    bool readFrame(Image& out);

    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    long long framesRead() const noexcept { return framesRead_; }

    const std::string& command() const noexcept { return command_; }

private:
    std::FILE* pipe_ = nullptr;
    std::string command_;
    int w_ = 0;
    int h_ = 0;
    long long framesRead_ = 0;
};

class VideoWriter {
public:

    VideoWriter(const std::string& path, int w, int h, double fps, int scale,
                const ToolPaths& tools);
    ~VideoWriter();

    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;
    VideoWriter(VideoWriter&&) = delete;
    VideoWriter& operator=(VideoWriter&&) = delete;

    void writeFrame(const Image& img);

    void close();

    int outWidth() const noexcept { return outW_; }
    int outHeight() const noexcept { return outH_; }
    int scale() const noexcept { return scale_; }
    long long framesWritten() const noexcept { return framesWritten_; }
    const std::string& command() const noexcept { return command_; }

private:
    std::FILE* pipe_ = nullptr;
    std::string command_;
    std::string path_;
    int w_ = 0;
    int h_ = 0;
    int scale_ = 1;
    int outW_ = 0;
    int outH_ = 0;
    long long framesWritten_ = 0;
};

}
