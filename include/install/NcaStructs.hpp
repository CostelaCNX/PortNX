#pragma once

#include <cstdint>
#include <cstring>

namespace pinx::install {

// -- NCA constants -----------------------------------------------------------

static constexpr std::uint32_t kMagicNca3     = 0x3341434E; // "NCA3"
static constexpr std::size_t   kNcaHeaderSize = 0xC00;
static constexpr std::size_t   kNcaSectorSize = 0x200;

// -- PFS0 (used inside NSP) --------------------------------------------------

static constexpr std::uint32_t kMagicPfs0 = 0x30534650; // "PFS0"

struct Pfs0Header {
    std::uint32_t magic;
    std::uint32_t file_count;
    std::uint32_t string_table_size;
    std::uint32_t reserved;
} __attribute__((packed));

static_assert(sizeof(Pfs0Header) == 0x10, "Pfs0Header must be 0x10");

struct Pfs0FileEntry {
    std::uint64_t data_offset;
    std::uint64_t file_size;
    std::uint32_t string_table_offset;
    std::uint32_t reserved;
} __attribute__((packed));

static_assert(sizeof(Pfs0FileEntry) == 0x18, "Pfs0FileEntry must be 0x18");

// -- HFS0 (used inside XCI partitions) ---------------------------------------

static constexpr std::uint32_t kMagicHfs0 = 0x30534648; // "HFS0"

struct Hfs0Header {
    std::uint32_t magic;
    std::uint32_t file_count;
    std::uint32_t string_table_size;
    std::uint32_t reserved;
} __attribute__((packed));

static_assert(sizeof(Hfs0Header) == 0x10, "Hfs0Header must be 0x10");

struct Hfs0FileEntry {
    std::uint64_t data_offset;
    std::uint64_t file_size;
    std::uint32_t string_table_offset;
    std::uint32_t hashed_size;
    std::uint64_t reserved;
    std::uint8_t  hash[0x20];
} __attribute__((packed));

static_assert(sizeof(Hfs0FileEntry) == 0x40, "Hfs0FileEntry must be 0x40");

// -- XCI header (first 0x200 bytes of a cartridge image) ---------------------

static constexpr std::uint32_t kMagicXci   = 0x44414548; // "HEAD"
static constexpr std::uint32_t kXciMagicOff = 0x100;     // byte offset of magic within XCI
static constexpr std::uint32_t kXciHfs0Off  = 0x130;     // byte offset of root HFS0 offset field

// Only the fields we actually read; the full XCI header is 0x200 bytes.
struct XciHeaderFields {
    std::uint8_t  _sig[0x100];
    std::uint32_t magic;           // 0x100: "HEAD"
    std::uint32_t secure_start;    // 0x104: secure area (media units)
    std::uint32_t backup_start;    // 0x108
    std::uint8_t  _0x10c[4];
    std::uint64_t package_id;      // 0x110
    std::uint64_t valid_data_end;  // 0x118
    std::uint8_t  info_iv[0x10];   // 0x120
    std::uint64_t hfs0_offset;     // 0x130: root HFS0 offset in bytes
    std::uint64_t hfs0_size;       // 0x138
} __attribute__((packed));

static_assert(offsetof(XciHeaderFields, hfs0_offset) == 0x130,
              "XciHeaderFields::hfs0_offset must be at 0x130");

// -- NCA header ---------------------------------------------------------------

struct NcaSectionEntry {
    std::uint32_t media_start_offset;
    std::uint32_t media_end_offset;
    std::uint8_t  _0x8[0x8];
} __attribute__((packed));

static_assert(sizeof(NcaSectionEntry) == 0x10, "NcaSectionEntry must be 0x10");

struct NcaFsHeader {
    std::uint16_t version;
    std::uint8_t  partition_type;
    std::uint8_t  fs_type;
    std::uint8_t  crypt_type;
    std::uint8_t  _0x5[0x3];
    std::uint8_t  superblock_data[0x138];
    union {
        std::uint64_t section_ctr;
        struct {
            std::uint32_t section_ctr_low;
            std::uint32_t section_ctr_high;
        };
    };
    std::uint8_t  _0x148[0xB8];
} __attribute__((packed));

static_assert(sizeof(NcaFsHeader) == 0x200, "NcaFsHeader must be 0x200");

struct NcaHeader {
    std::uint8_t    fixed_key_sig[0x100]; // RSA-PSS signature over header with fixed key
    std::uint8_t    npdm_key_sig[0x100];  // RSA-PSS signature over header with NPDM key
    std::uint32_t   magic;
    std::uint8_t    distribution;         // system vs gamecard
    std::uint8_t    content_type;
    std::uint8_t    crypto_type;
    std::uint8_t    kaek_index;
    std::uint64_t   nca_size;
    std::uint64_t   title_id;
    std::uint32_t   content_index;
    union {
        std::uint32_t sdk_version;
        struct {
            std::uint8_t sdk_revision;
            std::uint8_t sdk_micro;
            std::uint8_t sdk_minor;
            std::uint8_t sdk_major;
        };
    };
    std::uint8_t    crypto_type2;
    std::uint8_t    crypto_type3;
    std::uint8_t    _0x222[0xE];
    std::uint64_t   rights_id[2];
    NcaSectionEntry section_entries[4];
    std::uint8_t    section_hashes[4][0x20];
    std::uint8_t    encrypted_keys[4][0x10];
    std::uint8_t    _0x340[0xC0];
    NcaFsHeader     fs_headers[4];
} __attribute__((packed));

static_assert(sizeof(NcaHeader) == 0xC00, "NcaHeader must be 0xC00");

// -- Packaged content meta (CNMT) header inside the meta NCA ------------------

struct PackagedContentMetaHeader {
    std::uint64_t title_id;
    std::uint32_t version;
    std::uint8_t  type;
    std::uint8_t  _0xd;
    std::uint16_t extended_header_size;
    std::uint16_t content_count;
    std::uint16_t content_meta_count;
    std::uint8_t  attributes;
    std::uint8_t  storage_id;
    std::uint8_t  install_type;
    bool          committed;
    std::uint32_t required_system_version;
    std::uint32_t _0x1c;
} __attribute__((packed));

static_assert(sizeof(PackagedContentMetaHeader) == 0x20, "PackagedContentMetaHeader must be 0x20");

// -- Utility ------------------------------------------------------------------

inline std::uint64_t GetBaseTitleId(std::uint64_t title_id, std::uint8_t content_meta_type) {
    // NcmContentMetaType_Application = 0x80, _Patch = 0x81, _AddOnContent = 0x82
    switch(content_meta_type) {
        case 0x80: return title_id;
        case 0x81: return title_id ^ 0x800;
        case 0x82: return (title_id ^ 0x1000) & ~static_cast<std::uint64_t>(0xFFF);
        default:   return title_id;
    }
}

}
