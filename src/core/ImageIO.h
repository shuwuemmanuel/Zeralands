// ZeraLands - image loading/saving (PNG, JPG, TGA, BMP, WebP, RAW16) and heightmap import.
#pragma once

#include "core/Heightfield.h"

#include <string>
#include <vector>

namespace zl {

struct Image8 {
    int w = 0, h = 0, c = 0;
    std::vector<uint8_t> px;
    bool empty() const { return px.empty(); }
};

bool loadImage8(const std::string& path, Image8& out, int channels, std::string* err = nullptr);
Image8 resizeImage8(const Image8& img, int w, int h);
bool savePng8(const std::string& path, int w, int h, int channels, const uint8_t* data);
bool savePng16Gray(const std::string& path, int w, int h, const uint16_t* data);
bool saveRaw16(const std::string& path, const std::vector<uint16_t>& data, bool littleEndian = true);

// Loads any supported image or .raw/.r16 (square, 16-bit little endian) as a 0..1 heightfield.
bool loadHeightmap(const std::string& path, Grid& out, std::string* err = nullptr);

// Converts a 0..1 grid to 16-bit samples.
std::vector<uint16_t> toU16(const Grid& g);

}  // namespace zl
