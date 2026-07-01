#include <install/NcmWrapper.hpp>

#include <cstring>

#include <switch.h>
#include <install/es_ipc.h>
#include <install/ns_ext_ipc.h>

namespace pinx::install {

ContentStorage::ContentStorage(NcmStorageId storage_id) {
    Result rc = ncmOpenContentStorage(&storage_, storage_id);
    open_ = R_SUCCEEDED(rc);
}

ContentStorage::~ContentStorage() {
    if(open_) ncmContentStorageClose(&storage_);
}

bool ContentStorage::IsOpen() const { return open_; }

bool ContentStorage::Has(const NcmContentId &id) {
    if(!open_) return false;
    bool has = false;
    Result rc = ncmContentStorageHas(&storage_, &has, &id);
    return R_SUCCEEDED(rc) && has;
}

bool ContentStorage::GetPath(const NcmContentId &id, std::string &out_path) {
    if(!open_) return false;
    char path_buf[FS_MAX_PATH] = {};
    Result rc = ncmContentStorageGetPath(&storage_, path_buf, sizeof(path_buf), &id);
    if(R_FAILED(rc)) return false;
    out_path = path_buf;
    return true;
}

void ContentStorage::DeletePlaceholder(const NcmContentId &id) {
    if(!open_) return;
    NcmPlaceHolderId ph_id;
    std::memcpy(&ph_id, &id, sizeof(ph_id));
    ncmContentStorageDeletePlaceHolder(&storage_, &ph_id);
}

bool ContentStorage::CreatePlaceholder(const NcmContentId &id, std::uint64_t size) {
    if(!open_) return false;
    NcmPlaceHolderId ph_id;
    std::memcpy(&ph_id, &id, sizeof(ph_id));
    Result rc = ncmContentStorageCreatePlaceHolder(&storage_, &id, &ph_id,
                                                    static_cast<s64>(size));
    return R_SUCCEEDED(rc);
}

bool ContentStorage::WritePlaceholder(const NcmContentId &id, std::uint64_t offset,
                                       const void *data, std::size_t length) {
    if(!open_) return false;
    NcmPlaceHolderId ph_id;
    std::memcpy(&ph_id, &id, sizeof(ph_id));
    Result rc = ncmContentStorageWritePlaceHolder(&storage_, &ph_id,
                                                   static_cast<u64>(offset),
                                                   data, length);
    return R_SUCCEEDED(rc);
}

bool ContentStorage::Register(const NcmContentId &placeholder_id,
                               const NcmContentId &content_id) {
    if(!open_) return false;
    NcmPlaceHolderId ph_id;
    std::memcpy(&ph_id, &placeholder_id, sizeof(ph_id));
    Result rc = ncmContentStorageRegister(&storage_, &content_id, &ph_id);
    return R_SUCCEEDED(rc);
}

bool ContentStorage::Delete(const NcmContentId &id) {
    if(!open_) return false;
    Result rc = ncmContentStorageDelete(&storage_, &id);
    return R_SUCCEEDED(rc);
}

ContentMetaDatabase::ContentMetaDatabase(NcmStorageId storage_id) {
    Result rc = ncmOpenContentMetaDatabase(&db_, storage_id);
    open_ = R_SUCCEEDED(rc);
}

ContentMetaDatabase::~ContentMetaDatabase() {
    if(open_) ncmContentMetaDatabaseClose(&db_);
}

bool ContentMetaDatabase::IsOpen() const { return open_; }

bool ContentMetaDatabase::Set(const NcmContentMetaKey &key,
                               const void *data, std::size_t size) {
    if(!open_) return false;
    Result rc = ncmContentMetaDatabaseSet(&db_, &key, data, static_cast<u64>(size));
    return R_SUCCEEDED(rc);
}

bool ContentMetaDatabase::Remove(const NcmContentMetaKey &key) {
    if(!open_) return false;
    Result rc = ncmContentMetaDatabaseRemove(&db_, &key);
    return R_SUCCEEDED(rc);
}

bool ContentMetaDatabase::Commit() {
    if(!open_) return false;
    Result rc = ncmContentMetaDatabaseCommit(&db_);
    return R_SUCCEEDED(rc);
}

std::vector<NcmContentMetaKey> ContentMetaDatabase::ListKeys(NcmContentMetaType type) {
    std::vector<NcmContentMetaKey> result;
    if(!open_) return result;

    constexpr s32 kBatch = 64;
    std::vector<NcmContentMetaKey> batch(kBatch);
    s32 offset = 0;
    while(true) {
        s32 total = 0, written = 0;
        Result rc = ncmContentMetaDatabaseList(&db_, &total, &written,
            batch.data(), kBatch, type,
            0, 0, UINT64_MAX, NcmContentInstallType_Full);
        if(R_FAILED(rc)) break;
        for(s32 i = 0; i < written; i++) result.push_back(batch[i]);
        offset += written;
        if(offset >= total) break;
    }
    return result;
}

bool IsTitleInstalled(std::uint64_t title_id) {
    const NcmStorageId storages[] = { NcmStorageId_SdCard, NcmStorageId_BuiltInUser };
    for(auto storage_id : storages) {
        NcmContentMetaDatabase db{};
        if(R_FAILED(ncmOpenContentMetaDatabase(&db, storage_id))) continue;
        NcmContentMetaKey key{};
        s32 total = 0, written = 0;
        Result rc = ncmContentMetaDatabaseList(&db, &total, &written, &key, 1,
            NcmContentMetaType_Application, title_id, title_id, title_id,
            NcmContentInstallType_Full);
        ncmContentMetaDatabaseClose(&db);
        if(R_SUCCEEDED(rc) && written > 0) return true;
    }
    return false;
}

bool PushApplicationRecord(std::uint64_t base_title_id,
                           NcmStorageId storage_id,
                           const NcmContentMetaKey &key) {
    ContentStorageRecord record{};
    record.meta_key   = key;
    record.storage_id = static_cast<u64>(storage_id);
    Result rc = nsPushApplicationRecord(base_title_id,
                                         NsApplicationRecordType_Installed,
                                         &record, 1);
    return R_SUCCEEDED(rc);
}

bool ImportTicket(const void *tik_data, std::size_t tik_size,
                  const void *cert_data, std::size_t cert_size) {
    Result rc = esImportTicket(tik_data, tik_size, cert_data, cert_size);
    return R_SUCCEEDED(rc);
}

}
