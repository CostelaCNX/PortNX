#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <string>

#include <install/CryptoUtils.hpp>
#include <install/NcmWrapper.hpp>

namespace pinx::install {

using NczProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;
using NczStopCallback     = std::function<bool()>;

using NczRangeReader = std::function<bool(
    std::uint64_t offset,
    std::uint64_t size,
    std::function<bool(const void *, std::size_t)> write_fn)>;

static constexpr std::uint64_t kNczSectionMagic = 0x4E544345535A434EULL;
static constexpr std::uint64_t kNczBlockMagic   = 0x4B434F4C425A434EULL;
static constexpr std::size_t   kNczModernPrefix  = 0x4000;
static constexpr std::size_t   kNczLegacyPrefix  = 0xC00;

bool IsCompressedNca(const std::string &container_path, std::uint64_t nca_offset);

std::string DecompressNczToTemp(const std::string &container_path,
                                 std::uint64_t nca_offset,
                                 std::uint64_t compressed_size,
                                 const std::string &temp_dir,
                                 NczProgressCallback progress_callback = {},
                                 NczStopCallback stop_callback = {});

bool DecompressNczToStorage(const std::string &container_path,
                             std::uint64_t nca_offset,
                             std::uint64_t compressed_size,
                             ContentStorage &storage,
                             const NcmContentId &nca_id,
                             const HeaderKey &header_key,
                             NczProgressCallback progress_callback = {},
                             NczStopCallback stop_callback = {});

bool DecompressNczFromRangeReaderToStorage(
    const NczRangeReader &reader,
    std::uint64_t entry_offset,
    std::uint64_t entry_size,
    ContentStorage &storage,
    const NcmContentId &nca_id,
    const HeaderKey &header_key,
    NczProgressCallback progress_callback = {},
    NczStopCallback stop_callback = {});

}
