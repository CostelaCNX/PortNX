#pragma once

#include <cstdint>
#include <string>

namespace pinx::catalog {

enum class EntryKind {
    Directory,  // a navigable sub-listing
    File,       // a downloadable port (.nsz/.nsp/.xci/.xcz/...)
};

struct CatalogItem {
    EntryKind     kind = EntryKind::File;
    std::string   name;            // display name (titledb name when available)
    std::string   url;             // absolute URL to fetch / download (no #fragment)
    std::string   filename;        // download filename derived from the URL basename
    std::uint64_t size = 0;        // bytes for display (may be approximate)
    bool          size_authoritative = false;  // true only when from the file entry
    std::string   format;          // lowercase extension: nsz/nsp/xci/xcz/zip
    std::string   version;         // optional
    std::string   sha256;          // optional lowercase hex integrity hash
    std::string   icon_url;        // optional cover art URL (rendered later)
    std::uint64_t title_id = 0;   // 0 = unknown; set when filename is a 16-hex TID
};

}
