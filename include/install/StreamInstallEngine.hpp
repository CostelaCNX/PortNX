#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <install/InstallEngine.hpp>
#include <net/HttpClient.hpp>

namespace pinx::install {

struct StreamInstallRequest {
    std::string         url;
    net::HttpOptions    http_opts;
    InstallConfig       install_config;
};

// Install an NSP/NSZ file directly from a URL without writing to the SD card.
// Uses HTTP Range requests to fetch each NCA entry independently, streaming
// bytes directly into NCM content storage.
InstallResult StreamInstallFromUrl(const StreamInstallRequest &req,
                                    ProgressCallback progress,
                                    std::function<bool()> stop_callback = {});

}
