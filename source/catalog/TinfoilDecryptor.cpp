#include <catalog/TinfoilDecryptor.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include <switch.h>
#include <zlib.h>
#include <zstd.h>

#include <mbedtls/aes.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/rsa.h>

namespace pinx::catalog {
namespace {

constexpr const char   kMagic[]         = "TINFOIL";
constexpr std::size_t  kMagicLen        = 7;
constexpr std::uint8_t kCompMaskLow     = 0x0F;
constexpr std::uint8_t kCompNone        = 0x00;
constexpr std::uint8_t kCompZstd        = 0x0D;
constexpr std::uint8_t kCompZlib        = 0x0E;
constexpr std::size_t  kSessionKeyOff   = 8;
constexpr std::size_t  kSessionKeySize  = 256;
constexpr std::size_t  kSizeOffset      = kSessionKeyOff + kSessionKeySize;
constexpr std::size_t  kSizeFieldLen    = 8;
constexpr std::size_t  kPayloadOffset   = kSizeOffset + kSizeFieldLen;
constexpr std::size_t  kAesKeyLen       = 16;
constexpr std::uint64_t kMaxJsonSize    = 64u * 1024u * 1024u;

static const unsigned char kPrivateKey[] =
#include "TinfoilPrivateKey.inc"
;

int KernelEntropy(void * /*ctx*/, unsigned char *output, std::size_t len) {
    randomGet(output, len);
    return 0;
}

bool UnwrapSessionKey(const std::uint8_t *session_key,
                      std::uint8_t        out_aes_key[kAesKeyLen]) {
    mbedtls_pk_context       pk;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_init(&pk);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    bool ok = false;
    do {
        static const unsigned char kPers[] = "pinx_index_oaep";
        if(mbedtls_ctr_drbg_seed(&ctr_drbg, KernelEntropy, nullptr,
                                 kPers, sizeof(kPers) - 1) != 0) break;

        if(mbedtls_pk_parse_key(&pk, kPrivateKey, sizeof(kPrivateKey),
                                nullptr, 0) != 0) break;

        if(mbedtls_pk_get_type(&pk) != MBEDTLS_PK_RSA) break;

        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

        std::size_t out_len = 0;
        if(mbedtls_rsa_rsaes_oaep_decrypt(rsa,
                                          mbedtls_ctr_drbg_random, &ctr_drbg,
                                          MBEDTLS_RSA_PRIVATE,
                                          nullptr, 0,
                                          &out_len,
                                          session_key,
                                          out_aes_key,
                                          kAesKeyLen) != 0) break;
        ok = (out_len == kAesKeyLen);
    } while(false);

    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return ok;
}

bool AesEcbDecrypt(const std::uint8_t        *key,
                   const std::uint8_t        *ciphertext,
                   std::size_t                cipher_len,
                   std::vector<std::uint8_t> &out) {
    if(cipher_len == 0 || cipher_len % 16 != 0) return false;
    out.resize(cipher_len);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    bool ok = (mbedtls_aes_setkey_dec(&aes, key, 128) == 0);
    for(std::size_t i = 0; i < cipher_len && ok; i += 16)
        ok = (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                                    ciphertext + i, out.data() + i) == 0);
    mbedtls_aes_free(&aes);
    return ok;
}

bool ZlibInflate(const std::uint8_t *compressed, std::size_t compressed_size,
                 std::string &out_json) {
    z_stream stream{};
    stream.next_in  = const_cast<Bytef *>(compressed);
    stream.avail_in = static_cast<uInt>(compressed_size);
    if(inflateInit(&stream) != Z_OK) return false;

    std::string result;
    std::vector<char> chunk(65536);
    int ret = Z_OK;
    while(ret == Z_OK || ret == Z_BUF_ERROR) {
        stream.next_out  = reinterpret_cast<Bytef *>(chunk.data());
        stream.avail_out = static_cast<uInt>(chunk.size());
        ret = inflate(&stream, Z_NO_FLUSH);
        const std::size_t produced = chunk.size() - stream.avail_out;
        if(produced > 0) result.append(chunk.data(), produced);
        if(result.size() > kMaxJsonSize) { inflateEnd(&stream); return false; }
    }
    inflateEnd(&stream);
    if(ret != Z_STREAM_END) return false;
    out_json = std::move(result);
    return true;
}

bool ZstdInflate(const std::uint8_t *compressed, std::size_t compressed_size,
                 std::string &out_json) {
    if(compressed_size == 0) return false;
    const unsigned long long capacity =
        ZSTD_getFrameContentSize(compressed, compressed_size);
    if(capacity == ZSTD_CONTENTSIZE_ERROR ||
       capacity == ZSTD_CONTENTSIZE_UNKNOWN ||
       capacity > kMaxJsonSize) return false;
    out_json.resize(static_cast<std::size_t>(capacity));
    const std::size_t result =
        ZSTD_decompress(out_json.data(), out_json.size(), compressed, compressed_size);
    if(ZSTD_isError(result)) return false;
    out_json.resize(result);
    return true;
}

}

std::optional<std::string> TryTinfoilDecrypt(const std::string &raw) {
    if(raw.size() < kPayloadOffset) return std::nullopt;
    if(std::memcmp(raw.data(), kMagic, kMagicLen) != 0) return std::nullopt;

    const auto flag     = static_cast<std::uint8_t>(raw[kMagicLen]);
    const std::uint8_t comp = flag & kCompMaskLow;
    if(comp != kCompNone && comp != kCompZlib && comp != kCompZstd)
        return std::nullopt;

    std::uint64_t inner_size = 0;
    std::memcpy(&inner_size, raw.data() + kSizeOffset, kSizeFieldLen);

    const auto *payload =
        reinterpret_cast<const std::uint8_t *>(raw.data() + kPayloadOffset);
    const std::size_t payload_size = raw.size() - kPayloadOffset;
    if(inner_size == 0 || inner_size > payload_size || inner_size > kMaxJsonSize)
        return std::nullopt;

    const auto *session_key =
        reinterpret_cast<const std::uint8_t *>(raw.data() + kSessionKeyOff);
    std::uint8_t aes_key[kAesKeyLen] = {};
    if(!UnwrapSessionKey(session_key, aes_key)) return std::nullopt;

    std::vector<std::uint8_t> decrypted;
    const bool dec_ok = AesEcbDecrypt(aes_key, payload, payload_size, decrypted);
    mbedtls_platform_zeroize(aes_key, sizeof(aes_key));
    if(!dec_ok) return std::nullopt;

    decrypted.resize(static_cast<std::size_t>(inner_size));

    std::string out_json;
    switch(comp) {
        case kCompNone:
            out_json.assign(reinterpret_cast<const char *>(decrypted.data()),
                            decrypted.size());
            break;
        case kCompZlib:
            if(!ZlibInflate(decrypted.data(), decrypted.size(), out_json))
                return std::nullopt;
            break;
        case kCompZstd:
            if(!ZstdInflate(decrypted.data(), decrypted.size(), out_json))
                return std::nullopt;
            break;
        default:
            return std::nullopt;
    }

    return out_json;
}

}
