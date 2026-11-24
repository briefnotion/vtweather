// PNG reader: turns the bytes of a PNG file into 8-bit RGBA pixels. Written for pictures that
// arrive over the network, so it checks everything (signature, every chunk's CRC, sizes, the
// compressed data's checksum) and gives an error for anything odd instead of guessing.
//
// It reads every kind of PNG picture except interlaced ones (a service that sends those
// gets a clear error): grey, grey with alpha, RGB, RGBA and palette pictures, at every bit
// depth PNG allows, with transparency (tRNS) too. Colours are the stored values; gamma and
// colour profiles are ignored, which is what the radar colour table was made from.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vtw {

struct Image {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;       // width * height * 4, row by row from the top

    const uint8_t* pixel(int x, int y) const { return &rgba[((size_t)y * width + x) * 4]; }
};

constexpr int MAX_PNG_DIMENSION = 8192;                  // either side
constexpr size_t MAX_PNG_PIXELS = 32u * 1024 * 1024;     // width * height

// Decode a PNG held in memory. On failure returns false and says why in error; image is empty.
bool decode_png(const uint8_t* data, size_t size, Image& image, std::string& error);
bool decode_png(const std::string& bytes, Image& image, std::string& error);

// CRC-32 as PNG uses it (exposed for the tests, which repair checksums after damaging a file).
uint32_t png_crc32(const uint8_t* data, size_t size);

}  // namespace vtw
