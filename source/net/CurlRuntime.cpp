#include <net/CurlRuntime.hpp>

#include <mutex>
#include <vector>

#include <sys/socket.h>

#include <curl/curl.h>

namespace pinx::net {
namespace {

std::once_flag g_curl_once;
bool g_curl_initialized = false;

thread_local CURL *t_reusable_handle = nullptr;

std::mutex &HandlesMutex() {
    static std::mutex m;
    return m;
}

std::vector<CURL *> &AllHandles() {
    static std::vector<CURL *> v;
    return v;
}

extern "C" int BaseSockopts(void *, curl_socket_t fd, curlsocktype) {
    struct linger lin;
    lin.l_onoff  = 1;
    lin.l_linger = 0;
    setsockopt(static_cast<int>(fd), SOL_SOCKET, SO_LINGER, &lin, sizeof(lin));
    return CURL_SOCKOPT_OK;
}

}

bool EnsureCurlGlobalInit() {
    std::call_once(g_curl_once, []() {
        g_curl_initialized = (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    });
    return g_curl_initialized;
}

CURL *AcquireThreadCurlHandle() {
    if(!EnsureCurlGlobalInit()) {
        return nullptr;
    }

    if(t_reusable_handle == nullptr) {
        t_reusable_handle = curl_easy_init();
        if(t_reusable_handle != nullptr) {
            std::lock_guard<std::mutex> lock(HandlesMutex());
            AllHandles().push_back(t_reusable_handle);
        }
    }
    else {
        curl_easy_reset(t_reusable_handle);
    }

    if(t_reusable_handle != nullptr) {
        curl_easy_setopt(t_reusable_handle, CURLOPT_SOCKOPTFUNCTION, BaseSockopts);
        curl_easy_setopt(t_reusable_handle, CURLOPT_SOCKOPTDATA, nullptr);
    }
    return t_reusable_handle;
}

int ApplyCurlLingerSockopt(curl_socket_t fd) {
    return BaseSockopts(nullptr, fd, CURLSOCKTYPE_IPCXN);
}

void ShutdownCurl() {
    std::vector<CURL *> leaked;
    {
        std::lock_guard<std::mutex> lock(HandlesMutex());
        leaked.swap(AllHandles());
    }
    (void)leaked;
}

}
