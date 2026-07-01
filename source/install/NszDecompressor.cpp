#include <install/NszDecompressor.hpp>
#include <install/NcaStructs.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <sys/stat.h>
#include <vector>

#include <switch.h>

#if __has_include(<zstd.h>)
#define PINX_HAS_ZSTD 1
#include <zstd.h>
#else
#define PINX_HAS_ZSTD 0
#endif

namespace pinx::install {

struct NczSectionHeader {
    std::uint64_t magic;
    std::uint64_t section_count;
};

struct NczSectionEntry {
    std::uint64_t offset;
    std::uint64_t size;
    std::uint64_t crypto_type;
    std::uint8_t  padding[8];
    std::uint8_t  crypto_key[16];
    std::uint8_t  crypto_counter[16];
};

struct NczBlockHeader {
    std::uint64_t magic;
    std::uint8_t  version;
    std::uint8_t  type;
    std::uint8_t  padding;
    std::uint8_t  block_size_exponent;
    std::uint32_t total_blocks;
    std::uint64_t decompressed_size;
};

static_assert(sizeof(NczSectionEntry) == 64, "NczSectionEntry must be 64 bytes");
static_assert(sizeof(NczBlockHeader)  == 24, "NczBlockHeader must be 24 bytes");

static constexpr std::uint64_t kMaxNczSections = 200'000;

using SequentialWriteCallback = std::function<bool(const std::uint8_t *, std::size_t)>;

struct NczCryptoState {
    std::vector<NczSectionEntry> sections;
    const NczSectionEntry *current = nullptr;
    std::uint64_t current_end = 0;
    Aes128CtrContext ctr_ctx{};
    bool ctr_active = false;

    void SetupForOffset(std::uint64_t nca_offset) {
        current = nullptr;
        ctr_active = false;
        for(const auto &s : sections) {
            if(nca_offset >= s.offset && nca_offset < s.offset + s.size) {
                current = &s;
                current_end = s.offset + s.size;
                if(s.crypto_type >= 3) {
                    std::uint8_t counter[16];
                    std::memcpy(counter, s.crypto_counter, 8);
                    const std::uint64_t ctr_val = __builtin_bswap64(nca_offset >> 4);
                    std::memcpy(counter + 8, &ctr_val, 8);
                    aes128CtrContextCreate(&ctr_ctx, s.crypto_key, counter);
                    ctr_active = true;
                }
                break;
            }
        }
    }
};

static bool ReEncryptAndWrite(const SequentialWriteCallback &write_cb,
                               std::uint8_t *data, std::size_t size,
                               std::uint64_t &nca_offset, NczCryptoState &crypto) {
    std::size_t pos = 0;
    while(pos < size) {
        if(!crypto.current || nca_offset >= crypto.current_end) {
            crypto.SetupForOffset(nca_offset);
        }
        std::size_t chunk = size - pos;
        if(crypto.current) {
            const std::uint64_t rem = crypto.current_end - nca_offset;
            chunk = std::min<std::size_t>(chunk, static_cast<std::size_t>(rem));
        }
        if(crypto.ctr_active && chunk > 0) {
            aes128CtrCrypt(&crypto.ctr_ctx, data + pos, data + pos, chunk);
        }
        if(!write_cb(data + pos, chunk)) return false;
        pos += chunk;
        nca_offset += chunk;
    }
    return true;
}

class PlaceholderBufferedWriter {
    public:
        PlaceholderBufferedWriter(ContentStorage &storage, const NcmContentId &nca_id)
            : storage_(storage), nca_id_(nca_id) {
            buffer_.reserve(kBufferedWriteSize);
        }

        bool Write(const std::uint8_t *data, std::size_t size) {
            std::size_t offset = 0;
            while(offset < size) {
                const auto chunk = std::min<std::size_t>(
                    kBufferedWriteSize - buffer_.size(), size - offset);
                buffer_.insert(buffer_.end(), data + offset, data + offset + chunk);
                offset += chunk;
                if(buffer_.size() >= kBufferedWriteSize) {
                    if(!Flush()) return false;
                }
            }
            return true;
        }

        bool Flush() {
            if(buffer_.empty()) return true;
            if(!storage_.WritePlaceholder(nca_id_, write_offset_,
                                           buffer_.data(), buffer_.size())) {
                return false;
            }
            write_offset_ += buffer_.size();
            buffer_.clear();
            return true;
        }

        std::uint64_t GetWrittenSize() const {
            return write_offset_ + buffer_.size();
        }

    private:
        static constexpr std::size_t kBufferedWriteSize = 1024 * 1024;
        ContentStorage &storage_;
        NcmContentId nca_id_{};
        std::vector<std::uint8_t> buffer_;
        std::uint64_t write_offset_ = 0;
};

bool IsCompressedNca(const std::string &container_path, std::uint64_t nca_offset) {
    FILE *file = std::fopen(container_path.c_str(), "rb");
    if(!file) return false;
    for(const std::size_t prefix : { kNczModernPrefix, kNczLegacyPrefix }) {
        if(std::fseek(file, static_cast<long>(nca_offset + prefix), SEEK_SET) != 0) continue;
        std::uint64_t magic = 0;
        if(std::fread(&magic, sizeof(magic), 1, file) == 1 && magic == kNczSectionMagic) {
            std::fclose(file);
            return true;
        }
    }
    std::fclose(file);
    return false;
}

#if PINX_HAS_ZSTD

std::string DecompressNczToTemp(const std::string &container_path,
                                 std::uint64_t nca_offset,
                                 std::uint64_t compressed_size,
                                 const std::string &temp_dir,
                                 NczProgressCallback progress_callback,
                                 NczStopCallback stop_callback) {
    FILE *in = std::fopen(container_path.c_str(), "rb");
    if(!in) return {};

    const std::size_t max_prefix = std::min<std::uint64_t>(kNczModernPrefix, compressed_size);
    auto prefix_buf = std::make_unique<std::uint8_t[]>(max_prefix);

    std::fseek(in, static_cast<long>(nca_offset), SEEK_SET);
    if(std::fread(prefix_buf.get(), max_prefix, 1, in) != 1) {
        std::fclose(in);
        return {};
    }

    std::size_t ncz_header_offset = 0;
    std::size_t prefix_size = 0;
    NczSectionHeader sec_header{};

    for(const std::size_t candidate : { kNczModernPrefix, kNczLegacyPrefix }) {
        if(candidate + sizeof(NczSectionHeader) > compressed_size) continue;
        if(candidate + sizeof(NczSectionHeader) <= max_prefix) {
            std::memcpy(&sec_header, prefix_buf.get() + candidate, sizeof(sec_header));
        } else {
            std::fseek(in, static_cast<long>(nca_offset + candidate), SEEK_SET);
            if(std::fread(&sec_header, sizeof(sec_header), 1, in) != 1) continue;
        }
        if(sec_header.magic == kNczSectionMagic && sec_header.section_count > 0
            && sec_header.section_count <= kMaxNczSections) {
            ncz_header_offset = candidate;
            prefix_size = candidate;
            break;
        }
    }

    if(ncz_header_offset == 0 || prefix_size < kNcaHeaderSize) {
        std::fclose(in);
        return {};
    }

    NczCryptoState crypto;
    crypto.sections.resize(static_cast<std::size_t>(sec_header.section_count));
    const std::size_t sections_file_offset = ncz_header_offset + sizeof(NczSectionHeader);
    std::fseek(in, static_cast<long>(nca_offset + sections_file_offset), SEEK_SET);
    if(std::fread(crypto.sections.data(),
                   sizeof(NczSectionEntry) * crypto.sections.size(), 1, in) != 1) {
        std::fclose(in);
        return {};
    }

    NczBlockHeader block_header{};
    const bool read_block = (std::fread(&block_header, sizeof(block_header), 1, in) == 1);
    const std::uint32_t kMaxBlocksTemp =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(compressed_size / 4 + 1, 1'000'000));
    const bool is_block_based = read_block && block_header.magic == kNczBlockMagic
                                && block_header.total_blocks > 0
                                && block_header.total_blocks <= kMaxBlocksTemp
                                && block_header.block_size_exponent >= 14
                                && block_header.block_size_exponent <= 24;

    if(!is_block_based && read_block) {
        std::fseek(in, -static_cast<long>(sizeof(block_header)), SEEK_CUR);
    }

    mkdir(temp_dir.c_str(), 0755);
    const std::string temp_path = temp_dir + "/ncz_decompressed.nca";
    FILE *out = std::fopen(temp_path.c_str(), "wb");
    if(!out) { std::fclose(in); return {}; }

    if(std::fwrite(prefix_buf.get(), prefix_size, 1, out) != 1) {
        std::fclose(in);
        std::fclose(out);
        std::remove(temp_path.c_str());
        return {};
    }

    std::uint64_t nca_write_offset = prefix_size;
    bool ok = true;

    auto report_progress = [&](std::uint64_t absolute_file_offset) {
        if(!progress_callback) return;
        if(absolute_file_offset < nca_offset) absolute_file_offset = nca_offset;
        const std::uint64_t processed = std::min<std::uint64_t>(
            compressed_size, absolute_file_offset - nca_offset);
        progress_callback(processed, compressed_size);
    };
    report_progress(nca_offset + prefix_size);

    if(is_block_based) {
        std::vector<std::uint32_t> block_sizes(block_header.total_blocks);
        if(std::fread(block_sizes.data(),
                       sizeof(std::uint32_t) * block_header.total_blocks, 1, in) != 1) {
            ok = false;
        }

        const std::uint32_t block_size = 1u << block_header.block_size_exponent;
        auto comp_buf = std::make_unique<std::uint8_t[]>(block_size);
        auto dec_buf  = std::make_unique<std::uint8_t[]>(block_size);

        for(std::uint32_t i = 0; ok && i < block_header.total_blocks; i++) {
            if(stop_callback && stop_callback()) { ok = false; break; }
            const std::uint32_t comp_size = block_sizes[i];
            if(comp_size > block_size) { ok = false; break; }
            std::uint64_t expected_dec = block_size;
            if(i == block_header.total_blocks - 1) {
                const std::uint64_t rem = block_header.decompressed_size % block_size;
                if(rem != 0) expected_dec = rem;
            }
            if(std::fread(comp_buf.get(), comp_size, 1, in) != 1) { ok = false; break; }
            report_progress(static_cast<std::uint64_t>(std::ftell(in)));

            std::size_t dec_size = expected_dec;
            if(comp_size < expected_dec) {
                const std::size_t res = ZSTD_decompress(
                    dec_buf.get(), expected_dec, comp_buf.get(), comp_size);
                if(ZSTD_isError(res)) { ok = false; break; }
                dec_size = res;
            } else {
                dec_size = std::min<std::size_t>(comp_size, expected_dec);
                std::memcpy(dec_buf.get(), comp_buf.get(), dec_size);
            }
            ok = ReEncryptAndWrite(
                [&out](const std::uint8_t *data, std::size_t size) {
                    return std::fwrite(data, 1, size, out) == size;
                },
                dec_buf.get(), dec_size, nca_write_offset, crypto);
        }
    } else {
        const std::size_t zstd_in_size = ZSTD_DStreamInSize();
        const std::size_t zstd_out_size = ZSTD_DStreamOutSize();
        auto zstd_in  = std::make_unique<std::uint8_t[]>(zstd_in_size);
        auto zstd_out = std::make_unique<std::uint8_t[]>(zstd_out_size);
        ZSTD_DStream *dstream = ZSTD_createDStream();
        if(!dstream) {
            ok = false;
        } else {
            ZSTD_initDStream(dstream);
            bool frame_done = false;
            while(ok && !frame_done && !std::feof(in)) {
                if(stop_callback && stop_callback()) { ok = false; break; }
                const std::size_t n = std::fread(zstd_in.get(), 1, zstd_in_size, in);
                if(n == 0) break;
                report_progress(static_cast<std::uint64_t>(std::ftell(in)));
                ZSTD_inBuffer input = { zstd_in.get(), n, 0 };
                while(ok && !frame_done && input.pos < input.size) {
                    if(stop_callback && stop_callback()) { ok = false; break; }
                    ZSTD_outBuffer output = { zstd_out.get(), zstd_out_size, 0 };
                    const std::size_t ret = ZSTD_decompressStream(dstream, &output, &input);
                    if(output.pos > 0) {
                        ok = ReEncryptAndWrite(
                            [&out](const std::uint8_t *data, std::size_t size) {
                                return std::fwrite(data, 1, size, out) == size;
                            },
                            zstd_out.get(), output.pos, nca_write_offset, crypto);
                    }
                    if(ZSTD_isError(ret)) { ok = false; break; }
                    if(ret == 0) frame_done = true;
                }
            }
            ZSTD_freeDStream(dstream);
        }
    }

    std::fclose(in);
    std::fclose(out);
    if(!ok) { std::remove(temp_path.c_str()); return {}; }
    report_progress(nca_offset + compressed_size);
    return temp_path;
}

bool DecompressNczToStorage(const std::string &container_path,
                             std::uint64_t nca_offset,
                             std::uint64_t compressed_size,
                             ContentStorage &storage,
                             const NcmContentId &nca_id,
                             const HeaderKey &header_key,
                             NczProgressCallback progress_callback,
                             NczStopCallback stop_callback) {
    FILE *in = std::fopen(container_path.c_str(), "rb");
    if(!in) return false;

    const std::size_t max_prefix = std::min<std::uint64_t>(kNczModernPrefix, compressed_size);
    auto prefix_buf = std::make_unique<std::uint8_t[]>(max_prefix);
    std::fseek(in, static_cast<long>(nca_offset), SEEK_SET);
    if(std::fread(prefix_buf.get(), max_prefix, 1, in) != 1) { std::fclose(in); return false; }

    std::size_t ncz_header_offset = 0, prefix_size = 0;
    NczSectionHeader sec_header{};
    for(const std::size_t candidate : { kNczModernPrefix, kNczLegacyPrefix }) {
        if(candidate + sizeof(NczSectionHeader) > compressed_size) continue;
        if(candidate + sizeof(NczSectionHeader) <= max_prefix) {
            std::memcpy(&sec_header, prefix_buf.get() + candidate, sizeof(sec_header));
        } else {
            std::fseek(in, static_cast<long>(nca_offset + candidate), SEEK_SET);
            if(std::fread(&sec_header, sizeof(sec_header), 1, in) != 1) continue;
        }
        if(sec_header.magic == kNczSectionMagic && sec_header.section_count > 0
            && sec_header.section_count <= kMaxNczSections) {
            ncz_header_offset = candidate;
            prefix_size = candidate;
            break;
        }
    }
    if(ncz_header_offset == 0 || prefix_size < kNcaHeaderSize) { std::fclose(in); return false; }

    NcaHeader raw_header{};
    std::memcpy(&raw_header, prefix_buf.get(), kNcaHeaderSize);
    NcaHeader dec = raw_header;
    DecryptNcaHeader(&dec, kNcaHeaderSize, header_key);
    if(dec.magic != kMagicNca3 || dec.nca_size == 0) { std::fclose(in); return false; }

    const std::uint64_t nca_size = dec.nca_size;
    if(dec.distribution != 0) {
        dec.distribution = 0;
        EncryptNcaHeader(&dec, kNcaHeaderSize, header_key);
        std::memcpy(prefix_buf.get(), &dec, kNcaHeaderSize);
    }

    NczCryptoState crypto;
    crypto.sections.resize(static_cast<std::size_t>(sec_header.section_count));
    const std::size_t sections_file_offset = ncz_header_offset + sizeof(NczSectionHeader);
    std::fseek(in, static_cast<long>(nca_offset + sections_file_offset), SEEK_SET);
    if(std::fread(crypto.sections.data(),
                   sizeof(NczSectionEntry) * crypto.sections.size(), 1, in) != 1) {
        std::fclose(in);
        return false;
    }

    NczBlockHeader block_header{};
    const bool read_block = (std::fread(&block_header, sizeof(block_header), 1, in) == 1);
    const std::uint32_t kMaxBlocksStorage =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(compressed_size / 4 + 1, 1'000'000));
    const bool is_block_based = read_block && block_header.magic == kNczBlockMagic
                                && block_header.total_blocks > 0
                                && block_header.total_blocks <= kMaxBlocksStorage
                                && block_header.block_size_exponent >= 14
                                && block_header.block_size_exponent <= 24;
    if(!is_block_based && read_block) {
        std::fseek(in, -static_cast<long>(sizeof(block_header)), SEEK_CUR);
    }

    storage.DeletePlaceholder(nca_id);
    if(!storage.CreatePlaceholder(nca_id, nca_size)) { std::fclose(in); return false; }

    PlaceholderBufferedWriter writer(storage, nca_id);
    auto report_progress = [&](std::uint64_t absolute_file_offset) {
        if(!progress_callback) return;
        if(absolute_file_offset < nca_offset) absolute_file_offset = nca_offset;
        const std::uint64_t processed = std::min<std::uint64_t>(
            compressed_size, absolute_file_offset - nca_offset);
        progress_callback(processed, compressed_size);
    };

    bool ok = writer.Write(prefix_buf.get(), prefix_size);
    std::uint64_t nca_write_offset = prefix_size;
    report_progress(nca_offset + prefix_size);

    if(is_block_based) {
        std::vector<std::uint32_t> block_sizes(block_header.total_blocks);
        if(ok && std::fread(block_sizes.data(),
                            sizeof(std::uint32_t) * block_header.total_blocks, 1, in) != 1) {
            ok = false;
        }
        const std::uint32_t block_size = 1u << block_header.block_size_exponent;
        auto comp_buf = std::make_unique<std::uint8_t[]>(block_size);
        auto dec_buf  = std::make_unique<std::uint8_t[]>(block_size);

        for(std::uint32_t i = 0; ok && i < block_header.total_blocks; i++) {
            if(stop_callback && stop_callback()) { ok = false; break; }
            const std::uint32_t comp_size = block_sizes[i];
            if(comp_size > block_size) { ok = false; break; }
            std::uint64_t expected_dec = block_size;
            if(i == block_header.total_blocks - 1) {
                const std::uint64_t rem = block_header.decompressed_size % block_size;
                if(rem != 0) expected_dec = rem;
            }
            if(std::fread(comp_buf.get(), comp_size, 1, in) != 1) { ok = false; break; }
            report_progress(static_cast<std::uint64_t>(std::ftell(in)));
            std::size_t dec_size = expected_dec;
            if(comp_size < expected_dec) {
                const std::size_t res = ZSTD_decompress(
                    dec_buf.get(), expected_dec, comp_buf.get(), comp_size);
                if(ZSTD_isError(res)) { ok = false; break; }
                dec_size = res;
            } else {
                dec_size = std::min<std::size_t>(comp_size, expected_dec);
                std::memcpy(dec_buf.get(), comp_buf.get(), dec_size);
            }
            ok = ReEncryptAndWrite(
                [&writer](const std::uint8_t *data, std::size_t size) {
                    return writer.Write(data, size);
                },
                dec_buf.get(), dec_size, nca_write_offset, crypto);
        }
    } else {
        const std::size_t zstd_in_size = std::max<std::size_t>(ZSTD_DStreamInSize(), 1024 * 1024);
        const std::size_t zstd_out_size = std::max<std::size_t>(ZSTD_DStreamOutSize(), 1024 * 1024);
        auto zstd_in  = std::make_unique<std::uint8_t[]>(zstd_in_size);
        auto zstd_out = std::make_unique<std::uint8_t[]>(zstd_out_size);
        ZSTD_DStream *dstream = ZSTD_createDStream();
        if(!dstream) {
            ok = false;
        } else {
            ZSTD_initDStream(dstream);
            bool frame_done = false;
            while(ok && !frame_done && !std::feof(in)) {
                if(stop_callback && stop_callback()) { ok = false; break; }
                const std::size_t n = std::fread(zstd_in.get(), 1, zstd_in_size, in);
                if(n == 0) break;
                report_progress(static_cast<std::uint64_t>(std::ftell(in)));
                ZSTD_inBuffer input = { zstd_in.get(), n, 0 };
                while(ok && !frame_done && input.pos < input.size) {
                    if(stop_callback && stop_callback()) { ok = false; break; }
                    ZSTD_outBuffer output = { zstd_out.get(), zstd_out_size, 0 };
                    const std::size_t ret = ZSTD_decompressStream(dstream, &output, &input);
                    if(output.pos > 0) {
                        ok = ReEncryptAndWrite(
                            [&writer](const std::uint8_t *data, std::size_t size) {
                                return writer.Write(data, size);
                            },
                            zstd_out.get(), output.pos, nca_write_offset, crypto);
                    }
                    if(ZSTD_isError(ret)) { ok = false; break; }
                    if(ret == 0) frame_done = true;
                }
            }
            ZSTD_freeDStream(dstream);
        }
    }

    std::fclose(in);
    const bool flush_ok = writer.Flush();
    ok = ok && flush_ok;
    if(ok && writer.GetWrittenSize() != nca_size) ok = false;

    if(!ok) {
        storage.DeletePlaceholder(nca_id);
        storage.Delete(nca_id);
        return false;
    }

    storage.Register(nca_id, nca_id);
    storage.DeletePlaceholder(nca_id);
    report_progress(nca_offset + compressed_size);
    return true;
}

bool DecompressNczFromRangeReaderToStorage(
    const NczRangeReader &reader,
    std::uint64_t entry_offset,
    std::uint64_t entry_size,
    ContentStorage &storage,
    const NcmContentId &nca_id,
    const HeaderKey &header_key,
    NczProgressCallback progress_callback,
    NczStopCallback stop_callback) {

    const std::size_t max_prefix = static_cast<std::size_t>(
        std::min<std::uint64_t>(kNczModernPrefix, entry_size));
    std::vector<std::uint8_t> prefix_buf(max_prefix);

    auto ReadExact = [&reader](std::uint64_t abs_offset, std::size_t size, void *out) -> bool {
        auto *dst = static_cast<std::uint8_t *>(out);
        std::size_t got = 0;
        const bool ok = reader(abs_offset, static_cast<std::uint64_t>(size),
            [&](const void *d, std::size_t n) -> bool {
                std::memcpy(dst + got, d, n);
                got += n;
                return got <= size;
            });
        return ok && (got == size);
    };

    if(!ReadExact(entry_offset, max_prefix, prefix_buf.data())) return false;

    std::size_t ncz_hdr_off = 0, prefix_size = 0;
    NczSectionHeader sec_hdr{};
    for(const std::size_t cand : {kNczModernPrefix, kNczLegacyPrefix}) {
        if(cand + sizeof(NczSectionHeader) > entry_size) continue;
        if(cand + sizeof(NczSectionHeader) <= max_prefix) {
            std::memcpy(&sec_hdr, prefix_buf.data() + cand, sizeof(sec_hdr));
        } else {
            if(!ReadExact(entry_offset + cand, sizeof(sec_hdr), &sec_hdr)) continue;
        }
        if(sec_hdr.magic == kNczSectionMagic && sec_hdr.section_count > 0 &&
           sec_hdr.section_count <= kMaxNczSections) {
            ncz_hdr_off = cand;
            prefix_size = cand;
            break;
        }
    }
    if(ncz_hdr_off == 0 || prefix_size < kNcaHeaderSize) return false;

    NcaHeader raw_hdr{};
    std::memcpy(&raw_hdr, prefix_buf.data(), kNcaHeaderSize);
    NcaHeader dec_hdr = raw_hdr;
    DecryptNcaHeader(&dec_hdr, kNcaHeaderSize, header_key);
    if(dec_hdr.magic != kMagicNca3 || dec_hdr.nca_size == 0) return false;

    const std::uint64_t nca_size = dec_hdr.nca_size;
    if(dec_hdr.distribution != 0) {
        dec_hdr.distribution = 0;
        EncryptNcaHeader(&dec_hdr, kNcaHeaderSize, header_key);
        std::memcpy(prefix_buf.data(), &dec_hdr, kNcaHeaderSize);
    }

    NczCryptoState crypto;
    crypto.sections.resize(static_cast<std::size_t>(sec_hdr.section_count));
    {
        const std::size_t sz = sizeof(NczSectionEntry) * crypto.sections.size();
        const std::uint64_t off = entry_offset + ncz_hdr_off + sizeof(NczSectionHeader);
        if(!ReadExact(off, sz, crypto.sections.data())) return false;
    }

    const std::uint64_t sections_end_off =
        static_cast<std::uint64_t>(ncz_hdr_off) + sizeof(NczSectionHeader) +
        static_cast<std::uint64_t>(sec_hdr.section_count) * sizeof(NczSectionEntry);
    const std::uint64_t blk_hdr_abs = entry_offset + sections_end_off;

    NczBlockHeader blk_hdr{};
    bool have_blk_hdr = false;
    if(sections_end_off + sizeof(NczBlockHeader) <= entry_size) {
        have_blk_hdr = ReadExact(blk_hdr_abs, sizeof(NczBlockHeader), &blk_hdr);
    }

    const std::uint32_t kMaxBlocks =
        static_cast<std::uint32_t>(std::min<std::uint64_t>(entry_size / 4 + 1, 1'000'000));
    const bool is_block_based = have_blk_hdr
                                && (blk_hdr.magic == kNczBlockMagic)
                                && (blk_hdr.total_blocks > 0)
                                && (blk_hdr.total_blocks <= kMaxBlocks)
                                && (blk_hdr.block_size_exponent >= 14)
                                && (blk_hdr.block_size_exponent <= 24);

    storage.DeletePlaceholder(nca_id);
    if(!storage.CreatePlaceholder(nca_id, nca_size)) return false;

    PlaceholderBufferedWriter writer(storage, nca_id);
    bool ok = writer.Write(prefix_buf.data(), prefix_size);
    std::uint64_t nca_write_offset = prefix_size;
    if(progress_callback) progress_callback(0, entry_size);

    if(is_block_based) {
        const std::uint64_t sizes_abs = blk_hdr_abs + sizeof(NczBlockHeader);
        std::vector<std::uint32_t> blk_sizes(blk_hdr.total_blocks);
        if(ok && !ReadExact(sizes_abs, sizeof(std::uint32_t) * blk_hdr.total_blocks,
                             blk_sizes.data())) {
            ok = false;
        }
        if(ok) {
            const std::uint32_t block_sz = 1u << blk_hdr.block_size_exponent;
            const std::uint64_t blk_data_abs =
                sizes_abs + static_cast<std::uint64_t>(blk_hdr.total_blocks) * sizeof(std::uint32_t);
            const std::uint64_t body_size = (entry_offset + entry_size) - blk_data_abs;

            std::vector<std::uint8_t> block_accum;
            block_accum.reserve(block_sz);
            auto dec_buf = std::make_unique<std::uint8_t[]>(block_sz);
            std::uint32_t cur_blk = 0;
            std::uint64_t body_received = 0;

            const bool rd_ok = reader(blk_data_abs, body_size,
                [&](const void *d, std::size_t n) -> bool {
                    if(!ok) return false;
                    if(cur_blk >= blk_hdr.total_blocks) return true;
                    if(stop_callback && stop_callback()) { ok = false; return false; }

                    const std::uint8_t *src = static_cast<const std::uint8_t*>(d);
                    std::size_t remaining = n;
                    body_received += n;

                    while(ok && remaining > 0 && cur_blk < blk_hdr.total_blocks) {
                        if(blk_sizes[cur_blk] > block_sz) { ok = false; break; }
                        const std::uint32_t need =
                            blk_sizes[cur_blk] - static_cast<std::uint32_t>(block_accum.size());
                        const std::size_t take = std::min<std::size_t>(remaining, need);
                        block_accum.insert(block_accum.end(), src, src + take);
                        src += take;
                        remaining -= take;
                        if(static_cast<std::uint32_t>(block_accum.size()) < blk_sizes[cur_blk])
                            break;

                        const std::uint32_t comp_sz = blk_sizes[cur_blk];
                        std::uint64_t exp_dec = block_sz;
                        if(cur_blk == blk_hdr.total_blocks - 1) {
                            const std::uint64_t rem = blk_hdr.decompressed_size % block_sz;
                            if(rem != 0) exp_dec = rem;
                        }
                        std::size_t dec_sz = exp_dec;
                        if(comp_sz < exp_dec) {
                            const std::size_t res = ZSTD_decompress(
                                dec_buf.get(), exp_dec, block_accum.data(), comp_sz);
                            if(ZSTD_isError(res)) { ok = false; break; }
                            dec_sz = res;
                        } else {
                            dec_sz = std::min<std::size_t>(comp_sz, exp_dec);
                            std::memcpy(dec_buf.get(), block_accum.data(), dec_sz);
                        }
                        ok = ReEncryptAndWrite(
                            [&writer](const std::uint8_t *p, std::size_t sz) {
                                return writer.Write(p, sz);
                            },
                            dec_buf.get(), dec_sz, nca_write_offset, crypto);
                        block_accum.clear();
                        cur_blk++;
                        if(progress_callback) {
                            const std::uint64_t done = std::min(
                                blk_data_abs - entry_offset + body_received, entry_size);
                            progress_callback(done, entry_size);
                        }
                    }
                    return ok;
                });
            ok = ok && rd_ok && (cur_blk == blk_hdr.total_blocks);
        }
    } else {
        const std::uint64_t stream_abs  = entry_offset + sections_end_off;
        const std::uint64_t stream_size = (entry_offset + entry_size) - stream_abs;
        ZSTD_DStream *dstream = ZSTD_createDStream();
        if(!dstream) {
            ok = false;
        } else {
            ZSTD_initDStream(dstream);
            const std::size_t out_sz = std::max<std::size_t>(ZSTD_DStreamOutSize(), 1024 * 1024);
            auto out_buf = std::make_unique<std::uint8_t[]>(out_sz);
            bool frame_done = false;
            std::uint64_t stream_received = 0;
            const bool rd_ok = reader(stream_abs, stream_size,
                [&](const void *d, std::size_t n) -> bool {
                    if(!ok) return false;
                    if(frame_done) return true;
                    if(stop_callback && stop_callback()) { ok = false; return false; }
                    ZSTD_inBuffer in_b = {d, n, 0};
                    while(ok && !frame_done && in_b.pos < in_b.size) {
                        ZSTD_outBuffer out_b = {out_buf.get(), out_sz, 0};
                        const std::size_t ret = ZSTD_decompressStream(dstream, &out_b, &in_b);
                        if(out_b.pos > 0) {
                            ok = ReEncryptAndWrite(
                                [&writer](const std::uint8_t *data, std::size_t len) {
                                    return writer.Write(data, len);
                                },
                                out_buf.get(), out_b.pos, nca_write_offset, crypto);
                        }
                        if(ZSTD_isError(ret)) { ok = false; }
                        if(ret == 0) frame_done = true;
                    }
                    stream_received += n;
                    if(progress_callback) {
                        const std::uint64_t done = std::min(
                            sections_end_off + stream_received, entry_size);
                        progress_callback(done, entry_size);
                    }
                    return ok;
                });
            ZSTD_freeDStream(dstream);
            ok = ok && rd_ok;
        }
    }

    const bool flush_ok = writer.Flush();
    ok = ok && flush_ok;
    if(ok && writer.GetWrittenSize() != nca_size) ok = false;

    if(!ok) {
        storage.DeletePlaceholder(nca_id);
        storage.Delete(nca_id);
        return false;
    }
    storage.Register(nca_id, nca_id);
    storage.DeletePlaceholder(nca_id);
    if(progress_callback) progress_callback(entry_size, entry_size);
    return true;
}

#else

std::string DecompressNczToTemp(const std::string &, std::uint64_t, std::uint64_t,
                                 const std::string &, NczProgressCallback, NczStopCallback) {
    return {};
}
bool DecompressNczToStorage(const std::string &, std::uint64_t, std::uint64_t,
                             ContentStorage &, const NcmContentId &, const HeaderKey &,
                             NczProgressCallback, NczStopCallback) {
    return false;
}
bool DecompressNczFromRangeReaderToStorage(
    const NczRangeReader &, std::uint64_t, std::uint64_t,
    ContentStorage &, const NcmContentId &, const HeaderKey &,
    NczProgressCallback, NczStopCallback) {
    return false;
}

#endif

}
