#pragma once

#include <optional>
#include <string>

namespace pinx::catalog {

// If |raw| is a TINFOIL-encrypted index, decrypt and decompress it to JSON
// using the private key embedded at romfs:/keys/private.key. Returns nullopt
// when |raw| is not in TINFOIL format (the caller then treats it as plain
// JSON) or when decryption fails.
//
// Format: "TINFOIL"(7) + flag(1) + sessionKey(256, RSA-2048-OAEP-SHA256) +
// size(8, u64 LE) + payload(AES-128-ECB). The flag's low nibble selects the
// inner compression (none / zstd / zlib).
std::optional<std::string> TryTinfoilDecrypt(const std::string &raw);

}
