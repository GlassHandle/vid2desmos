#pragma once

#include "frame.hpp"

namespace v2d {

void invert(Image& img);
void invert(FloatImage& img);

void applyGamma(Image& img, float gamma);

void applyContrast(Image& img, float contrast, float brightness);

void normalize(Image& img);

Image rgbToGray(const std::uint8_t* rgb, int w, int h);

Image resizeArea(const Image& src, int dstW, int dstH);

}
