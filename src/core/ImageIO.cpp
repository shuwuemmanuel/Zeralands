#include "core/ImageIO.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"
#include "lodepng.h"

#ifdef ZL_HAVE_WEBP
#include <webp/decode.h>
#endif

namespace zl {

namespace fs = std::filesystem;

static std::string lowerExt(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    for (auto& ch : e) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return e;
}

static bool readFile(const std::string& path, std::vector<uint8_t>& bytes) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    std::streamoff n = f.tellg();
    f.seekg(0, std::ios::beg);
    if (n <= 0) return false;
    bytes.resize(size_t(n));
    f.read(reinterpret_cast<char*>(bytes.data()), n);
    return bool(f);
}

bool loadImage8(const std::string& path, Image8& out, int channels, std::string* err) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (lowerExt(path) == ".webp") {
#ifdef ZL_HAVE_WEBP
        int w = 0, h = 0;
        uint8_t* rgba = WebPDecodeRGBA(bytes.data(), bytes.size(), &w, &h);
        if (!rgba) {
            if (err) *err = "webp decode failed: " + path;
            return false;
        }
        out.w = w;
        out.h = h;
        out.c = channels;
        out.px.resize(size_t(w) * h * channels);
        for (size_t i = 0; i < size_t(w) * h; ++i)
            for (int c = 0; c < channels; ++c) out.px[i * channels + c] = rgba[i * 4 + std::min(c, 3)];
        WebPFree(rgba);
        return true;
#else
        if (err) *err = "built without WebP support";
        return false;
#endif
    }
    int w, h, n;
    uint8_t* data = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &n, channels);
    if (!data) {
        if (err) *err = std::string("decode failed: ") + stbi_failure_reason();
        return false;
    }
    out.w = w;
    out.h = h;
    out.c = channels;
    out.px.assign(data, data + size_t(w) * h * channels);
    stbi_image_free(data);
    return true;
}

Image8 resizeImage8(const Image8& img, int w, int h) {
    Image8 out;
    out.w = w;
    out.h = h;
    out.c = img.c;
    out.px.resize(size_t(w) * h * img.c);
    stbir_resize_uint8_linear(img.px.data(), img.w, img.h, 0, out.px.data(), w, h, 0, stbir_pixel_layout(img.c));
    return out;
}

bool savePng8(const std::string& path, int w, int h, int channels, const uint8_t* data) {
    LodePNGColorType ct = channels == 1 ? LCT_GREY : channels == 2 ? LCT_GREY_ALPHA : channels == 3 ? LCT_RGB : LCT_RGBA;
    std::vector<unsigned char> png;
    if (lodepng::encode(png, data, unsigned(w), unsigned(h), ct, 8)) return false;
    std::ofstream f(fs::u8path(path), std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(f);
}

bool savePng16Gray(const std::string& path, int w, int h, const uint16_t* data) {
    // lodepng expects big-endian 16-bit samples
    std::vector<unsigned char> be(size_t(w) * h * 2);
    for (size_t i = 0; i < size_t(w) * h; ++i) {
        be[i * 2] = uint8_t(data[i] >> 8);
        be[i * 2 + 1] = uint8_t(data[i] & 0xFF);
    }
    std::vector<unsigned char> png;
    if (lodepng::encode(png, be, unsigned(w), unsigned(h), LCT_GREY, 16)) return false;
    std::ofstream f(fs::u8path(path), std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), std::streamsize(png.size()));
    return bool(f);
}

bool saveRaw16(const std::string& path, const std::vector<uint16_t>& data, bool little) {
    std::ofstream f(fs::u8path(path), std::ios::binary);
    for (uint16_t v : data) {
        uint8_t b[2] = {little ? uint8_t(v & 0xFF) : uint8_t(v >> 8), little ? uint8_t(v >> 8) : uint8_t(v & 0xFF)};
        f.write(reinterpret_cast<const char*>(b), 2);
    }
    return bool(f);
}

std::vector<uint16_t> toU16(const Grid& g) {
    std::vector<uint16_t> out(g.size());
    for (size_t i = 0; i < g.size(); ++i) out[i] = uint16_t(std::lround(clamp01(g.vec()[i]) * 65535.f));
    return out;
}

bool loadHeightmap(const std::string& path, Grid& out, std::string* err) {
    const std::string ext = lowerExt(path);
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        if (err) *err = "cannot read " + path;
        return false;
    }
    if (ext == ".raw" || ext == ".r16") {
        size_t samples = bytes.size() / 2;
        int side = int(std::lround(std::sqrt(double(samples))));
        if (size_t(side) * side != samples || side < 2) {
            if (err) *err = "RAW must be a square 16-bit little-endian file";
            return false;
        }
        out.resize(side, side);
        for (size_t i = 0; i < samples; ++i) out.vec()[i] = float(uint16_t(bytes[i * 2] | (bytes[i * 2 + 1] << 8))) / 65535.f;
        return true;
    }
    if (ext == ".png") {
        // keep full 16-bit precision when present
        unsigned w = 0, h = 0;
        std::vector<unsigned char> img;
        lodepng::State st;
        if (lodepng_inspect(&w, &h, &st, bytes.data(), bytes.size()) == 0 && st.info_png.color.bitdepth == 16) {
            if (lodepng::decode(img, w, h, bytes, LCT_GREY, 16) == 0) {
                out.resize(int(w), int(h));
                for (size_t i = 0; i < size_t(w) * h; ++i) out.vec()[i] = float((img[i * 2] << 8) | img[i * 2 + 1]) / 65535.f;
                return true;
            }
        }
    }
    Image8 im;
    if (!loadImage8(path, im, 1, err)) return false;
    out.resize(im.w, im.h);
    for (size_t i = 0; i < im.px.size(); ++i) out.vec()[i] = float(im.px[i]) / 255.f;
    // 8-bit sources terrace badly; a light blur removes the quantisation steps
    out.blur(1, 1);
    return true;
}

}  // namespace zl
