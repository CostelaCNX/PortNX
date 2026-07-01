#include <install/Pfs0Parser.hpp>
#include <install/NcaStructs.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace pinx::install {
namespace {

bool EndsWith(const std::string &value, const std::string &suffix) {
    if(suffix.size() > value.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), value.rbegin(),
                      [](char a, char b) {
                          return std::tolower(static_cast<unsigned char>(a)) ==
                                 std::tolower(static_cast<unsigned char>(b));
                      });
}

}

bool ParsePfs0(const std::string &path,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset) {
    entries.clear();
    data_offset = 0;

    FILE *file = std::fopen(path.c_str(), "rb");
    if(!file) return false;

    Pfs0Header header{};
    if(std::fread(&header, sizeof(header), 1, file) != 1) {
        std::fclose(file);
        return false;
    }
    if(header.magic != kMagicPfs0) {
        std::fclose(file);
        return false;
    }
    if(header.file_count == 0) {
        std::fclose(file);
        return true;
    }

    const std::size_t entry_table_size = header.file_count * sizeof(Pfs0FileEntry);
    auto entry_buf = std::make_unique<std::uint8_t[]>(entry_table_size);
    if(std::fread(entry_buf.get(), entry_table_size, 1, file) != 1) {
        std::fclose(file);
        return false;
    }

    auto string_buf = std::make_unique<char[]>(header.string_table_size);
    if(std::fread(string_buf.get(), header.string_table_size, 1, file) != 1) {
        std::fclose(file);
        return false;
    }

    const std::uint64_t data_start = sizeof(Pfs0Header) +
                                     entry_table_size +
                                     header.string_table_size;
    data_offset = data_start;

    const auto *raw_entries = reinterpret_cast<const Pfs0FileEntry *>(entry_buf.get());
    entries.reserve(header.file_count);

    for(std::uint32_t i = 0; i < header.file_count; i++) {
        Pfs0Entry entry;
        if(raw_entries[i].string_table_offset < header.string_table_size) {
            entry.name = &string_buf[raw_entries[i].string_table_offset];
        }
        entry.offset = data_start + raw_entries[i].data_offset;
        entry.size   = raw_entries[i].file_size;
        entries.push_back(std::move(entry));
    }

    std::fclose(file);
    return true;
}

const Pfs0Entry *FindEntryByExtension(const std::vector<Pfs0Entry> &entries,
                                       const std::string &extension) {
    for(const auto &entry : entries) {
        if(EndsWith(entry.name, extension)) return &entry;
    }
    return nullptr;
}

std::vector<const Pfs0Entry *> FindEntriesByExtension(
    const std::vector<Pfs0Entry> &entries,
    const std::string &extension) {
    std::vector<const Pfs0Entry *> result;
    for(const auto &entry : entries) {
        if(EndsWith(entry.name, extension)) result.push_back(&entry);
    }
    return result;
}

const Pfs0Entry *FindEntryByNcaId(const std::vector<Pfs0Entry> &entries,
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

}
