// The method follows the plain, slow-but-simple style of zlib's "puff" example: codes are
// decoded one bit at a time with a count/symbol table. That is fast enough for a radar
// picture of a few megabytes, and short enough to check by reading.
#include "inflate.h"

#include <algorithm>
#include <cstring>

namespace vtw {

namespace {

constexpr int MAX_BITS = 15;           // longest Huffman code
constexpr int MAX_LITERAL_CODES = 286;
constexpr int MAX_DISTANCE_CODES = 30;
constexpr int FIXED_LITERAL_CODES = 288;

// Reads bits from the data, least significant first.
struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t pos = 0;
    uint32_t buffer = 0;
    int count = 0;
    bool ran_out = false;

    // The next `need` bits (0 to 16) as a number.
    uint32_t bits(int need) {
        uint32_t value = buffer;
        while (count < need) {
            if (pos >= size) { ran_out = true; return 0; }
            value |= (uint32_t)data[pos++] << count;
            count += 8;
        }
        buffer = value >> need;
        count -= need;
        return value & ((1u << need) - 1);
    }
};

// A Huffman code: how many codes of each length, and the symbols in code order.
struct Huffman {
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[FIXED_LITERAL_CODES];
};

// Build a code from the length of each symbol's code. Returns 0 for a complete code, a
// positive number for an incomplete one and a negative number for an over-subscribed one.
int build(Huffman& h, const uint8_t* length, int n) {
    std::memset(h.count, 0, sizeof h.count);
    for (int s = 0; s < n; s++) h.count[length[s]]++;
    if (h.count[0] == n) return 0;                      // no codes at all: fine for distances

    int left = 1;
    for (int len = 1; len <= MAX_BITS; len++) {
        left <<= 1;
        left -= h.count[len];
        if (left < 0) return left;
    }
    uint16_t offset[MAX_BITS + 1];
    offset[1] = 0;
    for (int len = 1; len < MAX_BITS; len++) offset[len + 1] = offset[len] + h.count[len];
    for (int s = 0; s < n; s++)
        if (length[s] != 0) h.symbol[offset[length[s]]++] = (uint16_t)s;
    return left;
}

// The next symbol, or a negative number if the bits are not a code or the data ran out.
int decode(BitReader& in, const Huffman& h) {
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= MAX_BITS; len++) {
        code |= (int)in.bits(1);
        if (in.ran_out) return -1;
        const int count = h.count[len];
        if (code - count < first) return h.symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -2;
}

const uint16_t LENGTH_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                  35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t LENGTH_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                   3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t DISTANCE_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                    6145, 8193, 12289, 16385, 24577};
const uint16_t DISTANCE_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
                                     8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// The part of a block made of codes: literals, and copies of earlier output.
// Returns an error text, or nullptr when the block ended properly.
const char* run_codes(BitReader& in, const Huffman& literal, const Huffman& distance,
                      std::vector<uint8_t>& out, size_t limit) {
    for (;;) {
        int symbol = decode(in, literal);
        if (symbol < 0) return in.ran_out ? "the compressed data ends too soon" : "invalid code in the compressed data";
        if (symbol < 256) {
            if (out.size() >= limit) return "the picture holds more data than its size allows";
            out.push_back((uint8_t)symbol);
        } else if (symbol == 256) {
            return nullptr;                                   // end of block
        } else {
            symbol -= 257;
            if (symbol >= 29) return "invalid length code";
            const int length = LENGTH_BASE[symbol] + (int)in.bits(LENGTH_EXTRA[symbol]);
            const int dsym = decode(in, distance);
            if (dsym < 0) return in.ran_out ? "the compressed data ends too soon" : "invalid distance code";
            if (dsym >= 30) return "invalid distance code";
            const size_t dist = DISTANCE_BASE[dsym] + in.bits(DISTANCE_EXTRA[dsym]);
            if (in.ran_out) return "the compressed data ends too soon";
            if (dist > out.size()) return "a copy reaches back before the start of the data";
            if (out.size() + (size_t)length > limit) return "the picture holds more data than its size allows";
            for (int i = 0; i < length; i++) out.push_back(out[out.size() - dist]);
        }
    }
}

const char* stored_block(BitReader& in, std::vector<uint8_t>& out, size_t limit) {
    in.buffer = 0;                                            // drop the rest of the current byte
    in.count = 0;
    if (in.pos + 4 > in.size) return "the compressed data ends too soon";
    const uint32_t len = in.data[in.pos] | (in.data[in.pos + 1] << 8);
    const uint32_t not_len = in.data[in.pos + 2] | (in.data[in.pos + 3] << 8);
    in.pos += 4;
    if (len != (~not_len & 0xFFFF)) return "damaged stored block";
    if (in.pos + len > in.size) return "the compressed data ends too soon";
    if (out.size() + len > limit) return "the picture holds more data than its size allows";
    out.insert(out.end(), in.data + in.pos, in.data + in.pos + len);
    in.pos += len;
    return nullptr;
}

struct FixedCodes {
    Huffman literal, distance;
    FixedCodes() {
        uint8_t length[FIXED_LITERAL_CODES];
        int s = 0;
        for (; s < 144; s++) length[s] = 8;
        for (; s < 256; s++) length[s] = 9;
        for (; s < 280; s++) length[s] = 7;
        for (; s < 288; s++) length[s] = 8;
        build(literal, length, FIXED_LITERAL_CODES);
        for (s = 0; s < MAX_DISTANCE_CODES; s++) length[s] = 5;
        build(distance, length, MAX_DISTANCE_CODES);
    }
};

const char* fixed_block(BitReader& in, std::vector<uint8_t>& out, size_t limit) {
    static const FixedCodes fixed;                            // built once, safely, on first use
    return run_codes(in, fixed.literal, fixed.distance, out, limit);
}

const char* dynamic_block(BitReader& in, std::vector<uint8_t>& out, size_t limit) {
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    const int n_literal = (int)in.bits(5) + 257;
    const int n_distance = (int)in.bits(5) + 1;
    const int n_code = (int)in.bits(4) + 4;
    if (in.ran_out) return "the compressed data ends too soon";
    if (n_literal > MAX_LITERAL_CODES || n_distance > MAX_DISTANCE_CODES) return "too many codes";

    uint8_t length[MAX_LITERAL_CODES + MAX_DISTANCE_CODES];
    int index = 0;
    for (; index < n_code; index++) length[ORDER[index]] = (uint8_t)in.bits(3);
    for (; index < 19; index++) length[ORDER[index]] = 0;
    if (in.ran_out) return "the compressed data ends too soon";

    Huffman length_code;
    if (build(length_code, length, 19) != 0) return "damaged code table";

    index = 0;
    while (index < n_literal + n_distance) {
        int symbol = decode(in, length_code);
        if (symbol < 0) return in.ran_out ? "the compressed data ends too soon" : "damaged code table";
        if (symbol < 16) {
            length[index++] = (uint8_t)symbol;
        } else {
            int repeat, value = 0;
            if (symbol == 16) {
                if (index == 0) return "damaged code table";
                value = length[index - 1];
                repeat = 3 + (int)in.bits(2);
            } else if (symbol == 17) {
                repeat = 3 + (int)in.bits(3);
            } else {
                repeat = 11 + (int)in.bits(7);
            }
            if (in.ran_out) return "the compressed data ends too soon";
            if (index + repeat > n_literal + n_distance) return "damaged code table";
            while (repeat--) length[index++] = (uint8_t)value;
        }
    }
    if (length[256] == 0) return "damaged code table";        // there must be an end-of-block code

    Huffman literal, distance;
    int err = build(literal, length, n_literal);
    if (err < 0 || (err > 0 && n_literal != literal.count[0] + literal.count[1])) return "damaged code table";
    err = build(distance, length + n_literal, n_distance);
    if (err < 0 || (err > 0 && n_distance != distance.count[0] + distance.count[1])) return "damaged code table";
    return run_codes(in, literal, distance, out, limit);
}

uint32_t adler32(const std::vector<uint8_t>& data) {
    uint32_t a = 1, b = 0;
    size_t i = 0;
    while (i < data.size()) {
        const size_t stop = std::min(data.size(), i + 5552);   // no overflow before the modulo
        for (; i < stop; i++) { a += data[i]; b += a; }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

}  // namespace

bool inflate_zlib(const uint8_t* data, size_t size, size_t expected_size,
                  std::vector<uint8_t>& out, std::string& error) {
    out.clear();
    if (size < 6) { error = "the compressed data is too short"; return false; }
    const int cmf = data[0], flags = data[1];
    if ((cmf & 0x0F) != 8 || (cmf >> 4) > 7) { error = "not zlib compressed data"; return false; }
    if ((cmf * 256 + flags) % 31 != 0) { error = "damaged zlib header"; return false; }
    if (flags & 0x20) { error = "zlib data with a preset dictionary is not supported"; return false; }

    out.reserve(expected_size);
    BitReader in{data + 2, size - 2};
    int last;
    do {
        last = (int)in.bits(1);
        const int type = (int)in.bits(2);
        if (in.ran_out) { error = "the compressed data ends too soon"; return false; }
        const char* problem;
        if (type == 0) problem = stored_block(in, out, expected_size);
        else if (type == 1) problem = fixed_block(in, out, expected_size);
        else if (type == 2) problem = dynamic_block(in, out, expected_size);
        else problem = "invalid block type";
        if (problem) { error = problem; out.clear(); return false; }
    } while (!last);

    if (out.size() != expected_size) { error = "the picture holds less data than its size needs"; out.clear(); return false; }
    if (in.pos + 4 > in.size) { error = "the compressed data ends too soon"; out.clear(); return false; }
    const uint8_t* tail = in.data + in.pos;
    const uint32_t stored = ((uint32_t)tail[0] << 24) | (tail[1] << 16) | (tail[2] << 8) | tail[3];
    if (stored != adler32(out)) { error = "the checksum of the picture data does not match"; out.clear(); return false; }
    return true;
}

}  // namespace vtw
