#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <switch.h>

namespace pinx::install {

struct InstalledTitle {
    std::uint64_t   title_id;
    std::uint32_t   version;
    NcmContentMetaType type;
    NcmStorageId    storage_id;
    std::uint32_t   content_count;
    std::string     name;        // from NACP; empty if unavailable
    std::string     publisher;
};

// List all installed Application / Patch / AddOnContent entries from SD + NAND.
// Does NOT populate InstalledTitle::name — names are loaded asynchronously.
std::vector<InstalledTitle> ListInstalledTitles();

struct TitleInfo {
    std::string          name;
    std::vector<uint8_t> icon_jpeg; // JPEG icon from NACP; empty if unavailable
};

// Read a title's display name and cover icon from the NS service.
// Must be called from the main thread. Results are cached in memory.
TitleInfo ReadTitleInfo(std::uint64_t app_title_id);

// Return the installed version of a title (NcmContentMetaType_Application only).
// Returns UINT32_MAX if not installed.
std::uint32_t GetInstalledVersion(std::uint64_t title_id);

// Remove a single title: deletes all NCAs, content meta entry, and application record.
// Returns true on full success. Partial failures still attempt all steps.
bool UninstallTitle(const InstalledTitle &title);

// Human-readable version string.
std::string VersionString(std::uint32_t version);

// Human-readable content meta type.
const char *MetaTypeName(NcmContentMetaType type);

}
