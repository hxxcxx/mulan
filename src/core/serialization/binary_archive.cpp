#include "binary_archive.h"

#include <fstream>
#include <stdexcept>

namespace mulan::core {

// ============================================================
// BinaryOutputArchive::Impl
// ============================================================

struct BinaryOutputArchive::Impl {
    std::vector<byte> buffer;
    std::filesystem::path filePath;
    bool fileMode = false;

    Impl() {
        // 预分配文件头空间
        buffer.resize(sizeof(BinaryFileHeader));
        BinaryFileHeader header;
        std::memcpy(buffer.data(), &header, sizeof(header));
    }
};

// ============================================================
// BinaryOutputArchive
// ============================================================

BinaryOutputArchive::BinaryOutputArchive() : impl_(std::make_unique<Impl>()) {
}

BinaryOutputArchive::BinaryOutputArchive(const std::filesystem::path& filePath) : impl_(std::make_unique<Impl>()) {
    impl_->filePath = filePath;
    impl_->fileMode = true;
}

BinaryOutputArchive::~BinaryOutputArchive() = default;

void BinaryOutputArchive::writeRaw(const void* data, size_t size) {
    auto& buf = impl_->buffer;
    auto ptr = static_cast<const byte*>(data);
    buf.insert(buf.end(), ptr, ptr + size);
}

void BinaryOutputArchive::writeTag(TypeTag tag) {
    auto v = static_cast<uint8_t>(tag);
    writeRaw(&v, sizeof(v));
}

void BinaryOutputArchive::beginObject() {
    if (bulk_mode_)
        return;
    writeTag(TypeTag::ObjectStart);
}

void BinaryOutputArchive::endObject() {
    if (bulk_mode_)
        return;
    writeTag(TypeTag::ObjectEnd);
}

void BinaryOutputArchive::key(std::string_view name) {
    if (bulk_mode_)
        return;
    writeTag(TypeTag::Key);
    uint32_t len = static_cast<uint32_t>(name.size());
    writeRaw(&len, sizeof(len));
    writeRaw(name.data(), name.size());
}

void BinaryOutputArchive::beginArray(uint32_t size) {
    if (bulk_mode_)
        return;
    writeTag(TypeTag::ArrayStart);
    writeRaw(&size, sizeof(size));
}

void BinaryOutputArchive::endArray() {
    if (bulk_mode_)
        return;
    writeTag(TypeTag::ArrayEnd);
}

void BinaryOutputArchive::beginBulkArray(uint32_t count, uint32_t elementStride) {
    if (elementStride == 0) {
        // 退化为普通数组
        beginArray(count);
        return;
    }

    writeTag(TypeTag::ArrayStart);
    writeRaw(&count, sizeof(count));
    writeTag(TypeTag::BulkFlag);
    writeRaw(&elementStride, sizeof(elementStride));

    bulk_mode_ = true;
    bulk_stride_ = elementStride;
    bulk_count_ = count;
    bulk_written_ = 0;
}

void BinaryOutputArchive::endBulkArray() {
    bulk_mode_ = false;
    writeTag(TypeTag::ArrayEnd);
}

void BinaryOutputArchive::write(int32_t value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(&value, sizeof(value));
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Int32);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(int64_t value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(&value, sizeof(value));
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Int64);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(float value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(&value, sizeof(value));
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Float32);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(double value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(&value, sizeof(value));
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Float64);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(bool value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        uint8_t v = value ? 1 : 0;
        writeRaw(&v, sizeof(v));
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Bool);
    uint8_t v = value ? 1 : 0;
    writeRaw(&v, sizeof(v));
}

void BinaryOutputArchive::write(std::string_view value) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(value.data(), value.size());
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::String);
    uint32_t len = static_cast<uint32_t>(value.size());
    writeRaw(&len, sizeof(len));
    writeRaw(value.data(), value.size());
}

void BinaryOutputArchive::writeBytes(std::span<const byte> data) {
    if (bulk_mode_) {
        if (bulk_written_ >= bulk_count_)
            return;
        writeRaw(data.data(), data.size());
        ++bulk_written_;
        return;
    }
    writeTag(TypeTag::Bytes);
    uint32_t len = static_cast<uint32_t>(data.size());
    writeRaw(&len, sizeof(len));
    writeRaw(data.data(), data.size());
}

const std::vector<byte>& BinaryOutputArchive::data() const {
    return impl_->buffer;
}

bool BinaryOutputArchive::saveToFile(const std::filesystem::path& filePath) const {
    auto path = filePath.empty() ? impl_->filePath : filePath;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs)
        return false;
    ofs.write(reinterpret_cast<const char*>(impl_->buffer.data()), static_cast<std::streamsize>(impl_->buffer.size()));
    return ofs.good();
}

uint64_t BinaryOutputArchive::tell() const {
    return impl_->buffer.size();
}

void BinaryOutputArchive::seek(uint64_t pos) {
    if (pos <= impl_->buffer.size()) {
        // BinaryOutputArchive 是 append-only，seek 仅用于 VersionBlock 回填
    }
}

void BinaryOutputArchive::writeAt(uint64_t pos, const void* data, size_t size) {
    if (pos + size <= impl_->buffer.size()) {
        std::memcpy(impl_->buffer.data() + pos, data, size);
    }
}

// ============================================================
// BinaryInputArchive::Impl
// ============================================================

struct BinaryInputArchive::Impl {
    std::vector<byte> buffer;
    uint64_t pos = 0;

    bool hasMore(size_t n) const { return pos + n <= buffer.size(); }

    const byte* current() const { return buffer.data() + pos; }

    void advance(size_t n) { pos += n; }
};

// ============================================================
// BinaryInputArchive
// ============================================================

BinaryInputArchive::BinaryInputArchive(std::span<const byte> data) : impl_(std::make_unique<Impl>()) {
    impl_->buffer.assign(data.begin(), data.end());

    // 读取并验证文件头
    if (impl_->buffer.size() < sizeof(BinaryFileHeader)) {
        setError("Binary data too small for header");
        return;
    }

    BinaryFileHeader header;
    std::memcpy(&header, impl_->buffer.data(), sizeof(header));
    if (std::memcmp(header.magic, "MGAR", 4) != 0) {
        setError("Invalid binary archive magic");
        return;
    }

    impl_->pos = sizeof(BinaryFileHeader);
    setVersion(header.version);
}

BinaryInputArchive::BinaryInputArchive(const std::filesystem::path& filePath) : impl_(std::make_unique<Impl>()) {
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs) {
        setError("Cannot open file: " + filePath.string());
        return;
    }

    auto size = ifs.tellg();
    ifs.seekg(0);
    impl_->buffer.resize(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(impl_->buffer.data()), static_cast<std::streamsize>(size));

    if (!ifs) {
        setError("Failed to read file: " + filePath.string());
        return;
    }

    // 读取并验证文件头
    if (impl_->buffer.size() < sizeof(BinaryFileHeader)) {
        setError("Binary data too small for header");
        return;
    }

    BinaryFileHeader header;
    std::memcpy(&header, impl_->buffer.data(), sizeof(header));
    if (std::memcmp(header.magic, "MGAR", 4) != 0) {
        setError("Invalid binary archive magic");
        return;
    }

    impl_->pos = sizeof(BinaryFileHeader);
    setVersion(header.version);
}

BinaryInputArchive::~BinaryInputArchive() = default;

ArchiveResult BinaryInputArchive::readRaw(void* out, size_t size) {
    if (hasError())
        return {};
    if (!impl_->hasMore(size)) {
        setError("Unexpected end of binary data");
        return std::unexpected(ArchiveError::corrupted("Unexpected end of binary data"));
    }
    std::memcpy(out, impl_->current(), size);
    impl_->advance(size);
    return {};
}

ArchiveResult BinaryInputArchive::readTag(TypeTag& out) {
    uint8_t raw = 0;
    auto result = readRaw(&raw, sizeof(raw));
    if (!result)
        return result;
    out = static_cast<TypeTag>(raw);
    return {};
}

ArchiveResult BinaryInputArchive::expectTag(TypeTag expected) {
    TypeTag actual;
    auto result = readTag(actual);
    if (!result)
        return result;
    if (actual != expected) {
        return std::unexpected(ArchiveError::corrupted("Expected tag " + std::to_string(static_cast<int>(expected)) +
                                                       " but got " + std::to_string(static_cast<int>(actual))));
    }
    return {};
}

ArchiveResult BinaryInputArchive::beginObject() {
    if (hasError())
        return {};
    return expectTag(TypeTag::ObjectStart);
}

ArchiveResult BinaryInputArchive::endObject() {
    if (hasError())
        return {};
    return expectTag(TypeTag::ObjectEnd);
}

ArchiveResult BinaryInputArchive::key(std::string_view name) {
    if (hasError())
        return {};

    // Binary 格式按顺序读取，key 必须匹配
    auto result = expectTag(TypeTag::Key);
    if (!result)
        return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result)
        return result;

    // Binary 格式严格匹配 key 名称
    if (len != name.size() || !impl_->hasMore(len)) {
        // key 不匹配，不回退——这是严格顺序格式
        if (impl_->hasMore(len))
            impl_->advance(len);
        return std::unexpected(ArchiveError::missingKey(name));
    }

    // 比较内容
    if (std::memcmp(impl_->current(), name.data(), len) != 0) {
        impl_->advance(len);
        return std::unexpected(ArchiveError::missingKey(name));
    }

    impl_->advance(len);
    return {};
}

ArchiveResult BinaryInputArchive::beginArray(uint32_t& outSize) {
    if (hasError())
        return {};

    auto result = expectTag(TypeTag::ArrayStart);
    if (!result)
        return result;

    result = readRaw(&outSize, sizeof(outSize));
    if (!result)
        return result;

    // 检查是否是 bulk 数组
    if (impl_->hasMore(1)) {
        uint64_t savedPos = impl_->pos;
        TypeTag nextTag;
        result = readTag(nextTag);
        if (!result)
            return result;

        if (nextTag == TypeTag::BulkFlag) {
            uint32_t stride = 0;
            result = readRaw(&stride, sizeof(stride));
            if (!result)
                return result;

            bulk_mode_ = true;
            bulk_stride_ = stride;
            bulk_remaining_ = outSize;
            return {};
        }

        // 不是 bulk，回退
        impl_->pos = savedPos;
    }

    return {};
}

ArchiveResult BinaryInputArchive::endArray() {
    if (bulk_mode_) {
        bulk_mode_ = false;
        // bulk 模式结束时可能还有剩余未读数据，跳过
        return expectTag(TypeTag::ArrayEnd);
    }
    if (hasError())
        return {};
    return expectTag(TypeTag::ArrayEnd);
}

ArchiveResult BinaryInputArchive::read(int32_t& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        if (bulk_remaining_ == 0) {
            return std::unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(int32_t));
        if (!result)
            return result;
        --bulk_remaining_;
        return {};
    }

    auto result = expectTag(TypeTag::Int32);
    if (!result)
        return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(int64_t& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        if (bulk_remaining_ == 0) {
            return std::unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(int64_t));
        if (!result)
            return result;
        --bulk_remaining_;
        return {};
    }

    auto result = expectTag(TypeTag::Int64);
    if (!result)
        return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(float& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        if (bulk_remaining_ == 0) {
            return std::unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(float));
        if (!result)
            return result;
        --bulk_remaining_;
        return {};
    }

    auto result = expectTag(TypeTag::Float32);
    if (!result)
        return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(double& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        if (bulk_remaining_ == 0) {
            return std::unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(double));
        if (!result)
            return result;
        --bulk_remaining_;
        return {};
    }

    auto result = expectTag(TypeTag::Float64);
    if (!result)
        return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(bool& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        if (bulk_remaining_ == 0) {
            return std::unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        uint8_t v = 0;
        auto result = readRaw(&v, sizeof(v));
        if (!result)
            return result;
        out = (v != 0);
        --bulk_remaining_;
        return {};
    }

    auto result = expectTag(TypeTag::Bool);
    if (!result)
        return result;
    uint8_t v = 0;
    result = readRaw(&v, sizeof(v));
    if (!result)
        return result;
    out = (v != 0);
    return {};
}

ArchiveResult BinaryInputArchive::read(std::string& out) {
    if (hasError())
        return {};

    if (bulk_mode_) {
        // bulk 模式下 string 无法按 stride 读取，报错
        return std::unexpected(ArchiveError::corrupted("Cannot read string in bulk mode"));
    }

    auto result = expectTag(TypeTag::String);
    if (!result)
        return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result)
        return result;

    if (!impl_->hasMore(len)) {
        return std::unexpected(ArchiveError::corrupted("String data truncated"));
    }

    out.assign(reinterpret_cast<const char*>(impl_->current()), len);
    impl_->advance(len);
    return {};
}

ArchiveResult BinaryInputArchive::readBytes(std::vector<byte>& out) {
    if (hasError())
        return {};

    auto result = expectTag(TypeTag::Bytes);
    if (!result)
        return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result)
        return result;

    if (!impl_->hasMore(len)) {
        return std::unexpected(ArchiveError::corrupted("Bytes data truncated"));
    }

    out.assign(impl_->current(), impl_->current() + len);
    impl_->advance(len);
    return {};
}

uint64_t BinaryInputArchive::tell() const {
    return impl_->pos;
}

void BinaryInputArchive::seek(uint64_t pos) {
    if (pos <= impl_->buffer.size()) {
        impl_->pos = pos;
    }
}

uint64_t BinaryInputArchive::remaining() const {
    return impl_->buffer.size() - impl_->pos;
}

}  // namespace mulan::core
