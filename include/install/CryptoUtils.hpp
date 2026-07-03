#pragma once

#include <cstddef>
#include <cstdint>

namespace pinx::install {

extern const unsigned char kNcaHeaderSignatureModulus[0x100];
extern const unsigned char kHeaderKekSource[0x10];
extern const unsigned char kHeaderKeySource[0x20];

struct HeaderKey {
    unsigned char key[0x20];
};

bool DeriveHeaderKey(HeaderKey &out);
void DecryptNcaHeader(void *header, std::size_t length, const HeaderKey &key);
void EncryptNcaHeader(void *header, std::size_t length, const HeaderKey &key);

}
