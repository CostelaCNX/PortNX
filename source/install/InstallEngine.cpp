#include <install/InstallEngine.hpp>
#include <install/ContentMeta.hpp>
#include <install/CryptoUtils.hpp>
#include <install/NcaStructs.hpp>
#include <install/NcmWrapper.hpp>
#include <install/NszDecompressor.hpp>
#include <install/Pfs0Parser.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <sys/stat.h>

#include <switch.h>

namespace pinx::install {
namespace {

constexpr std::size_t kStreamBufferSize    = 512 * 1024;
constexpr std::size_t kPlaceholderFlushSize = 1 * 1024 * 1024;
constexpr const char *kTempDir             = "sdmc:/switch/PortNX/tmp";
constexpr const char *kNczStreamTemp       = "sdmc:/switch/PortNX/tmp/stream_ncz.ncz";

class BufferedPlaceholderWriter {
public:
    BufferedPlaceholderWriter(ContentStorage &storage, const NcmContentId &id,
                               std::uint64_t start_offset)
        : storage_(storage), id_(id), write_offset_(start_offset) {
        buffer_.reserve(kPlaceholderFlushSize);
    }

    bool Write(const void *data, std::size_t len) {
        const auto *p = static_cast<const std::uint8_t *>(data);
        std::size_t left = len;
        while(left > 0) {
            const std::size_t space = kPlaceholderFlushSize - buffer_.size();
            const std::size_t take  = std::min(space, left);
            buffer_.insert(buffer_.end(), p, p + take);
            p    += take;
            left -= take;
            if(buffer_.size() >= kPlaceholderFlushSize) {
                if(!Flush()) return false;
            }
        }
        return true;
    }

    bool Flush() {
        if(buffer_.empty()) return true;
        if(!storage_.WritePlaceholder(id_, write_offset_, buffer_.data(), buffer_.size())) {
            return false;
        }
        write_offset_ += buffer_.size();
        buffer_.clear();
        return true;
    }

private:
    ContentStorage &storage_;
    NcmContentId id_{};
    std::vector<std::uint8_t> buffer_;
    std::uint64_t write_offset_ = 0;
};

class AsyncPlaceholderWriter {
public:
    AsyncPlaceholderWriter(ContentStorage &storage, const NcmContentId &id,
                            std::uint64_t start_offset)
        : storage_(storage), id_(id), consumer_offset_(start_offset) {
        pending_.reserve(kPlaceholderFlushSize);
        worker_ = std::thread([this] { WorkerLoop(); });
    }

    ~AsyncPlaceholderWriter() { Shutdown(); }

    bool Write(const void *data, std::size_t len) {
        const auto *p = static_cast<const std::uint8_t *>(data);
        std::size_t left = len;
        while(left > 0) {
            { std::lock_guard<std::mutex> g(mtx_); if(error_) return false; }
            const std::size_t space = kPlaceholderFlushSize - pending_.size();
            const std::size_t take  = std::min(space, left);
            pending_.insert(pending_.end(), p, p + take);
            p    += take;
            left -= take;
            if(pending_.size() >= kPlaceholderFlushSize) {
                if(!Enqueue()) return false;
            }
        }
        return true;
    }

    bool Finish() {
        if(!pending_.empty()) { if(!Enqueue()) { Shutdown(); return false; } }
        Shutdown();
        std::lock_guard<std::mutex> g(mtx_);
        return !error_;
    }

private:
    bool Enqueue() {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_space_.wait(lk, [this] { return error_ || queue_.size() < kMaxQueued; });
        if(error_) return false;
        queue_.push_back(std::move(pending_));
        pending_ = std::vector<std::uint8_t>();
        pending_.reserve(kPlaceholderFlushSize);
        lk.unlock();
        cv_data_.notify_one();
        return true;
    }

    void WorkerLoop() {
        std::uint64_t off = consumer_offset_;
        while(true) {
            std::vector<std::uint8_t> buf;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_data_.wait(lk, [this] { return !queue_.empty() || stop_; });
                if(queue_.empty()) return;
                buf = std::move(queue_.front());
                queue_.pop_front();
            }
            cv_space_.notify_one();
            if(!storage_.WritePlaceholder(id_, off, buf.data(), buf.size())) {
                std::lock_guard<std::mutex> g(mtx_);
                error_ = true;
                cv_space_.notify_all();
                return;
            }
            off += buf.size();
        }
    }

    void Shutdown() {
        { std::lock_guard<std::mutex> g(mtx_); stop_ = true; }
        cv_data_.notify_all();
        cv_space_.notify_all();
        if(worker_.joinable()) worker_.join();
    }

    static constexpr std::size_t kMaxQueued = 3;

    ContentStorage &storage_;
    NcmContentId id_{};
    std::uint64_t consumer_offset_;
    std::vector<std::uint8_t> pending_;
    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_data_;
    std::condition_variable cv_space_;
    std::deque<std::vector<std::uint8_t>> queue_;
    bool stop_ = false;
    bool error_ = false;
};

bool IsNczFilename(const std::string &name) {
    if(name.size() < 4) return false;
    std::string ext = name.substr(name.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".ncz";
}

struct GenericEntry {
    std::string   name;
    std::uint64_t offset;
    std::uint64_t size;
};

const GenericEntry *FindEntryByNcaId(const std::vector<GenericEntry> &entries,
                                      const std::string &nca_id_hex) {
    for(const auto &entry : entries) {
        if(entry.name.size() >= 32) {
            bool match = std::equal(
                nca_id_hex.begin(), nca_id_hex.end(), entry.name.begin(),
                [](char a, char b) {
                    return std::tolower(static_cast<unsigned char>(a)) ==
                           std::tolower(static_cast<unsigned char>(b));
                });
            if(match) return &entry;
        }
    }
    return nullptr;
}

bool StreamNcaToStorage(FILE *file, std::uint64_t offset, std::uint64_t size,
                         ContentStorage &storage, const NcmContentId &nca_id,
                         ProgressCallback &progress,
                         std::uint32_t nca_index, std::uint32_t nca_count,
                         const std::string &nca_name,
                         const HeaderKey *header_key) {
    storage.DeletePlaceholder(nca_id);

    NcaHeader raw_header{};
    std::uint64_t nca_size = size;

    if(size >= kNcaHeaderSize) {
        if(std::fseek(file, static_cast<long>(offset), SEEK_SET) != 0) return false;
        if(std::fread(&raw_header, kNcaHeaderSize, 1, file) == 1 && header_key) {
            NcaHeader dec = raw_header;
            DecryptNcaHeader(&dec, kNcaHeaderSize, *header_key);
            if(dec.magic == kMagicNca3) {
                if(dec.nca_size >= kNcaHeaderSize && dec.nca_size <= size) {
                    nca_size = dec.nca_size;
                }
                if(dec.distribution != 0) {
                    dec.distribution = 0;
                    EncryptNcaHeader(&dec, kNcaHeaderSize, *header_key);
                    std::memcpy(&raw_header, &dec, kNcaHeaderSize);
                }
            }
        }
    }

    if(!storage.CreatePlaceholder(nca_id, nca_size)) return false;

    struct PlaceholderGuard {
        ContentStorage &s;
        const NcmContentId &id;
        ~PlaceholderGuard() { s.DeletePlaceholder(id); }
    } ph_guard{storage, nca_id};

    std::uint64_t written = 0;
    if(size >= kNcaHeaderSize) {
        if(!storage.WritePlaceholder(nca_id, 0, &raw_header, kNcaHeaderSize)) return false;
        written = kNcaHeaderSize;
        if(progress) progress(InstallProgress{written, nca_size, nca_name, nca_index, nca_count, false});
    }

    if(std::fseek(file, static_cast<long>(offset + written), SEEK_SET) != 0) return false;

    auto buf = std::make_unique<std::uint8_t[]>(kStreamBufferSize);
    while(written < nca_size) {
        const std::size_t chunk = std::min<std::size_t>(kStreamBufferSize, nca_size - written);
        const std::size_t read_count = std::fread(buf.get(), 1, chunk, file);
        if(read_count == 0) break;
        if(!storage.WritePlaceholder(nca_id, written, buf.get(), read_count)) return false;
        written += read_count;
        if(progress) progress(InstallProgress{written, nca_size, nca_name, nca_index, nca_count, false});
    }

    if(written != nca_size) return false;
    storage.Register(nca_id, nca_id);
    storage.DeletePlaceholder(nca_id);
    return true;
}

bool InstallSingleNca(FILE *file, const std::string &container_path,
                       const GenericEntry *nca_entry,
                       ContentStorage &storage, const NcmContentId &nca_id,
                       ProgressCallback &progress,
                       std::uint32_t nca_index, std::uint32_t nca_count,
                       const HeaderKey *header_key) {
    if(IsNczFilename(nca_entry->name)) {
        if(header_key != nullptr) {
            const bool ok = DecompressNczToStorage(
                container_path, nca_entry->offset, nca_entry->size,
                storage, nca_id, *header_key,
                [&progress, &nca_entry, nca_index, nca_count](std::uint64_t done, std::uint64_t total) {
                    if(progress) progress(InstallProgress{done, total, nca_entry->name, nca_index, nca_count, true});
                });
            if(ok) return true;
        }

        std::string decompressed = DecompressNczToTemp(
            container_path, nca_entry->offset, nca_entry->size, kTempDir,
            [&progress, &nca_entry, nca_index, nca_count](std::uint64_t done, std::uint64_t total) {
                if(progress) progress(InstallProgress{done, total, nca_entry->name, nca_index, nca_count, true});
            });

        if(decompressed.empty()) {
            return StreamNcaToStorage(file, nca_entry->offset, nca_entry->size,
                                       storage, nca_id, progress, nca_index, nca_count,
                                       nca_entry->name, header_key);
        }

        FILE *tmp_file = std::fopen(decompressed.c_str(), "rb");
        if(!tmp_file) { std::remove(decompressed.c_str()); return false; }

        std::fseek(tmp_file, 0, SEEK_END);
        const std::uint64_t decompressed_size = static_cast<std::uint64_t>(std::ftell(tmp_file));
        std::fseek(tmp_file, 0, SEEK_SET);

        bool ok = StreamNcaToStorage(tmp_file, 0, decompressed_size,
                                      storage, nca_id, progress, nca_index, nca_count,
                                      nca_entry->name, header_key);
        std::fclose(tmp_file);
        std::remove(decompressed.c_str());
        return ok;
    }

    return StreamNcaToStorage(file, nca_entry->offset, nca_entry->size,
                               storage, nca_id, progress, nca_index, nca_count,
                               nca_entry->name, header_key);
}

InstallResult InstallFromEntries(FILE *file,
                                  const std::string &container_path,
                                  const std::vector<GenericEntry> &entries,
                                  const InstallConfig &config,
                                  ProgressCallback progress) {
    InstallResult result;

    HeaderKey header_key{};
    const bool have_header_key = DeriveHeaderKey(header_key);
    const HeaderKey *hk_ptr = have_header_key ? &header_key : nullptr;

    ContentStorage storage(config.dest_storage_id);
    if(!storage.IsOpen()) { result.error_message = "Failed to open content storage"; return result; }

    ContentMetaDatabase meta_db(config.dest_storage_id);
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
    } cleanup_guard{storage, registered_ids};

    std::vector<const GenericEntry *> tik_entries, cert_entries;
    for(const auto &entry : entries) {
        std::string lower_name = entry.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if(lower_name.size() >= 4 && lower_name.substr(lower_name.size() - 4) == ".tik")
            tik_entries.push_back(&entry);
        if(lower_name.size() >= 5 && lower_name.substr(lower_name.size() - 5) == ".cert")
            cert_entries.push_back(&entry);
    }

    for(std::size_t i = 0; i < tik_entries.size(); i++) {
        if(i >= cert_entries.size()) break;
        const auto *tik_e  = tik_entries[i];
        const auto *cert_e = cert_entries[i];
        auto tik_buf  = std::make_unique<std::uint8_t[]>(tik_e->size);
        auto cert_buf = std::make_unique<std::uint8_t[]>(cert_e->size);
        std::fseek(file, static_cast<long>(tik_e->offset), SEEK_SET);
        if(std::fread(tik_buf.get(), tik_e->size, 1, file) != 1) continue;
        std::fseek(file, static_cast<long>(cert_e->offset), SEEK_SET);
        if(std::fread(cert_buf.get(), cert_e->size, 1, file) != 1) continue;
        ImportTicket(tik_buf.get(), tik_e->size, cert_buf.get(), cert_e->size);
    }

    std::vector<const GenericEntry *> cnmt_entries;
    for(const auto &entry : entries) {
        if(entry.name.size() > 9) {
            std::string suffix = entry.name.substr(entry.name.size() - 9);
            std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if(suffix == ".cnmt.nca" || suffix == ".cnmt.ncz")
                cnmt_entries.push_back(&entry);
        }
    }
    if(cnmt_entries.empty()) { result.error_message = "No CNMT NCA found in package"; return result; }

    struct CnmtRecord { ContentMeta meta; NcmContentInfo cnmt_info; };
    std::vector<CnmtRecord> cnmt_records;

    for(const auto *cnmt_entry : cnmt_entries) {
        if(cnmt_entry->name.size() < 32) continue;
        std::string nca_id_hex = cnmt_entry->name.substr(0, 32);
        NcmContentId cnmt_nca_id = NcaIdFromString(nca_id_hex);

        if(!storage.Has(cnmt_nca_id) || config.reinstall_ncas) {
            if(!InstallSingleNca(file, container_path, cnmt_entry,
                                  storage, cnmt_nca_id, progress, 0, 0, hk_ptr)) {
                result.error_message = "Failed to install CNMT NCA: " + cnmt_entry->name;
                return result;
            }
            registered_ids.push_back(cnmt_nca_id);
        }

        std::vector<std::uint8_t> cnmt_data;
        if(!ReadCnmtFromInstalledNca(storage, cnmt_nca_id, cnmt_data)) {
            result.error_message = "Failed to read CNMT from installed NCA: " + cnmt_entry->name;
            return result;
        }

        CnmtRecord record{};
        record.meta = ContentMeta(cnmt_data.data(), cnmt_data.size());
        record.cnmt_info.content_id = cnmt_nca_id;
        ncmU64ToContentInfoSize(cnmt_entry->size & 0xFFFFFFFFFFFFULL, &record.cnmt_info);
        record.cnmt_info.content_type = NcmContentType_Meta;
        cnmt_records.push_back(std::move(record));
    }

    struct DeferredPush { std::uint64_t base_title_id; NcmContentMetaKey key; };
    std::vector<DeferredPush> deferred_pushes;

    for(auto &record : cnmt_records) {
        const auto key = record.meta.GetContentMetaKey();
        ByteBuffer install_buf;
        if(!record.meta.GetInstallContentMeta(install_buf, record.cnmt_info, config.ignore_req_fw)) {
            result.error_message = "Failed to build install content meta";
            return result;
        }
        if(!meta_db.Set(key, install_buf.GetData(), install_buf.GetSize())) {
            result.error_message = "Failed to set content meta records";
            return result;
        }
        if(!meta_db.Commit()) {
            result.error_message = "Failed to commit content meta database";
            return result;
        }
        const std::uint64_t base_title_id =
            GetBaseTitleId(key.id, static_cast<std::uint8_t>(key.type));
        deferred_pushes.push_back({ base_title_id, key });
        result.title_id = key.id;
    }

    std::vector<NcmContentInfo> all_content_infos;
    for(auto &record : cnmt_records) {
        auto infos = record.meta.GetContentInfos();
        all_content_infos.insert(all_content_infos.end(), infos.begin(), infos.end());
    }

    const std::uint32_t total_ncas = static_cast<std::uint32_t>(all_content_infos.size());
    std::uint32_t nca_index = 0;

    for(const auto &content_info : all_content_infos) {
        nca_index++;
        const std::string nca_id_hex = NcaIdToString(content_info.content_id);
        if(storage.Has(content_info.content_id) && !config.reinstall_ncas) continue;

        const GenericEntry *nca_entry = FindEntryByNcaId(entries, nca_id_hex);
        if(!nca_entry) {
            result.error_message = "Missing NCA in package: " + nca_id_hex;
            return result;
        }

        if(!InstallSingleNca(file, container_path, nca_entry,
                              storage, content_info.content_id,
                              progress, nca_index, total_ncas, hk_ptr)) {
            result.error_message = "Failed to install NCA: " + nca_entry->name;
            return result;
        }
        registered_ids.push_back(content_info.content_id);
    }

    for(const auto &dp : deferred_pushes) {
        if(!PushApplicationRecord(dp.base_title_id, config.dest_storage_id, dp.key)) {
            result.error_message = "Failed to push application record";
            return result;
        }
    }

    result.success = true;
    cleanup_guard.armed = false;
    return result;
}

} // anon

InstallResult InstallNsp(const std::string &file_path,
                          const InstallConfig &config,
                          ProgressCallback progress) {
    InstallResult result;

    std::vector<Pfs0Entry> pfs0_entries;
    std::uint64_t data_offset = 0;
    if(!ParsePfs0(file_path, pfs0_entries, data_offset)) {
        result.error_message = "Failed to parse NSP/NSZ header";
        return result;
    }
    if(pfs0_entries.empty()) {
        result.error_message = "NSP contains no files";
        return result;
    }

    FILE *file = std::fopen(file_path.c_str(), "rb");
    if(!file) {
        result.error_message = "Cannot open file";
        return result;
    }

    appletSetMediaPlaybackState(true);
    mkdir("sdmc:/switch/PortNX", 0777);
    mkdir("sdmc:/switch/PortNX/tmp", 0777);

    std::vector<GenericEntry> generic_entries;
    generic_entries.reserve(pfs0_entries.size());
    for(const auto &e : pfs0_entries) {
        generic_entries.push_back({ e.name, e.offset, e.size });
    }

    result = InstallFromEntries(file, file_path, generic_entries, config, progress);

    appletSetMediaPlaybackState(false);
    std::fclose(file);
    return result;
}

InstallResult InstallFromLocalFile(const std::string &file_path,
                                    const InstallConfig &config,
                                    ProgressCallback progress) {
    std::string ext;
    const auto dot = file_path.find_last_of('.');
    if(dot != std::string::npos) {
        ext = file_path.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    if(ext == ".nsp" || ext == ".nsz") {
        return InstallNsp(file_path, config, std::move(progress));
    }

    InstallResult result;
    result.error_message = "Unsupported format: " + ext;
    return result;
}

}
