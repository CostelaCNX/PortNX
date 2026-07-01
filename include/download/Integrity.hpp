#pragma once

#include <string>

namespace pinx::download {

// Lowercase hex SHA-256 of a file, streamed in chunks. Empty on error.
std::string Sha256File(const std::string &path);

}
