#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace v2d {

template <typename T>
class Plane {
public:
    Plane() = default;

    Plane(int w, int h) { resize(w, h); }

    void resize(int w, int h) {
        if (w < 0 || h < 0) {
            throw std::invalid_argument("Plane: negative dimensions");
        }
        w_ = w;
        h_ = h;
        data_.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), T{});
    }

    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    bool empty() const noexcept { return data_.empty(); }
    std::size_t size() const noexcept { return data_.size(); }

    T* data() noexcept { return data_.data(); }
    const T* data() const noexcept { return data_.data(); }

    T* row(int y) noexcept { return data_.data() + static_cast<std::size_t>(y) * w_; }
    const T* row(int y) const noexcept { return data_.data() + static_cast<std::size_t>(y) * w_; }

    T& at(int x, int y) noexcept { return data_[static_cast<std::size_t>(y) * w_ + x]; }
    const T& at(int x, int y) const noexcept { return data_[static_cast<std::size_t>(y) * w_ + x]; }

    T clamped(int x, int y) const noexcept {
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        if (x >= w_) x = w_ - 1;
        if (y >= h_) y = h_ - 1;
        return data_[static_cast<std::size_t>(y) * w_ + x];
    }

private:
    int w_ = 0;
    int h_ = 0;
    std::vector<T> data_;
};

using Image = Plane<std::uint8_t>;

using FloatImage = Plane<float>;

using BinaryImage = Plane<std::uint8_t>;

FloatImage toFloat(const Image& src);

Image toImage(const FloatImage& src, bool normalize);

Image binaryToImage(const BinaryImage& mask);

std::size_t countSet(const BinaryImage& mask);

}
