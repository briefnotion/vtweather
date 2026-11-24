// Inflate: undo zlib compression (RFC 1950 and 1951), written for reading PNG files that
// arrive over the network, so it trusts nothing. Every read is bounds-checked, the output can
// never grow past the size the caller expects, and a damaged or hostile stream gives an error,
// never a crash.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace vtw {

// Decompress a zlib stream. The result must be exactly expected_size bytes (a PNG knows how
// many bytes its picture takes). On failure returns false and says why in error.
bool inflate_zlib(const uint8_t* data, size_t size, size_t expected_size,
                  std::vector<uint8_t>& out, std::string& error);

}  // namespace vtw
