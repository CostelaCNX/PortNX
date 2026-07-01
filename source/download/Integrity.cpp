#include <download/Integrity.hpp>

#include <cstdio>
#include <vector>

#include <mbedtls/sha256.h>

namespace pinx::download {

std::string Sha256File(const std::string &path) {
    std::FILE *fp = std::fopen(path.c_str(), "rb");
    if(fp == nullptr) {
        return "";
    }

    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts_ret(&ctx, 0);

    std::vector<unsigned char> buf(65536);
    size_t n;
    while((n = std::fread(buf.data(), 1, buf.size(), fp)) > 0) {
        mbedtls_sha256_update_ret(&ctx, buf.data(), n);
    }
    std::fclose(fp);

    unsigned char out[32];
    mbedtls_sha256_finish_ret(&ctx, out);
    mbedtls_sha256_free(&ctx);

    static const char *hexd = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for(int i = 0; i < 32; ++i) {
        hex.push_back(hexd[out[i] >> 4]);
        hex.push_back(hexd[out[i] & 0x0F]);
    }
    return hex;
}

}
