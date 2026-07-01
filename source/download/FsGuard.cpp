#include <download/FsGuard.hpp>

#include <sys/statvfs.h>

namespace pinx::download {

std::uint64_t FreeBytes(const std::string &path) {
    struct statvfs st;
    if(statvfs(path.c_str(), &st) != 0) {
        return 0;
    }
    const std::uint64_t unit = st.f_frsize ? st.f_frsize : st.f_bsize;
    return static_cast<std::uint64_t>(st.f_bavail) * unit;
}

bool ExceedsFat32(std::uint64_t size) {
    return size >= (4ULL * 1024ULL * 1024ULL * 1024ULL);
}

}
