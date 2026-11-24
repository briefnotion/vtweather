#include "png.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "inflate.h"

namespace vtw {

namespace {

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

struct Header {
    uint32_t width = 0, height = 0;
    int depth = 0;           // bits per sample
    int colour_type = 0;     // 0 grey, 2 RGB, 3 palette, 4 grey+alpha, 6 RGBA
    int channels = 0;
    size_t bits_per_pixel() const { return (size_t)channels * depth; }
    size_t row_bytes() const { return (width * bits_per_pixel() + 7) / 8; }
};

int channels_for(int colour_type) {
    switch (colour_type) {
        case 0: return 1;
        case 2: return 3;
        case 3: return 1;
        case 4: return 2;
        case 6: return 4;
        default: return 0;
    }
}

bool depth_allowed(int colour_type, int depth) {
    switch (colour_type) {
        case 0: return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
        case 3: return depth == 1 || depth == 2 || depth == 4 || depth == 8;
        default: return depth == 8 || depth == 16;       // RGB, grey+alpha, RGBA
    }
}

// Undo the filter of each row, in place. raw holds, for each row, a filter byte and then
// row_bytes bytes. Returns false if a filter type is not one of the five.
bool unfilter(std::vector<uint8_t>& raw, const Header& h) {
    const size_t row = h.row_bytes();
    const size_t bpp = std::max<size_t>(1, h.bits_per_pixel() / 8);
    std::vector<uint8_t> zeros(row, 0);
    for (size_t y = 0; y < h.height; y++) {
        uint8_t* line = &raw[y * (row + 1)];
        const int filter = line[0];
        uint8_t* cur = line + 1;
        const uint8_t* prev = y == 0 ? zeros.data() : &raw[(y - 1) * (row + 1) + 1];
        switch (filter) {
            case 0: break;
            case 1:
                for (size_t i = bpp; i < row; i++) cur[i] = (uint8_t)(cur[i] + cur[i - bpp]);
                break;
            case 2:
                for (size_t i = 0; i < row; i++) cur[i] = (uint8_t)(cur[i] + prev[i]);
                break;
            case 3:
                for (size_t i = 0; i < row; i++) {
                    const int left = i >= bpp ? cur[i - bpp] : 0;
                    cur[i] = (uint8_t)(cur[i] + ((left + prev[i]) >> 1));
                }
                break;
            case 4:
                for (size_t i = 0; i < row; i++) {
                    const int a = i >= bpp ? cur[i - bpp] : 0;       // left
                    const int b = prev[i];                           // above
                    const int c = i >= bpp ? prev[i - bpp] : 0;      // above left
                    const int p = a + b - c;
                    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                    const int predictor = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    cur[i] = (uint8_t)(cur[i] + predictor);
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

// Sample number `index` of a row, as stored (so 0..65535 at 16 bits).
uint32_t sample(const uint8_t* row, size_t index, int depth) {
    if (depth == 8) return row[index];
    if (depth == 16) return ((uint32_t)row[2 * index] << 8) | row[2 * index + 1];
    const size_t bit = index * depth;
    return (row[bit >> 3] >> (8 - depth - (bit & 7))) & ((1u << depth) - 1);
}

}  // namespace

uint32_t png_crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t n = 0; n < 256; n++) {
            uint32_t c = n;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool decode_png(const std::string& bytes, Image& image, std::string& error) {
    return decode_png((const uint8_t*)bytes.data(), bytes.size(), image, error);
}

bool decode_png(const uint8_t* data, size_t size, Image& image, std::string& error) {
    image = Image();
    auto fail = [&](const char* why) { error = why; image = Image(); return false; };

    static const uint8_t SIGNATURE[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (size < 8 || std::memcmp(data, SIGNATURE, 8) != 0) return fail("not a PNG file");

    Header h;
    bool have_header = false, have_end = false;
    std::vector<uint8_t> palette;            // r, g, b for each entry
    std::vector<uint8_t> palette_alpha;      // one per entry, as many as tRNS gave
    bool have_transparent_value = false;     // tRNS for grey or RGB: one colour is transparent
    uint32_t transparent[3] = {0, 0, 0};
    std::vector<uint8_t> compressed;         // all the IDAT data, joined

    size_t pos = 8;
    while (pos < size && !have_end) {
        if (size - pos < 12) return fail("the file ends in the middle of a chunk");
        const uint32_t length = be32(data + pos);
        if (length > size - pos - 12) return fail("the file ends in the middle of a chunk");
        const uint8_t* type = data + pos + 4;
        const uint8_t* body = data + pos + 8;
        if (be32(body + length) != png_crc32(type, 4 + (size_t)length)) return fail("a chunk is damaged (bad checksum)");
        pos += 12 + (size_t)length;

        const std::string name((const char*)type, 4);
        if (!have_header) {
            if (name != "IHDR" || length != 13) return fail("the first chunk is not a PNG header");
            h.width = be32(body);
            h.height = be32(body + 4);
            h.depth = body[8];
            h.colour_type = body[9];
            if (body[10] != 0 || body[11] != 0) return fail("unknown PNG compression or filter method");
            if (body[12] != 0) return fail("interlaced PNG pictures are not supported");
            h.channels = channels_for(h.colour_type);
            if (h.channels == 0 || !depth_allowed(h.colour_type, h.depth)) return fail("invalid PNG colour type or bit depth");
            if (h.width == 0 || h.height == 0 || h.width > (uint32_t)MAX_PNG_DIMENSION ||
                h.height > (uint32_t)MAX_PNG_DIMENSION || (size_t)h.width * h.height > MAX_PNG_PIXELS)
                return fail("the picture is empty or too large");
            have_header = true;
        } else if (name == "IHDR") {
            return fail("a second PNG header");
        } else if (name == "PLTE") {
            if (!compressed.empty()) return fail("the palette comes after the picture data");
            if (length % 3 != 0 || length == 0 || length > 768) return fail("invalid palette");
            palette.assign(body, body + length);
        } else if (name == "tRNS") {
            if (!compressed.empty()) return fail("the transparency comes after the picture data");
            if (h.colour_type == 3) {
                if (length > 256) return fail("invalid transparency");
                palette_alpha.assign(body, body + length);
            } else if (h.colour_type == 0) {
                if (length != 2) return fail("invalid transparency");
                transparent[0] = ((uint32_t)body[0] << 8) | body[1];
                have_transparent_value = true;
            } else if (h.colour_type == 2) {
                if (length != 6) return fail("invalid transparency");
                for (int c = 0; c < 3; c++) transparent[c] = ((uint32_t)body[2 * c] << 8) | body[2 * c + 1];
                have_transparent_value = true;
            }                                                // the other colour types have alpha already
        } else if (name == "IDAT") {
            compressed.insert(compressed.end(), body, body + length);
            if (compressed.size() > size) return fail("invalid picture data");
        } else if (name == "IEND") {
            have_end = true;
        }                                                    // anything else is not needed
    }
    if (!have_header) return fail("no PNG header");
    if (!have_end) return fail("the file ends before its last chunk");
    if (compressed.empty()) return fail("the file has no picture data");
    if (h.colour_type == 3 && palette.empty()) return fail("a palette picture without a palette");

    const size_t row = h.row_bytes();
    const size_t raw_size = (row + 1) * h.height;
    std::vector<uint8_t> raw;
    if (!inflate_zlib(compressed.data(), compressed.size(), raw_size, raw, error)) { image = Image(); return false; }
    if (!unfilter(raw, h)) return fail("unknown row filter in the picture data");

    image.width = (int)h.width;
    image.height = (int)h.height;
    image.rgba.assign((size_t)h.width * h.height * 4, 255);
    const int depth = h.depth;
    const uint32_t depth_max = (1u << (depth == 16 ? 16 : depth)) - 1;
    for (uint32_t y = 0; y < h.height; y++) {
        const uint8_t* line = &raw[(size_t)y * (row + 1) + 1];
        uint8_t* out = &image.rgba[(size_t)y * h.width * 4];
        for (uint32_t x = 0; x < h.width; x++, out += 4) {
            switch (h.colour_type) {
                case 0: {
                    const uint32_t v = sample(line, x, depth);
                    const uint8_t grey = depth == 16 ? (uint8_t)(v >> 8) : (uint8_t)(v * 255 / depth_max);
                    out[0] = out[1] = out[2] = grey;
                    if (have_transparent_value && v == transparent[0]) out[3] = 0;
                    break;
                }
                case 2: {
                    uint32_t v[3];
                    for (int c = 0; c < 3; c++) {
                        v[c] = sample(line, (size_t)x * 3 + c, depth);
                        out[c] = depth == 16 ? (uint8_t)(v[c] >> 8) : (uint8_t)v[c];
                    }
                    if (have_transparent_value && v[0] == transparent[0] && v[1] == transparent[1] && v[2] == transparent[2]) out[3] = 0;
                    break;
                }
                case 3: {
                    const uint32_t index = sample(line, x, depth);
                    if (index * 3 + 2 >= palette.size()) {
                        error = "a pixel uses a palette entry that does not exist";
                        image = Image();
                        return false;
                    }
                    out[0] = palette[index * 3];
                    out[1] = palette[index * 3 + 1];
                    out[2] = palette[index * 3 + 2];
                    out[3] = index < palette_alpha.size() ? palette_alpha[index] : 255;
                    break;
                }
                case 4: {
                    const uint32_t g = sample(line, (size_t)x * 2, depth), a = sample(line, (size_t)x * 2 + 1, depth);
                    out[0] = out[1] = out[2] = depth == 16 ? (uint8_t)(g >> 8) : (uint8_t)g;
                    out[3] = depth == 16 ? (uint8_t)(a >> 8) : (uint8_t)a;
                    break;
                }
                case 6:
                    for (int c = 0; c < 4; c++) {
                        const uint32_t v = sample(line, (size_t)x * 4 + c, depth);
                        out[c] = depth == 16 ? (uint8_t)(v >> 8) : (uint8_t)v;
                    }
                    break;
            }
        }
    }
    return true;
}

}  // namespace vtw
