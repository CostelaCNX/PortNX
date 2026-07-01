#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pinx::install {

struct Pfs0Entry {
    std::string   name;
    std::uint64_t offset;
    std::uint64_t size;
};

bool ParsePfs0(const std::string &path,
               std::vector<Pfs0Entry> &entries,
               std::uint64_t &data_offset);

const Pfs0Entry *FindEntryByExtension(const std::vector<Pfs0Entry> &entries,
                                       const std::string &extension);

std::vector<const Pfs0Entry *> FindEntriesByExtension(
    const std::vector<Pfs0Entry> &entries,
    const std::string &extension);

const Pfs0Entry *FindEntryByNcaId(const std::vector<Pfs0Entry> &entries,
                                   const std::string &nca_id_hex);

}
