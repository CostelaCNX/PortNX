#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <switch/services/ncm.h>

namespace pinx::install {

struct InstallProgress {
    std::uint64_t bytes_done        = 0;
    std::uint64_t bytes_total       = 0;
    std::string   current_nca;
    std::uint32_t nca_index         = 0;
    std::uint32_t nca_count         = 0;
    bool          decompressing     = false;
};

using ProgressCallback = std::function<void(const InstallProgress &)>;

struct InstallResult {
    bool          success = false;
    std::string   error_message;
    std::uint64_t title_id = 0;
};

struct InstallConfig {
    NcmStorageId dest_storage_id = NcmStorageId_SdCard;
    bool         ignore_req_fw   = true;
    bool         reinstall_ncas  = false;
};

InstallResult InstallFromLocalFile(const std::string &file_path,
                                    const InstallConfig &config,
                                    ProgressCallback progress = nullptr);

InstallResult InstallNsp(const std::string &file_path,
                          const InstallConfig &config,
                          ProgressCallback progress);

}
