#pragma once

#include <cstdint>

// Opaque curl typedefs so this header does not pull in <curl/curl.h>.
// These match libcurl's own defaults (no CURL_STRICTER), so including both
// headers in a translation unit is a no-op redefinition.
typedef void CURL;
typedef std::int32_t curl_socket_t;

namespace pinx::net {

// Initialize the global curl runtime exactly once. Returns false if it failed.
bool EnsureCurlGlobalInit();

// Returns a thread-local, reusable easy handle. The handle is reset on each
// call (options cleared) but keeps its connection cache, so repeated requests
// to the same host reuse the TCP/TLS connection. Returns nullptr if the global
// runtime could not init. NEVER pass this handle to curl_easy_cleanup — it
// lives until the thread terminates and is reclaimed at process exit.
//
// Every handle gets a default SOCKOPTFUNCTION that applies
// SO_LINGER { l_onoff=1, l_linger=0 } so process exit cannot hang on dead
// keep-alive peers when socketExit closes curl's leaked fds.
CURL *AcquireThreadCurlHandle();

// Apply the LINGER-0 socket option (the shutdown-safety baseline). Meant to be
// called from a custom CURLOPT_SOCKOPTFUNCTION. Returns CURL_SOCKOPT_OK (0).
int ApplyCurlLingerSockopt(curl_socket_t fd);

// Release bookkeeping for all acquired handles WITHOUT curl_easy_cleanup (which
// has been observed to segfault on tainted handles). Call on shutdown BEFORE
// socketExit().
void ShutdownCurl();

}
