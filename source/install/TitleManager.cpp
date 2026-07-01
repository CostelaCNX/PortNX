#include <install/TitleManager.hpp>
#include <install/NcmWrapper.hpp>
#include <install/ns_ext_ipc.h>
#include <install/NcaStructs.hpp>

#include <cstdio>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

#include <switch.h>

namespace pinx::install {

std::string VersionString(std::uint32_t version) {
    const std::uint32_t major = version >> 26;
    const std::uint32_t minor = (version >> 20) & 0x3F;
    const std::uint32_t micro = (version >> 16) & 0xF;
    const std::uint32_t patch = version & 0xFFFF;
    char buf[32];
    if(patch > 0) std::snprintf(buf, sizeof(buf), "v%u.%u.%u.%u", major, minor, micro, patch);
    else          std::snprintf(buf, sizeof(buf), "v%u.%u.%u",    major, minor, micro);
    return buf;
}

const char *MetaTypeName(NcmContentMetaType type) {
    switch(type) {
        case NcmContentMetaType_Application:  return "App";
        case NcmContentMetaType_Patch:        return "Update";
        case NcmContentMetaType_AddOnContent: return "DLC";
        case NcmContentMetaType_Delta:        return "Delta";
        default:                              return "Other";
    }
}

static std::unordered_map<std::uint64_t, TitleInfo> s_title_cache;

TitleInfo ReadTitleInfo(std::uint64_t app_title_id) {
    auto it = s_title_cache.find(app_title_id);
    if(it != s_title_cache.end()) return it->second;

    auto *ctrl = static_cast<NsApplicationControlData *>(
        ::operator new(sizeof(NsApplicationControlData), std::nothrow));
    if(!ctrl) return {};

    std::memset(ctrl, 0, sizeof(*ctrl));
    u64 actual_size = 0;
    const Result rc = nsGetApplicationControlData(
        NsApplicationControlSource_Storage, app_title_id,
        ctrl, sizeof(*ctrl), &actual_size);

    TitleInfo info;
    if(R_SUCCEEDED(rc)) {
        NacpLanguageEntry *lang = nullptr;
        nacpGetLanguageEntry(&ctrl->nacp, &lang);
        if(lang && lang->name[0] != '\0')
            info.name = std::string(lang->name,
                ::strnlen(lang->name, sizeof(lang->name)));
        if(actual_size > sizeof(ctrl->nacp)) {
            const std::size_t icon_size = static_cast<std::size_t>(actual_size) - sizeof(ctrl->nacp);
            if(icon_size <= sizeof(ctrl->icon)) {
                info.icon_jpeg.assign(ctrl->icon, ctrl->icon + icon_size);
            }
        }
    }

    ::operator delete(ctrl);
    s_title_cache[app_title_id] = info;
    return info;
}

std::vector<InstalledTitle> ListInstalledTitles() {
    std::vector<InstalledTitle> result;

    const NcmStorageId storages[] = { NcmStorageId_SdCard, NcmStorageId_BuiltInUser };
    const NcmContentMetaType types[] = {
        NcmContentMetaType_Application,
        NcmContentMetaType_Patch,
        NcmContentMetaType_AddOnContent,
    };

    for(const auto storage_id : storages) {
        ContentMetaDatabase meta_db(storage_id);
        if(!meta_db.IsOpen()) continue;

        for(const auto meta_type : types) {
            const auto keys = meta_db.ListKeys(meta_type);
            for(const auto &key : keys) {
                std::uint32_t content_count = 0;
                {
                    NcmContentMetaDatabase raw_db{};
                    if(R_SUCCEEDED(ncmOpenContentMetaDatabase(&raw_db, storage_id))) {
                        u64 size_out = 0;
                        if(R_SUCCEEDED(ncmContentMetaDatabaseGetSize(&raw_db, &size_out, &key))) {
                            std::vector<std::uint8_t> cbuf(size_out);
                            if(R_SUCCEEDED(ncmContentMetaDatabaseGet(&raw_db, &key,
                                                                      &size_out, cbuf.data(), size_out))) {
                                NcmContentMetaHeader header{};
                                if(size_out >= sizeof(header)) {
                                    std::memcpy(&header, cbuf.data(), sizeof(header));
                                    content_count = header.content_count;
                                }
                            }
                        }
                        ncmContentMetaDatabaseClose(&raw_db);
                    }
                }

                InstalledTitle title;
                title.title_id      = key.id;
                title.version       = key.version;
                title.type          = static_cast<NcmContentMetaType>(key.type);
                title.storage_id    = storage_id;
                title.content_count = content_count;
                result.push_back(title);
            }
        }
    }

    return result;
}

std::uint32_t GetInstalledVersion(std::uint64_t title_id) {
    const NcmStorageId storages[] = { NcmStorageId_SdCard, NcmStorageId_BuiltInUser };
    for(const auto storage_id : storages) {
        NcmContentMetaDatabase raw_db{};
        if(R_FAILED(ncmOpenContentMetaDatabase(&raw_db, storage_id))) continue;
        NcmContentMetaKey key{};
        s32 total = 0, written = 0;
        Result rc = ncmContentMetaDatabaseList(&raw_db, &total, &written, &key, 1,
            NcmContentMetaType_Application, title_id, title_id, title_id,
            NcmContentInstallType_Full);
        ncmContentMetaDatabaseClose(&raw_db);
        if(R_SUCCEEDED(rc) && written > 0) return key.version;
    }
    return UINT32_MAX;
}

bool UninstallTitle(const InstalledTitle &title) {
    bool ok = true;

    const NcmContentMetaKey key {
        title.title_id,
        title.version,
        static_cast<u8>(title.type),
        0,
        NcmContentInstallType_Full
    };

    std::vector<NcmContentInfo> content_infos;
    {
        NcmContentMetaDatabase raw_db{};
        if(R_SUCCEEDED(ncmOpenContentMetaDatabase(&raw_db, title.storage_id))) {
            u64 size_out = 0;
            if(R_SUCCEEDED(ncmContentMetaDatabaseGetSize(&raw_db, &size_out, &key)) && size_out > 0) {
                std::vector<std::uint8_t> cbuf(size_out);
                if(R_SUCCEEDED(ncmContentMetaDatabaseGet(&raw_db, &key, &size_out, cbuf.data(), size_out))) {
                    NcmContentMetaHeader header{};
                    if(size_out >= sizeof(header)) {
                        std::memcpy(&header, cbuf.data(), sizeof(header));
                        const std::size_t info_offset = sizeof(header) + header.extended_header_size;
                        const std::size_t info_total  = static_cast<std::size_t>(header.content_count)
                                                        * sizeof(NcmContentInfo);
                        if(size_out >= info_offset + info_total) {
                            content_infos.resize(header.content_count);
                            std::memcpy(content_infos.data(), cbuf.data() + info_offset, info_total);
                        }
                    }
                }
            }
            ncmContentMetaDatabaseClose(&raw_db);
        }
    }

    {
        ContentStorage storage(title.storage_id);
        if(storage.IsOpen()) {
            for(const auto &info : content_infos) {
                if(!storage.Delete(info.content_id)) ok = false;
            }
        } else { ok = false; }
    }

    {
        ContentMetaDatabase meta_db(title.storage_id);
        if(meta_db.IsOpen()) {
            if(!meta_db.Remove(key)) ok = false;
            meta_db.Commit();
        } else { ok = false; }
    }

    const std::uint64_t base_id = GetBaseTitleId(title.title_id, static_cast<std::uint8_t>(title.type));
    nsDeleteApplicationRecord(base_id);

    return ok;
}

}
