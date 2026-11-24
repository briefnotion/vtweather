// Tests for the PNG reader and the inflate code under it.
//   1. every picture in tests/data decodes to exactly the pixels the generator expects
//      (see make_png_fixtures.py: synthetic pictures judged against Pillow, and real radar
//      pictures from NOAA);
//   2. odd and damaged files are refused with a reason, never crash: made-up files for each
//      rule, every shortened version of some files, and thousands of randomly damaged ones
//      (with their checksums repaired, so the damage reaches the decoder). Run under ASan.
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "check.h"

#include "../src/inflate.h"
#include "../src/png.h"

#ifndef VTW_TEST_DATA_DIR
#error "VTW_TEST_DATA_DIR must name the tests/data folder"
#endif

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

uint32_t crc_of(const std::vector<uint8_t>& bytes) { return vtw::png_crc32(bytes.data(), bytes.size()); }

// ---- making small PNG files by hand (the pixel data is stored, not compressed)

void put32(std::string& s, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) s += (char)((v >> shift) & 0xFF);
}

std::string chunk(const std::string& type, const std::string& body) {
    std::string s;
    put32(s, (uint32_t)body.size());
    s += type + body;
    put32(s, vtw::png_crc32((const uint8_t*)(type + body).data(), 4 + body.size()));
    return s;
}

uint32_t adler(const std::string& data) {
    uint32_t a = 1, b = 0;
    for (unsigned char c : data) { a = (a + c) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

std::string stored_zlib(const std::string& raw) {
    std::string z = "\x78\x01";
    size_t pos = 0;
    do {
        const size_t n = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + n >= raw.size();
        z += (char)(last ? 1 : 0);
        z += (char)(n & 0xFF); z += (char)(n >> 8);
        z += (char)(~n & 0xFF); z += (char)((~n >> 8) & 0xFF);
        z += raw.substr(pos, n);
        pos += n;
    } while (pos < raw.size());
    put32(z, adler(raw));
    return z;
}

struct Spec {
    uint32_t width = 2, height = 2;
    int depth = 8, colour_type = 6, interlace = 0;
    std::string raw;                // filter byte + pixel bytes for every row
    std::string palette;
    bool header = true, data = true, end = true, palette_after_data = false;
};

std::string make_png(Spec s) {
    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    put32(ihdr, s.width); put32(ihdr, s.height);
    ihdr += (char)s.depth; ihdr += (char)s.colour_type; ihdr += '\0'; ihdr += '\0'; ihdr += (char)s.interlace;
    if (s.header) png += chunk("IHDR", ihdr);
    if (!s.palette.empty() && !s.palette_after_data) png += chunk("PLTE", s.palette);
    if (s.data) png += chunk("IDAT", stored_zlib(s.raw));
    if (!s.palette.empty() && s.palette_after_data) png += chunk("PLTE", s.palette);
    if (s.end) png += chunk("IEND", "");
    return png;
}

std::string rgba_rows(int rows, int filter = 0) {      // 2 pixels wide, 4 bytes each
    std::string raw;
    for (int y = 0; y < rows; y++) { raw += (char)filter; raw += std::string("\x0a\x14\x1e\xff\x28\x32\x3c\x80", 8); }
    return raw;
}

bool refused(const std::string& png, const char* reason_part) {
    vtw::Image image;
    std::string error;
    const bool ok = vtw::decode_png(png, image, error);
    if (ok) { std::printf("  accepted a file that should be refused (%s)\n", reason_part); return false; }
    if (error.find(reason_part) == std::string::npos) {
        std::printf("  refused with '%s', expected a reason with '%s'\n", error.c_str(), reason_part);
        return false;
    }
    return image.rgba.empty();
}

// ---- 1. the fixtures

void fixture_tests() {
    std::ifstream manifest(std::string(VTW_TEST_DATA_DIR) + "/manifest.txt");
    std::string name;
    int width, height;
    unsigned long crc;
    int count = 0;
    while (manifest >> name >> width >> height >> crc) {
        count++;
        const std::string png = read_file(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png");
        vtw::Image image;
        std::string error;
        const bool ok = vtw::decode_png(png, image, error);
        if (!ok) std::printf("  %s: %s\n", name.c_str(), error.c_str());
        CHECK(ok);
        if (!ok) continue;
        CHECK(image.width == width && image.height == height);
        const bool same = crc_of(image.rgba) == (uint32_t)crc;
        if (!same) {
            std::printf("  %s: pixels differ\n", name.c_str());
            const std::string expected = read_file(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".rgba");
            for (size_t i = 0; i < expected.size() && i < image.rgba.size(); i++)
                if ((uint8_t)expected[i] != image.rgba[i]) {
                    std::printf("  first difference at pixel %zu, channel %zu: got %d, expected %d\n", i / 4, i % 4,
                                image.rgba[i], (uint8_t)expected[i]);
                    break;
                }
        }
        CHECK(same);
    }
    CHECK(count >= 30);
    std::printf("%d pictures checked\n", count);
}

// ---- 2. files that must be refused

void refusal_tests() {
    // the hand-made file itself is fine
    {
        Spec s; s.raw = rgba_rows(2);
        vtw::Image image; std::string error;
        CHECK(vtw::decode_png(make_png(s), image, error));
        CHECK(image.width == 2 && image.height == 2);
        CHECK(image.pixel(1, 1)[0] == 0x28 && image.pixel(1, 1)[3] == 0x80);
    }
    { Spec s; s.raw = rgba_rows(2); std::string p = make_png(s); p[1] = 'X'; CHECK(refused(p, "not a PNG")); }
    CHECK(refused("", "not a PNG"));
    CHECK(refused("\x89PNG", "not a PNG"));
    { Spec s; s.raw = rgba_rows(2); s.interlace = 1; CHECK(refused(make_png(s), "interlaced")); }
    { Spec s; s.raw = rgba_rows(2); s.width = 100000; CHECK(refused(make_png(s), "too large")); }
    { Spec s; s.raw = rgba_rows(2); s.width = 8193; CHECK(refused(make_png(s), "too large")); }
    { Spec s; s.raw = rgba_rows(2); s.width = 0; CHECK(refused(make_png(s), "too large")); }
    { Spec s; s.raw = rgba_rows(2); s.colour_type = 2; s.depth = 4; CHECK(refused(make_png(s), "colour type or bit depth")); }
    { Spec s; s.raw = rgba_rows(2); s.colour_type = 5; CHECK(refused(make_png(s), "colour type or bit depth")); }
    { Spec s; s.raw = rgba_rows(2); s.header = false; CHECK(refused(make_png(s), "header")); }
    { Spec s; s.raw = rgba_rows(2); s.data = false; CHECK(refused(make_png(s), "no picture data")); }
    { Spec s; s.raw = rgba_rows(2); s.end = false; CHECK(refused(make_png(s), "last chunk")); }
    { Spec s; s.raw = rgba_rows(2, 7); CHECK(refused(make_png(s), "filter")); }
    { Spec s; s.raw = rgba_rows(1); CHECK(refused(make_png(s), "less data")); }       // a row missing
    { Spec s; s.raw = rgba_rows(3); CHECK(refused(make_png(s), "more data")); }       // a row too many
    {   // a palette picture whose pixel names an entry that is not there
        Spec s; s.colour_type = 3; s.width = 2; s.height = 1;
        s.raw = std::string("\x00\x00\x05", 3); s.palette = std::string("\x01\x02\x03\x04\x05\x06", 6);
        CHECK(refused(make_png(s), "palette entry"));
        s.raw = std::string("\x00\x00\x01", 3);
        vtw::Image image; std::string error;
        CHECK(vtw::decode_png(make_png(s), image, error));
        s.palette.clear();
        CHECK(refused(make_png(s), "without a palette"));
        s.palette = std::string("\x01\x02\x03", 3); s.palette_after_data = true;
        CHECK(refused(make_png(s), "palette comes after"));
        s.palette = std::string("\x01\x02", 2); s.palette_after_data = false;
        CHECK(refused(make_png(s), "invalid palette"));
    }
    {   // a damaged chunk (a byte changed in the header, its checksum left alone)
        Spec s; s.raw = rgba_rows(2); std::string p = make_png(s); p[16] ^= 1;
        CHECK(refused(p, "checksum"));
    }
    {   // damaged compressed data whose chunk checksum is right: the zlib checksum catches it
        Spec s; s.raw = rgba_rows(2);
        std::string z = stored_zlib(s.raw);
        z[z.size() - 1] ^= 1;                                     // the Adler-32 at the end
        std::string p = "\x89PNG\r\n\x1a\n";
        std::string ihdr; put32(ihdr, 2); put32(ihdr, 2); ihdr += std::string("\x08\x06\x00\x00\x00", 5);
        p += chunk("IHDR", ihdr) + chunk("IDAT", z) + chunk("IEND", "");
        CHECK(refused(p, "checksum of the picture"));
    }
    {   // the picture data in several IDAT chunks, and a chunk we do not know, are fine
        Spec s; s.raw = rgba_rows(2);
        const std::string z = stored_zlib(s.raw);
        std::string p = "\x89PNG\r\n\x1a\n";
        std::string ihdr; put32(ihdr, 2); put32(ihdr, 2); ihdr += std::string("\x08\x06\x00\x00\x00", 5);
        p += chunk("IHDR", ihdr) + chunk("tEXt", "a\0b") + chunk("IDAT", z.substr(0, 5)) + chunk("IDAT", z.substr(5)) + chunk("IEND", "");
        vtw::Image image; std::string error;
        CHECK(vtw::decode_png(p, image, error));
    }
}

// ---- 3. shortened and damaged files

void damage_tests() {
    // every shortened version of a few files is refused (and none crashes)
    for (const char* name : {"palette_4", "rgba_8", "grey_16_trns", "noaa_kansas_clear"}) {
        const std::string png = read_file(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png");
        const size_t step = png.size() > 3000 ? 7 : 1;
        for (size_t length = 0; length < png.size(); length += step) {
            vtw::Image image; std::string error;
            if (vtw::decode_png(png.substr(0, length), image, error)) { std::printf("  %s cut to %zu bytes was accepted\n", name, length); CHECK(false); }
        }
    }
    // random damage with the checksums put right again, so that it reaches the decoder
    std::mt19937 rng(20261124);
    int accepted = 0, total = 0;
    for (const char* name : {"palette_4", "palette_8_trns", "rgba_16", "grey_2", "rgb_8_trns", "gradient_rgba", "grey_1", "filter_4"}) {
        const std::string original = read_file(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png");
        for (int round = 0; round < 700; round++) {
            std::string p = original;
            const int changes = 1 + (int)(rng() % 3);
            for (int k = 0; k < changes; k++) {
                const size_t at = 8 + rng() % (p.size() - 8);
                p[at] = (char)(rng() % 256);
            }
            // put every chunk's checksum right
            size_t pos = 8;
            while (pos + 12 <= p.size()) {
                const uint32_t length = ((uint8_t)p[pos] << 24) | ((uint8_t)p[pos + 1] << 16) | ((uint8_t)p[pos + 2] << 8) | (uint8_t)p[pos + 3];
                if (length > p.size() - pos - 12) break;
                const uint32_t crc = vtw::png_crc32((const uint8_t*)p.data() + pos + 4, 4 + (size_t)length);
                for (int i = 0; i < 4; i++) p[pos + 8 + length + i] = (char)((crc >> (24 - 8 * i)) & 0xFF);
                pos += 12 + length;
            }
            vtw::Image image; std::string error;
            total++;
            if (vtw::decode_png(p, image, error)) {
                accepted++;
                CHECK(image.width > 0 && image.height > 0 && image.rgba.size() == (size_t)image.width * image.height * 4);
            } else {
                CHECK(!error.empty() && image.rgba.empty());
            }
        }
    }
    std::printf("%d damaged files tried, %d still decoded (damage that does not matter), the rest refused\n", total, accepted);
}

// ---- 4. the compressed data alone

void inflate_tests() {
    // damaged streams of every block kind, from the pictures' own data
    std::mt19937 rng(7);
    int tried = 0;
    for (const char* name : {"grey_4", "grey_8", "rgba_8", "gradient_rgba", "palette_2", "grey_1", "palette_8"}) {
        const std::string png = read_file(std::string(VTW_TEST_DATA_DIR) + "/" + name + ".png");
        // find the first IDAT chunk's data
        const size_t idat = png.find("IDAT");
        const uint32_t length = ((uint8_t)png[idat - 4] << 24) | ((uint8_t)png[idat - 3] << 16) | ((uint8_t)png[idat - 2] << 8) | (uint8_t)png[idat - 1];
        const std::string z = png.substr(idat + 4, length);
        for (int round = 0; round < 1500; round++) {
            std::string d = z;
            const int changes = 1 + (int)(rng() % 4);
            for (int k = 0; k < changes; k++) d[rng() % d.size()] ^= (char)(1 << (rng() % 8));
            std::vector<uint8_t> out; std::string error;
            const size_t expected = 1 + rng() % 20000;
            const bool ok = vtw::inflate_zlib((const uint8_t*)d.data(), d.size(), expected, out, error);
            tried++;
            CHECK(ok ? out.size() == expected : out.empty());
        }
    }
    // pure noise after a valid header, and every short length of it
    for (int round = 0; round < 3000; round++) {
        std::string d = "\x78\x9c";
        const size_t n = rng() % 200;
        for (size_t i = 0; i < n; i++) d += (char)(rng() % 256);
        std::vector<uint8_t> out; std::string error;
        const bool ok = vtw::inflate_zlib((const uint8_t*)d.data(), d.size(), 1 + rng() % 5000, out, error);
        tried++;
        CHECK(!ok || !out.empty());
    }
    // a stream that claims more output than the picture can hold stops at the limit
    {
        std::string raw(1000, 'x');
        const std::string z = stored_zlib(raw);
        std::vector<uint8_t> out; std::string error;
        CHECK(!vtw::inflate_zlib((const uint8_t*)z.data(), z.size(), 999, out, error));
        CHECK(error.find("more data") != std::string::npos);
        CHECK(vtw::inflate_zlib((const uint8_t*)z.data(), z.size(), 1000, out, error));
        CHECK(!vtw::inflate_zlib((const uint8_t*)z.data(), z.size(), 1001, out, error));
    }
    std::printf("%d damaged compressed streams tried\n", tried);
}

}  // namespace

int main() {
    fixture_tests();
    refusal_tests();
    damage_tests();
    inflate_tests();
    if (failures == 0) std::printf("all PNG tests passed\n");
    else std::printf("%d check(s) failed\n", failures);
    return failures;
}
