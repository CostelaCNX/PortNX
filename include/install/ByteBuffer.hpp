#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace pinx::install {

class ByteBuffer {
    public:
        ByteBuffer() = default;

        void Append(const void *src, std::size_t length) {
            const auto *bytes = static_cast<const std::uint8_t *>(src);
            data_.insert(data_.end(), bytes, bytes + length);
        }

        template <typename T>
        void AppendValue(const T &value) {
            Append(&value, sizeof(T));
        }

        void Resize(std::size_t new_size) {
            data_.resize(new_size, 0);
        }

        std::uint8_t       *GetData()       { return data_.data(); }
        const std::uint8_t *GetData() const { return data_.data(); }
        std::size_t          GetSize() const { return data_.size(); }
        bool                 IsEmpty() const { return data_.empty(); }

        void Clear() { data_.clear(); }

    private:
        std::vector<std::uint8_t> data_;
};

}
