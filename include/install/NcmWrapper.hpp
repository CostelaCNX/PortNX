#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <switch.h>

namespace pinx::install {

class ContentStorage {
    public:
        explicit ContentStorage(NcmStorageId storage_id);
        ~ContentStorage();

        ContentStorage(const ContentStorage &) = delete;
        ContentStorage &operator=(const ContentStorage &) = delete;

        bool IsOpen() const;
        bool Has(const NcmContentId &id);
        bool GetPath(const NcmContentId &id, std::string &out_path);
        void DeletePlaceholder(const NcmContentId &id);
        bool CreatePlaceholder(const NcmContentId &id, std::uint64_t size);
        bool WritePlaceholder(const NcmContentId &id, std::uint64_t offset,
                              const void *data, std::size_t length);
        bool Register(const NcmContentId &placeholder_id, const NcmContentId &content_id);
        bool Delete(const NcmContentId &id);

    private:
        NcmContentStorage storage_{};
        bool open_ = false;
};

class ContentMetaDatabase {
    public:
        explicit ContentMetaDatabase(NcmStorageId storage_id);
        ~ContentMetaDatabase();

        ContentMetaDatabase(const ContentMetaDatabase &) = delete;
        ContentMetaDatabase &operator=(const ContentMetaDatabase &) = delete;

        bool IsOpen() const;
        bool Set(const NcmContentMetaKey &key, const void *data, std::size_t size);
        bool Remove(const NcmContentMetaKey &key);
        bool Commit();
        // List all keys of a given type (NcmContentMetaType_Unknown = all).
        std::vector<NcmContentMetaKey> ListKeys(NcmContentMetaType type = NcmContentMetaType_Unknown);

    private:
        NcmContentMetaDatabase db_{};
        bool open_ = false;
};

bool IsTitleInstalled(std::uint64_t title_id);

bool PushApplicationRecord(std::uint64_t base_title_id,
                           NcmStorageId storage_id,
                           const NcmContentMetaKey &key);

bool ImportTicket(const void *tik_data, std::size_t tik_size,
                  const void *cert_data, std::size_t cert_size);

}
