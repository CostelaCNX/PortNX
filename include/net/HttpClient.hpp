#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pinx::net {

struct HttpResponse {
    bool        success     = false;
    long        status_code = 0;
    std::string body;
    std::string error;   // human-readable failure reason when !success
};

struct HttpOptions {
    // TLS verification is off by default: the console clock is often wrong
    // and there is no system CA store. Enable to use romfs:/certs/cacert.pem.
    bool verify_tls = false;

    std::string username;   // optional HTTP Basic auth
    std::string password;

    long timeout_ms         = 30000;
    long connect_timeout_ms = 15000;

    std::vector<std::string> extra_headers;  // e.g. "Accept: application/json"

    std::function<bool()> cancel;  // return true to abort the transfer
};

// Blocking HTTP(S) GET that accumulates the response body in memory.
// Follows redirects. Reuses the calling thread's curl handle (see CurlRuntime).
HttpResponse Get(const std::string &url, const HttpOptions &opts = {});

struct DownloadProgress {
    std::uint64_t downloaded = 0;
    std::uint64_t total      = 0;  // 0 when the server does not report a length
};

struct DownloadResult {
    bool          success     = false;
    long          status_code = 0;
    std::uint64_t bytes       = 0;
    std::string   error;
};

// Streams an HTTP(S) GET to |dest_path|. If the file already exists with bytes,
// the transfer resumes from its end via a Range request (append mode). Reports
// progress and honors cancellation (return true from |on_cancel| to abort).
// Meant to run on a worker thread — it blocks for the whole transfer.
DownloadResult Download(const std::string &url,
                        const std::string &dest_path,
                        const HttpOptions &opts,
                        std::function<void(const DownloadProgress &)> on_progress = {},
                        std::function<bool()> on_cancel = {});

struct StreamResult {
    bool          success      = false;
    long          status_code  = 0;
    std::uint64_t bytes        = 0;
    std::string   error;
};

// Issues a Range GET for bytes [offset, offset+size) and calls |write_fn| with
// each received chunk. If size==0 fetches the full resource (no Range header).
// Returns false from write_fn to abort early. Blocks for the whole transfer.
StreamResult HttpStreamRange(
    const std::string &url,
    std::uint64_t offset,
    std::uint64_t size,
    std::function<bool(const void *, std::size_t)> write_fn,
    const HttpOptions &opts = {},
    std::function<void(std::uint64_t, std::uint64_t)> on_progress = {});

}
