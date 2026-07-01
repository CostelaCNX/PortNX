#pragma once

#include <install/ByteBuffer.hpp>
#include <install/NcaStructs.hpp>
#include <install/NcmWrapper.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <switch.h>

namespace pinx::install {

class ContentMeta {
    public:
        ContentMeta() = default;
        ContentMeta(const std::uint8_t *data, std::size_t size);

        PackagedContentMetaHeader   GetHeader() const;
        NcmContentMetaKey           GetContentMetaKey() const;
        std::vector<NcmContentInfo> GetContentInfos() const;

        bool GetInstallContentMeta(ByteBuffer &out,
                                   const NcmContentInfo &cnmt_content_info,
                                   bool ignore_required_firmware_version) const;

    private:
        std::vector<std::uint8_t> bytes_;
};

bool ReadCnmtFromInstalledNca(ContentStorage &storage,
                               const NcmContentId &nca_id,
                               std::vector<std::uint8_t> &out_cnmt_data);

std::string  NcaIdToString(const NcmContentId &id);
NcmContentId NcaIdFromString(const std::string &hex);

}
