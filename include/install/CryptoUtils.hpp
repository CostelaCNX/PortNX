#pragma once

#include <cstddef>
#include <cstdint>

namespace pinx::install {

// Fixed RSA-2048 public modulus Nintendo uses to sign NCA headers (signature 0).
extern const unsigned char kNcaHeaderSignatureModulus[0x100];

// Key sources used to derive the NCA header key via the Security Processor.
extern const unsigned char kHeaderKekSource[0x10];
extern const unsigned char kHeaderKeySource[0x20];

struct HeaderKey {
    unsigned char key[0x20];  // 32-byte AES-128-XTS key (two 16-byte halves)
};

// Derive the NCA header decryption key at runtime via splCrypto. No prod.keys
// file is needed. Returns true on success.
bool DeriveHeaderKey(HeaderKey &out);

// AES-128-XTS decrypt/encrypt of an NCA header in place (sector size 0x200).
void DecryptNcaHeader(void *header, std::size_t length, const HeaderKey &key);
void EncryptNcaHeader(void *header, std::size_t length, const HeaderKey &key);

// Verify the RSA-2048-PSS signature in the first 0x100 bytes of an NCA header
// against the 0x200 bytes starting at NcaHeader::magic.
bool Rsa2048PssVerify(const void *data, std::size_t len,
                      const unsigned char *signature,
                      const unsigned char *modulus);

}
