#include "png.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace v2d {
namespace {

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> t{};
        for (std::uint32_t n = 0; n < 256; ++n) {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();
    return table;
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t len) {
    const auto& t = crcTable();
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        c = t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t len) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < len; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

void pushBE32(std::vector<std::uint8_t>& v, std::uint32_t x) {
    v.push_back(static_cast<std::uint8_t>(x >> 24));
    v.push_back(static_cast<std::uint8_t>(x >> 16));
    v.push_back(static_cast<std::uint8_t>(x >> 8));
    v.push_back(static_cast<std::uint8_t>(x));
}

void pushChunk(std::vector<std::uint8_t>& out, const char type[4],
               const std::vector<std::uint8_t>& payload) {
    pushBE32(out, static_cast<std::uint32_t>(payload.size()));
    std::vector<std::uint8_t> body;
    body.reserve(payload.size() + 4);
    for (int i = 0; i < 4; ++i) {
        body.push_back(static_cast<std::uint8_t>(type[i]));
    }
    body.insert(body.end(), payload.begin(), payload.end());
    out.insert(out.end(), body.begin(), body.end());
    pushBE32(out, crc32(body.data(), body.size()));
}

std::vector<std::uint8_t> zlibStored(const std::vector<std::uint8_t>& raw) {
    std::vector<std::uint8_t> out;
    out.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
    out.push_back(0x78);
    out.push_back(0x01);

    std::size_t pos = 0;
    if (raw.empty()) {
        out.push_back(0x01);
        out.push_back(0x00);
        out.push_back(0x00);
        out.push_back(0xFF);
        out.push_back(0xFF);
    }
    while (pos < raw.size()) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - pos);
        const bool last = (pos + n) >= raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(n & 0xFF));
        out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>((~n) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(((~n) >> 8) & 0xFF));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + n);
        pos += n;
    }

    pushBE32(out, adler32(raw.data(), raw.size()));
    return out;
}

}

void writeGrayPng(const std::string& path, const Image& img) {
    if (img.empty()) {
        throw std::invalid_argument("writeGrayPng: empty image");
    }

    const int w = img.width();
    const int h = img.height();

    std::vector<std::uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(h) * (static_cast<std::size_t>(w) + 1));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const std::uint8_t* row = img.row(y);
        raw.insert(raw.end(), row, row + w);
    }

    std::vector<std::uint8_t> out = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<std::uint8_t> ihdr;
    pushBE32(ihdr, static_cast<std::uint32_t>(w));
    pushBE32(ihdr, static_cast<std::uint32_t>(h));
    ihdr.push_back(8);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    pushChunk(out, "IHDR", ihdr);

    pushChunk(out, "IDAT", zlibStored(raw));
    pushChunk(out, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("Cannot write PNG '" + path + "'");
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) {
        throw std::runtime_error("Write failed for PNG '" + path + "'");
    }
}

}
