#include <install/StreamInstallEngine.hpp>
#include <install/ContentMeta.hpp>
#include <install/CryptoUtils.hpp>
#include <install/NcaStructs.hpp>
#include <install/NcmWrapper.hpp>
#include <install/NszDecompressor.hpp>
#include <install/Pfs0Parser.hpp>
#include <net/HttpClient.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

#include <switch.h>

namespace pinx::install {
namespace {

constexpr std::size_t kPrefetchSize  = 65536;
constexpr std::size_t kNcmWriteSize  = 1024 * 1024;
constexpr int         kMaxRetries    = 3;
constexpr int         kRetryBaseMs   = 1500;

struct RemoteEntry {
    std::string   name;
    std::uint64_t offset;
    std::uint64_t size;
};

template<typename Fn>
net::StreamResult WithRetry(Fn fn, std::function<bool()> stop_cb) {
    net::StreamResult r;
    for(int attempt = 0; attempt <= kMaxRetries; attempt++) {
        if(stop_cb && stop_cb()) { r.error = "canceled"; return r; }
        r = fn();
        if(r.success) return r;
        if(attempt < kMaxRetries) {
            const int delay_ms = kRetryBaseMs * (1 << attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    return r;
}

NczRangeReader MakeHttpRangeReader(const StreamInstallRequest &req,
                                    std::function<bool()> stop_cb) {
    return [req, stop_cb](std::uint64_t offset, std::uint64_t size,
                          std::function<bool(const void *, std::size_t)> write_fn) -> bool {
        net::HttpOptions opts = req.http_opts;
        opts.cancel = stop_cb;
        const auto r = WithRetry([&] {
            return net::HttpStreamRange(req.url, offset, size, write_fn, opts);
        }, stop_cb);
        return r.success;
    };
}

bool RangeRead(const StreamInstallRequest &req, std::uint64_t offset, std::uint64_t size,
               std::vector<std::uint8_t> &out, std::string *out_error = nullptr) {
    net::StreamResult last;
    const auto r = WithRetry([&]() -> net::StreamResult {
        out.assign(static_cast<std::size_t>(size), 0);
        std::size_t received = 0;
        net::StreamResult sr = net::HttpStreamRange(
            req.url, offset, size,
            [&](const void *d, std::size_t n) -> bool {
                n = std::min(n, static_cast<std::size_t>(size) - received);
                std::memcpy(out.data() + received, d, n);
                received += n;
                return true;
            }, req.http_opts);
        if(sr.success && received != static_cast<std::size_t>(size)) {
            sr.success = false;
            sr.error   = "short read";
        }
        return sr;
    }, {});
    if(!r.success && out_error) *out_error = r.error;
    return r.success;
}

bool IsNczName(const std::string &name) {
    if(name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return ext == ".ncz";
}

bool ResolvePfs0Entries(const StreamInstallRequest &req,
                         std::vector<RemoteEntry> &out_entries,
                         std::string &out_error) {
    std::vector<std::uint8_t> buf(kPrefetchSize, 0);
    std::size_t recv = 0;
    net::HttpStreamRange(req.url, 0, kPrefetchSize,
        [&](const void *d, std::size_t n) -> bool {
            n = std::min(n, kPrefetchSize - recv);
            std::memcpy(buf.data() + recv, d, n);
            recv += n;
            return true;
        }, req.http_opts);

    if(recv < sizeof(Pfs0Header)) { out_error = "File too small to be a valid NSP"; return false; }

    const auto *hdr = reinterpret_cast<const Pfs0Header *>(buf.data());
    if(hdr->magic != kMagicPfs0 || hdr->file_count == 0) {
        out_error = "Not a valid NSP (bad PFS0 magic)";
        return false;
    }

    const std::size_t entry_bytes  = static_cast<std::size_t>(hdr->file_count) * sizeof(Pfs0FileEntry);
    const std::size_t string_bytes = hdr->string_table_size;
    const std::size_t header_total = sizeof(Pfs0Header) + entry_bytes + string_bytes;

    if(header_total > recv) {
        buf.resize(header_total);
        std::size_t extra = recv;
        const net::StreamResult r = net::HttpStreamRange(
            req.url, recv, header_total - recv,
            [&](const void *d, std::size_t n) -> bool {
                n = std::min(n, header_total - extra);
                std::memcpy(buf.data() + extra, d, n);
                extra += n;
                return true;
            }, req.http_opts);
        if(!r.success) { out_error = "Failed to fetch complete PFS0 table: " + r.error; return false; }
    }

    const std::uint64_t data_start = static_cast<std::uint64_t>(header_total);
    const auto *entries = reinterpret_cast<const Pfs0FileEntry *>(buf.data() + sizeof(Pfs0Header));
    const char *str_tbl = reinterpret_cast<const char *>(buf.data() + sizeof(Pfs0Header) + entry_bytes);

    out_entries.reserve(hdr->file_count);
    for(std::uint32_t i = 0; i < hdr->file_count; i++) {
        RemoteEntry e;
        e.name   = str_tbl + entries[i].string_table_offset;
        e.offset = data_start + entries[i].data_offset;
        e.size   = entries[i].file_size;
        out_entries.push_back(std::move(e));
    }
    return true;
}

bool ParseHfs0Block(const std::uint8_t *data, std::size_t data_size,
                     std::uint64_t hfs0_abs_offset,
                     std::vector<RemoteEntry> &out_entries,
                     std::string &out_error) {
    if(data_size < sizeof(Hfs0Header)) { out_error = "HFS0 buffer too small"; return false; }
    const auto *hdr = reinterpret_cast<const Hfs0Header *>(data);
    if(hdr->magic != kMagicHfs0) { out_error = "Bad HFS0 magic"; return false; }

    const std::size_t entry_bytes  = static_cast<std::size_t>(hdr->file_count) * sizeof(Hfs0FileEntry);
    const std::size_t string_bytes = hdr->string_table_size;
    const std::size_t header_total = sizeof(Hfs0Header) + entry_bytes + string_bytes;
    if(data_size < header_total) { out_error = "HFS0 buffer truncated"; return false; }

    const std::uint64_t data_region = hfs0_abs_offset + header_total;
    const auto *entries = reinterpret_cast<const Hfs0FileEntry *>(data + sizeof(Hfs0Header));
    const char *str_tbl = reinterpret_cast<const char *>(data + sizeof(Hfs0Header) + entry_bytes);

    out_entries.reserve(hdr->file_count);
    for(std::uint32_t i = 0; i < hdr->file_count; i++) {
        RemoteEntry e;
        e.name   = str_tbl + entries[i].string_table_offset;
        e.offset = data_region + entries[i].data_offset;
        e.size   = entries[i].file_size;
        out_entries.push_back(std::move(e));
    }
    return true;
}

bool ResolveXciEntries(const StreamInstallRequest &req,
                        std::vector<RemoteEntry> &out_entries,
                        std::string &out_error) {
    std::vector<std::uint8_t> xci_hdr_buf;
    if(!RangeRead(req, 0, sizeof(XciHeaderFields), xci_hdr_buf)) {
        out_error = "Failed to fetch XCI header";
        return false;
    }

    const auto *xci = reinterpret_cast<const XciHeaderFields *>(xci_hdr_buf.data());
    if(xci->magic != kMagicXci) {
        out_error = "Not a valid XCI (bad HEAD magic)";
        return false;
    }

    const std::uint64_t root_hfs0_abs = xci->hfs0_offset;

    std::vector<std::uint8_t> root_hdr_buf;
    if(!RangeRead(req, root_hfs0_abs, kPrefetchSize, root_hdr_buf)) {
        out_error = "Failed to fetch XCI root HFS0";
        return false;
    }

    std::vector<RemoteEntry> root_partitions;
    {
        std::string err;
        if(!ParseHfs0Block(root_hdr_buf.data(), root_hdr_buf.size(),
                            root_hfs0_abs, root_partitions, err)) {
            out_error = "XCI root HFS0: " + err;
            return false;
        }
    }

    const RemoteEntry *secure_entry = nullptr;
    for(const auto &p : root_partitions) {
        std::string lower = p.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if(lower == "secure") { secure_entry = &p; break; }
    }
    if(!secure_entry) { out_error = "XCI has no 'secure' partition"; return false; }

    std::vector<std::uint8_t> sec_hdr_buf;
    if(!RangeRead(req, secure_entry->offset, kPrefetchSize, sec_hdr_buf)) {
        out_error = "Failed to fetch XCI secure partition";
        return false;
    }

    if(sec_hdr_buf.size() >= sizeof(Hfs0Header)) {
        const auto *shdr = reinterpret_cast<const Hfs0Header *>(sec_hdr_buf.data());
        if(shdr->magic == kMagicHfs0) {
            const std::size_t need = sizeof(Hfs0Header)
                + static_cast<std::size_t>(shdr->file_count) * sizeof(Hfs0FileEntry)
                + shdr->string_table_size;
            if(need > sec_hdr_buf.size()) {
                if(!RangeRead(req, secure_entry->offset, need, sec_hdr_buf)) {
                    out_error = "Failed to fetch complete XCI secure HFS0 table";
                    return false;
                }
            }
        }
    }

    std::string err;
    if(!ParseHfs0Block(sec_hdr_buf.data(), sec_hdr_buf.size(),
                        secure_entry->offset, out_entries, err)) {
        out_error = "XCI secure HFS0: " + err;
        return false;
    }
    return true;
}

bool StreamPlainNcaToStorage(const StreamInstallRequest &req,
                              std::function<bool()> stop_cb,
                              std::uint64_t nca_offset,
                              std::uint64_t nca_file_size,
                              ContentStorage &storage,
                              const NcmContentId &nca_id,
                              const HeaderKey *header_key,
                              ProgressCallback &progress,
                              std::uint32_t nca_index,
                              std::uint32_t nca_count,
                              const std::string &nca_name) {
    std::vector<std::uint8_t> hdr_buf(kNcaHeaderSize, 0);
    {
        const std::uint64_t fetch = std::min<std::uint64_t>(kNcaHeaderSize, nca_file_size);
        std::size_t recv = 0;
        net::HttpOptions opts = req.http_opts; opts.cancel = stop_cb;
        const net::StreamResult r = net::HttpStreamRange(req.url, nca_offset, fetch,
            [&](const void *d, std::size_t n) -> bool {
                n = std::min(n, static_cast<std::size_t>(fetch) - recv);
                std::memcpy(hdr_buf.data() + recv, d, n); recv += n; return true;
            }, opts);
        if(!r.success || recv < static_cast<std::size_t>(fetch)) return false;
    }

    std::uint64_t nca_size = nca_file_size;
    if(header_key && nca_file_size >= kNcaHeaderSize) {
        NcaHeader dec{};
        std::memcpy(&dec, hdr_buf.data(), sizeof(dec));
        DecryptNcaHeader(&dec, kNcaHeaderSize, *header_key);
        if(dec.magic == kMagicNca3 && dec.nca_size >= kNcaHeaderSize) nca_size = dec.nca_size;
        if(dec.distribution != 0) {
            dec.distribution = 0;
            EncryptNcaHeader(&dec, kNcaHeaderSize, *header_key);
            std::memcpy(hdr_buf.data(), &dec, kNcaHeaderSize);
        }
    }

    storage.DeletePlaceholder(nca_id);
    if(!storage.CreatePlaceholder(nca_id, nca_size)) return false;
    if(!storage.WritePlaceholder(nca_id, 0, hdr_buf.data(), hdr_buf.size())) {
        storage.DeletePlaceholder(nca_id); return false;
    }
    std::uint64_t written = hdr_buf.size();
    if(progress) progress(InstallProgress{written, nca_size, nca_name, nca_index, nca_count, false});

    const std::uint64_t body_offset = nca_offset + written;
    const std::uint64_t body_size   = (nca_size > written) ? (nca_size - written) : 0;
    bool ok = true;

    if(body_size > 0) {
        std::vector<std::uint8_t> wbuf;
        wbuf.reserve(kNcmWriteSize);
        std::uint64_t body_written = 0;
        net::HttpOptions opts = req.http_opts; opts.cancel = stop_cb;
        const net::StreamResult r = net::HttpStreamRange(req.url, body_offset, body_size,
            [&](const void *d, std::size_t n) -> bool {
                if(stop_cb && stop_cb()) return false;
                const auto *src = static_cast<const std::uint8_t *>(d);
                std::size_t left = n;
                while(left > 0) {
                    const std::size_t space = kNcmWriteSize - wbuf.size();
                    const std::size_t take  = std::min(space, left);
                    wbuf.insert(wbuf.end(), src, src + take);
                    src += take; left -= take;
                    if(wbuf.size() >= kNcmWriteSize) {
                        if(!storage.WritePlaceholder(nca_id, written + body_written,
                                                      wbuf.data(), wbuf.size())) return false;
                        body_written += wbuf.size();
                        wbuf.clear();
                        if(progress) progress(InstallProgress{written + body_written, nca_size,
                                                               nca_name, nca_index, nca_count, false});
                    }
                }
                return true;
            }, opts);
        ok = r.success;
        if(ok && !wbuf.empty()) {
            ok = storage.WritePlaceholder(nca_id, written + body_written,
                                           wbuf.data(), wbuf.size());
            body_written += wbuf.size();
        }
        if(ok) written += body_written;
    }

    if(!ok || written != nca_size) { storage.DeletePlaceholder(nca_id); return false; }
    storage.Register(nca_id, nca_id);
    storage.DeletePlaceholder(nca_id);
    if(progress) progress(InstallProgress{nca_size, nca_size, nca_name, nca_index, nca_count, false});
    return true;
}

InstallResult InstallRemoteEntries(const StreamInstallRequest &req,
                                    const std::vector<RemoteEntry> &entries,
                                    ProgressCallback progress,
                                    std::function<bool()> stop_callback) {
    InstallResult result;

    HeaderKey header_key{};
    const bool have_hk = DeriveHeaderKey(header_key);
    const HeaderKey *hk = have_hk ? &header_key : nullptr;

    ContentStorage storage(req.install_config.dest_storage_id);
    if(!storage.IsOpen()) { result.error_message = "Failed to open content storage"; return result; }

    ContentMetaDatabase meta_db(req.install_config.dest_storage_id);
    if(!meta_db.IsOpen()) { result.error_message = "Failed to open content meta database"; return result; }

    std::vector<NcmContentId> registered_ids;
    struct CleanupGuard {
        ContentStorage &storage;
        std::vector<NcmContentId> &ids;
        bool armed = true;
        ~CleanupGuard() {
            if(!armed) return;
            for(const auto &id : ids) storage.Delete(id);
        }
    } cleanup{storage, registered_ids};

    std::vector<const RemoteEntry *> tik_vec, cert_vec;
    for(const auto &e : entries) {
        std::string lo = e.name;
        std::transform(lo.begin(), lo.end(), lo.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        if(lo.size() >= 4 && lo.substr(lo.size() - 4) == ".tik")  tik_vec.push_back(&e);
        if(lo.size() >= 5 && lo.substr(lo.size() - 5) == ".cert") cert_vec.push_back(&e);
    }
    for(std::size_t i = 0; i < tik_vec.size() && i < cert_vec.size(); i++) {
        std::vector<std::uint8_t> tik_buf, cert_buf;
        RangeRead(req, tik_vec[i]->offset,  tik_vec[i]->size,  tik_buf);
        RangeRead(req, cert_vec[i]->offset, cert_vec[i]->size, cert_buf);
        if(tik_buf.size() == tik_vec[i]->size && cert_buf.size() == cert_vec[i]->size) {
            ImportTicket(tik_buf.data(), tik_buf.size(), cert_buf.data(), cert_buf.size());
        }
    }

    struct CnmtRecord { ContentMeta meta; NcmContentInfo cnmt_info; };
    std::vector<CnmtRecord> cnmt_records;

    for(const auto &e : entries) {
        std::string lo = e.name;
        std::transform(lo.begin(), lo.end(), lo.begin(),
                       [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        const bool is_cnmt = lo.size() > 9 && (
            lo.substr(lo.size() - 9) == ".cnmt.nca" ||
            lo.substr(lo.size() - 9) == ".cnmt.ncz");
        if(!is_cnmt || e.name.size() < 32) continue;

        const NcmContentId cnmt_id = NcaIdFromString(e.name.substr(0, 32));
        if(!storage.Has(cnmt_id) || req.install_config.reinstall_ncas) {
            bool ok = false;
            if(IsNczName(e.name) && hk) {
                ok = DecompressNczFromRangeReaderToStorage(
                    MakeHttpRangeReader(req, stop_callback), e.offset, e.size,
                    storage, cnmt_id, *hk,
                    [&](std::uint64_t done, std::uint64_t total) {
                        if(progress) progress(InstallProgress{done, total, e.name, 0, 0, true});
                    }, stop_callback);
            } else {
                ok = StreamPlainNcaToStorage(req, stop_callback, e.offset, e.size,
                                              storage, cnmt_id, hk, progress, 0, 0, e.name);
            }
            if(!ok) { result.error_message = "Failed to install CNMT NCA: " + e.name; return result; }
            registered_ids.push_back(cnmt_id);
        }

        std::vector<std::uint8_t> cnmt_data;
        if(!ReadCnmtFromInstalledNca(storage, cnmt_id, cnmt_data)) {
            result.error_message = "Failed to read CNMT: " + e.name; return result;
        }
        CnmtRecord rec;
        rec.meta = ContentMeta(cnmt_data.data(), cnmt_data.size());
        rec.cnmt_info.content_id = cnmt_id;
        ncmU64ToContentInfoSize(e.size & 0xFFFFFFFFFFFFULL, &rec.cnmt_info);
        rec.cnmt_info.content_type = NcmContentType_Meta;
        cnmt_records.push_back(std::move(rec));
    }

    if(cnmt_records.empty()) { result.error_message = "No CNMT NCA found in package"; return result; }

    struct DeferredPush { std::uint64_t base_title_id; NcmContentMetaKey key; };
    std::vector<DeferredPush> deferred_pushes;
    for(auto &rec : cnmt_records) {
        const auto key = rec.meta.GetContentMetaKey();
        ByteBuffer install_buf;
        if(!rec.meta.GetInstallContentMeta(install_buf, rec.cnmt_info, req.install_config.ignore_req_fw)) {
            result.error_message = "Failed to build install content meta"; return result;
        }
        if(!meta_db.Set(key, install_buf.GetData(), install_buf.GetSize())) {
            result.error_message = "Failed to set content meta"; return result;
        }
        if(!meta_db.Commit()) {
            result.error_message = "Failed to commit content meta database"; return result;
        }
        deferred_pushes.push_back({ GetBaseTitleId(key.id, static_cast<std::uint8_t>(key.type)), key });
        result.title_id = key.id;
    }

    std::vector<NcmContentInfo> all_infos;
    for(auto &rec : cnmt_records) {
        auto infos = rec.meta.GetContentInfos();
        all_infos.insert(all_infos.end(), infos.begin(), infos.end());
    }

    const std::uint32_t total_ncas = static_cast<std::uint32_t>(all_infos.size());
    std::uint32_t nca_index = 0;

    for(const auto &info : all_infos) {
        nca_index++;
        if(storage.Has(info.content_id) && !req.install_config.reinstall_ncas) continue;
        if(stop_callback && stop_callback()) { result.error_message = "Canceled"; return result; }

        const std::string id_hex = NcaIdToString(info.content_id);
        const RemoteEntry *entry = nullptr;
        for(const auto &e : entries) {
            if(e.name.size() >= 32 && e.name.substr(0, 32) == id_hex) { entry = &e; break; }
        }
        if(!entry) { result.error_message = "Missing NCA: " + id_hex; return result; }

        bool ok = false;
        if(IsNczName(entry->name) && hk) {
            ok = DecompressNczFromRangeReaderToStorage(
                MakeHttpRangeReader(req, stop_callback), entry->offset, entry->size,
                storage, info.content_id, *hk,
                [&](std::uint64_t done, std::uint64_t total) {
                    if(progress) progress(InstallProgress{done, total, entry->name,
                                                           nca_index, total_ncas, true});
                }, stop_callback);
        } else {
            ok = StreamPlainNcaToStorage(req, stop_callback, entry->offset, entry->size,
                                          storage, info.content_id, hk,
                                          progress, nca_index, total_ncas, entry->name);
        }
        if(!ok) { result.error_message = "Failed to install NCA: " + entry->name; return result; }
        registered_ids.push_back(info.content_id);
    }

    for(const auto &dp : deferred_pushes) {
        if(!PushApplicationRecord(dp.base_title_id, req.install_config.dest_storage_id, dp.key)) {
            result.error_message = "Failed to push application record"; return result;
        }
    }

    result.success = true;
    cleanup.armed = false;
    return result;
}

enum class PkgFormat { Nsp, Xci, Unknown };

PkgFormat DetectFormat(const std::string &url) {
    const auto dot = url.find_last_of('.');
    if(dot == std::string::npos) return PkgFormat::Unknown;
    auto end = url.find_first_of("?#", dot);
    const std::string ext = url.substr(dot, end == std::string::npos ? std::string::npos : end - dot);
    std::string lo = ext;
    std::transform(lo.begin(), lo.end(), lo.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if(lo == ".nsp" || lo == ".nsz") return PkgFormat::Nsp;
    if(lo == ".xci" || lo == ".xcz") return PkgFormat::Xci;
    return PkgFormat::Unknown;
}

} // anon

InstallResult StreamInstallFromUrl(const StreamInstallRequest &req,
                                    ProgressCallback progress,
                                    std::function<bool()> stop_callback) {
    InstallResult result;

    std::vector<RemoteEntry> entries;
    std::string parse_error;
    bool resolved = false;

    switch(DetectFormat(req.url)) {
        case PkgFormat::Nsp:
            resolved = ResolvePfs0Entries(req, entries, parse_error);
            break;
        case PkgFormat::Xci:
            resolved = ResolveXciEntries(req, entries, parse_error);
            break;
        default: {
            resolved = ResolvePfs0Entries(req, entries, parse_error);
            if(!resolved) {
                entries.clear();
                std::string xci_err;
                if(ResolveXciEntries(req, entries, xci_err)) {
                    resolved = true;
                    parse_error.clear();
                }
            }
            break;
        }
    }

    if(!resolved) {
        result.error_message = "Failed to parse package: " + parse_error;
        return result;
    }

    appletSetMediaPlaybackState(true);
    result = InstallRemoteEntries(req, entries, std::move(progress), std::move(stop_callback));
    appletSetMediaPlaybackState(false);
    return result;
}

}
