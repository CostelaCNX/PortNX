#pragma once

#include <cstdint>
#include <string>

namespace pinx::download {

std::uint64_t FreeBytes(const std::string &path);

}
