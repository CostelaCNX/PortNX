#pragma once

#include <string>
#include <vector>

#include <catalog/CatalogItem.hpp>

namespace pinx::catalog {

struct ParseResult {
    bool        ok = false;
    std::string error;                 // set when !ok
    std::string message;               // optional server "success" text
    std::vector<CatalogItem> items;    // directories first, then files
};

// Parse a JSON index in the common store format: top-level object with optional
// "files", "directories", "success" and "error" keys. "files" entries may be a
// bare URL string or an object {url, size, name, version}. Relative URLs are
// resolved against |base_url|.
ParseResult ParseIndex(const std::string &body, const std::string &base_url);

}
