#include <net/HttpClient.hpp>
#include <net/CurlRuntime.hpp>

#include <cstdio>
#include <sstream>
#include <sys/stat.h>

#include <curl/curl.h>

namespace pinx::net {
namespace {

size_t WriteToString(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *out = reinterpret_cast<std::string *>(userdata);
    if(out == nullptr) {
        return 0;
    }
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

struct CancelControl {
    std::function<bool()> cb;
};

int XferCancel(void *userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto *c = reinterpret_cast<CancelControl *>(userdata);
    if((c == nullptr) || !c->cb) {
        return 0;
    }
    return c->cb() ? 1 : 0;
}

}

HttpResponse Get(const std::string &url, const HttpOptions &opts) {
    HttpResponse res;

    CURL *curl = AcquireThreadCurlHandle();
    if(curl == nullptr) {
        res.error = "curl init failed";
        return res;
    }

    std::string body;
    struct curl_slist *headers = nullptr;
    for(const auto &h : opts.extra_headers) {
        headers = curl_slist_append(headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PortNX");
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, opts.timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opts.connect_timeout_ms);

    if(opts.verify_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/certs/cacert.pem");
    }
    else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if(headers != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    if(!opts.username.empty() || !opts.password.empty()) {
        const std::string basic = opts.username + ":" + opts.password;
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, basic.c_str());
    }

    CancelControl cancel_ctrl{opts.cancel};
    if(cancel_ctrl.cb) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, XferCancel);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancel_ctrl);
    }

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status_code);

    if((rc == CURLE_OK) && (res.status_code >= 200) && (res.status_code < 400)) {
        res.success = true;
        res.body = std::move(body);
    }
    else {
        std::ostringstream err;
        if(rc == CURLE_ABORTED_BY_CALLBACK) {
            err << "canceled";
        }
        else if(rc != CURLE_OK) {
            err << "transfer failed (" << curl_easy_strerror(rc) << ")";
        }
        else {
            err << "HTTP " << res.status_code;
        }
        res.error = err.str();
        res.body = std::move(body);
    }

    if(headers != nullptr) {
        curl_slist_free_all(headers);
    }
    return res;
}

namespace {

size_t WriteToFile(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *fp = reinterpret_cast<std::FILE *>(userdata);
    return std::fwrite(ptr, size, nmemb, fp);
}

struct DownloadControl {
    std::function<void(const DownloadProgress &)> on_progress;
    std::function<bool()>                         on_cancel;
    std::uint64_t                                 base;
};

int DownloadXfer(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto *c = reinterpret_cast<DownloadControl *>(userdata);
    if(c->on_progress) {
        DownloadProgress p;
        p.downloaded = c->base + (dlnow > 0 ? static_cast<std::uint64_t>(dlnow) : 0);
        p.total      = (dltotal > 0) ? c->base + static_cast<std::uint64_t>(dltotal) : 0;
        c->on_progress(p);
    }
    return (c->on_cancel && c->on_cancel()) ? 1 : 0;
}

}

DownloadResult Download(const std::string &url,
                        const std::string &dest_path,
                        const HttpOptions &opts,
                        std::function<void(const DownloadProgress &)> on_progress,
                        std::function<bool()> on_cancel) {
    DownloadResult res;

    CURL *curl = AcquireThreadCurlHandle();
    if(curl == nullptr) {
        res.error = "curl init failed";
        return res;
    }

    std::uint64_t resume_from = 0;
    {
        struct stat st;
        if(::stat(dest_path.c_str(), &st) == 0 && st.st_size > 0) {
            resume_from = static_cast<std::uint64_t>(st.st_size);
        }
    }

    std::FILE *fp = std::fopen(dest_path.c_str(), resume_from ? "ab" : "wb");
    if(fp == nullptr) {
        res.error = "cannot open destination file";
        return res;
    }

    struct curl_slist *headers = nullptr;
    for(const auto &h : opts.extra_headers) {
        headers = curl_slist_append(headers, h.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PortNX");
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opts.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    if(resume_from > 0) {
        curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(resume_from));
    }

    if(opts.verify_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/certs/cacert.pem");
    }
    else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if(headers != nullptr) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }
    if(!opts.username.empty() || !opts.password.empty()) {
        const std::string basic = opts.username + ":" + opts.password;
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, basic.c_str());
    }

    DownloadControl ctrl{std::move(on_progress), std::move(on_cancel), resume_from};
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, DownloadXfer);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctrl);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status_code);
    std::fclose(fp);

    if(headers != nullptr) {
        curl_slist_free_all(headers);
    }

    {
        struct stat st;
        if(::stat(dest_path.c_str(), &st) == 0) {
            res.bytes = static_cast<std::uint64_t>(st.st_size);
        }
    }

    if(rc == CURLE_OK && (res.status_code == 200 || res.status_code == 206)) {
        res.success = true;
    }
    else {
        std::ostringstream err;
        if(rc == CURLE_ABORTED_BY_CALLBACK) {
            err << "canceled";
        }
        else if(rc != CURLE_OK) {
            err << "transfer failed (" << curl_easy_strerror(rc) << ")";
        }
        else {
            err << "HTTP " << res.status_code;
        }
        res.error = err.str();
    }

    return res;
}

namespace {

struct StreamControl {
    std::function<bool(const void *, std::size_t)>    write_fn;
    std::function<void(std::uint64_t, std::uint64_t)> on_progress;
    std::function<bool()>                             on_cancel;
    std::uint64_t                                     received = 0;
    bool                                              write_failed = false;
};

size_t StreamWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *c = reinterpret_cast<StreamControl *>(userdata);
    const std::size_t total = size * nmemb;
    if(c->write_failed) return 0;
    if(!c->write_fn(ptr, total)) {
        c->write_failed = true;
        return 0;
    }
    c->received += total;
    return total;
}

int StreamXfer(void *userdata, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto *c = reinterpret_cast<StreamControl *>(userdata);
    if(c->on_progress) {
        c->on_progress(c->received,
                       dltotal > 0 ? static_cast<std::uint64_t>(dltotal) : 0);
    }
    return (c->on_cancel && c->on_cancel()) ? 1 : 0;
}

}

StreamResult HttpStreamRange(const std::string &url,
                              std::uint64_t offset,
                              std::uint64_t size,
                              std::function<bool(const void *, std::size_t)> write_fn,
                              const HttpOptions &opts,
                              std::function<void(std::uint64_t, std::uint64_t)> on_progress) {
    StreamResult res;

    CURL *curl = AcquireThreadCurlHandle();
    if(!curl) { res.error = "curl init failed"; return res; }

    struct curl_slist *headers = nullptr;
    for(const auto &h : opts.extra_headers) {
        headers = curl_slist_append(headers, h.c_str());
    }

    if(size > 0) {
        char range_buf[64];
        std::snprintf(range_buf, sizeof(range_buf), "%llu-%llu",
                      static_cast<unsigned long long>(offset),
                      static_cast<unsigned long long>(offset + size - 1));
        curl_easy_setopt(curl, CURLOPT_RANGE, range_buf);
    } else if(offset > 0) {
        char range_buf[32];
        std::snprintf(range_buf, sizeof(range_buf), "%llu-",
                      static_cast<unsigned long long>(offset));
        curl_easy_setopt(curl, CURLOPT_RANGE, range_buf);
    }

    StreamControl ctrl{ std::move(write_fn), std::move(on_progress), opts.cancel };

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PortNX");
    curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, StreamWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctrl);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, opts.connect_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);

    if(opts.verify_tls) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_CAINFO, "romfs:/certs/cacert.pem");
    } else {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if(headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if(!opts.username.empty() || !opts.password.empty()) {
        const std::string basic = opts.username + ":" + opts.password;
        curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
        curl_easy_setopt(curl, CURLOPT_USERPWD, basic.c_str());
    }

    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, StreamXfer);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctrl);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &res.status_code);
    if(headers) curl_slist_free_all(headers);
    curl_easy_setopt(curl, CURLOPT_RANGE, nullptr);

    res.bytes = ctrl.received;

    if(ctrl.write_failed) {
        res.error = "write callback aborted";
        return res;
    }
    if(rc == CURLE_ABORTED_BY_CALLBACK) {
        res.error = "canceled";
        return res;
    }
    if(rc != CURLE_OK) {
        res.error = curl_easy_strerror(rc);
        return res;
    }
    if(res.status_code == 200 || res.status_code == 206) {
        res.success = true;
    } else {
        std::ostringstream err;
        err << "HTTP " << res.status_code;
        res.error = err.str();
    }
    return res;
}

}
