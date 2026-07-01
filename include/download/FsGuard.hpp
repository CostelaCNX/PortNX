#pragma once

#include <cstdint>
#include <string>

namespace pinx::download {

// Bytes free on the volume holding |path| (via statvfs). 0 if it can't query.
std::uint64_t FreeBytes(const std::string &path);

// Whether |size| would hit the 4 GiB single-file limit of FAT32. Advisory:
// the SD's filesystem type isn't probed, so this only warns; an actual FAT32
// write past 4 GiB surfaces as a write error during download.
bool ExceedsFat32(std::uint64_t size);

}
