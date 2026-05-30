/**
 * @file BinaryArchive.cpp
 * @brief Binary 格式 Archive 完整实现
 */

#include "BinaryArchive.h"

#include <cstring>
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

BinaryOutputArchive::BinaryOutputArchive()
    : m_impl(std::make_unique<Impl>()) {}

BinaryOutputArchive::BinaryOutputArchive(const std::filesystem::path& filePath)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->filePath = filePath;
    m_impl->fileMode = true;
}

BinaryOutputArchive::~BinaryOutputArchive() = default;

void BinaryOutputArchive::writeRaw(const void* data, size_t size) {
    auto& buf = m_impl->buffer;
    auto ptr = static_cast<const byte*>(data);
    buf.insert(buf.end(), ptr, ptr + size);
}

void BinaryOutputArchive::writeTag(TypeTag tag) {
    auto v = static_cast<uint8_t>(tag);
    writeRaw(&v, sizeof(v));
}

void BinaryOutputArchive::beginObject() {
    if (m_bulkMode) return;
    writeTag(TypeTag::ObjectStart);
}

void BinaryOutputArchive::endObject() {
    if (m_bulkMode) return;
    writeTag(TypeTag::ObjectEnd);
}

void BinaryOutputArchive::key(std::string_view name) {
    if (m_bulkMode) return;
    writeTag(TypeTag::Key);
    uint32_t len = static_cast<uint32_t>(name.size());
    writeRaw(&len, sizeof(len));
    writeRaw(name.data(), name.size());
}

void BinaryOutputArchive::beginArray(uint32_t size) {
    if (m_bulkMode) return;
    writeTag(TypeTag::ArrayStart);
    writeRaw(&size, sizeof(size));
}

void BinaryOutputArchive::endArray() {
    if (m_bulkMode) return;
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

    m_bulkMode = true;
    m_bulkStride = elementStride;
    m_bulkCount = count;
    m_bulkWritten = 0;
}

void BinaryOutputArchive::endBulkArray() {
    m_bulkMode = false;
    writeTag(TypeTag::ArrayEnd);
}

void BinaryOutputArchive::write(int32_t value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(&value, sizeof(value));
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Int32);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(int64_t value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(&value, sizeof(value));
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Int64);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(float value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(&value, sizeof(value));
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Float32);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(double value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(&value, sizeof(value));
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Float64);
    writeRaw(&value, sizeof(value));
}

void BinaryOutputArchive::write(bool value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        uint8_t v = value ? 1 : 0;
        writeRaw(&v, sizeof(v));
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Bool);
    uint8_t v = value ? 1 : 0;
    writeRaw(&v, sizeof(v));
}

void BinaryOutputArchive::write(std::string_view value) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(value.data(), value.size());
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::String);
    uint32_t len = static_cast<uint32_t>(value.size());
    writeRaw(&len, sizeof(len));
    writeRaw(value.data(), value.size());
}

void BinaryOutputArchive::writeBytes(std::span<const byte> data) {
    if (m_bulkMode) {
        if (m_bulkWritten >= m_bulkCount) return;
        writeRaw(data.data(), data.size());
        ++m_bulkWritten;
        return;
    }
    writeTag(TypeTag::Bytes);
    uint32_t len = static_cast<uint32_t>(data.size());
    writeRaw(&len, sizeof(len));
    writeRaw(data.data(), data.size());
}

const std::vector<byte>& BinaryOutputArchive::data() const {
    return m_impl->buffer;
}

bool BinaryOutputArchive::saveToFile(const std::filesystem::path& filePath) const {
    auto path = filePath.empty() ? m_impl->filePath : filePath;
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) return false;
    ofs.write(reinterpret_cast<const char*>(m_impl->buffer.data()),
              static_cast<std::streamsize>(m_impl->buffer.size()));
    return ofs.good();
}

uint64_t BinaryOutputArchive::tell() const {
    return m_impl->buffer.size();
}

void BinaryOutputArchive::seek(uint64_t pos) {
    if (pos <= m_impl->buffer.size()) {
        // BinaryOutputArchive 是 append-only，seek 仅用于 VersionBlock 回填
    }
}

void BinaryOutputArchive::writeAt(uint64_t pos, const void* data, size_t size) {
    if (pos + size <= m_impl->buffer.size()) {
        std::memcpy(m_impl->buffer.data() + pos, data, size);
    }
}

// ============================================================
// BinaryInputArchive::Impl
// ============================================================

struct BinaryInputArchive::Impl {
    std::vector<byte> buffer;
    uint64_t pos = 0;

    bool hasMore(size_t n) const {
        return pos + n <= buffer.size();
    }

    const byte* current() const {
        return buffer.data() + pos;
    }

    void advance(size_t n) {
        pos += n;
    }
};

// ============================================================
// BinaryInputArchive
// ============================================================

BinaryInputArchive::BinaryInputArchive(std::span<const byte> data)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->buffer.assign(data.begin(), data.end());

    // 读取并验证文件头
    if (m_impl->buffer.size() < sizeof(BinaryFileHeader)) {
        setError("Binary data too small for header");
        return;
    }

    BinaryFileHeader header;
    std::memcpy(&header, m_impl->buffer.data(), sizeof(header));
    if (std::memcmp(header.magic, "MGAR", 4) != 0) {
        setError("Invalid binary archive magic");
        return;
    }

    m_impl->pos = sizeof(BinaryFileHeader);
    setVersion(header.version);
}

BinaryInputArchive::BinaryInputArchive(const std::filesystem::path& filePath)
    : m_impl(std::make_unique<Impl>()) {
    std::ifstream ifs(filePath, std::ios::binary | std::ios::ate);
    if (!ifs) {
        setError("Cannot open file: " + filePath.string());
        return;
    }

    auto size = ifs.tellg();
    ifs.seekg(0);
    m_impl->buffer.resize(static_cast<size_t>(size));
    ifs.read(reinterpret_cast<char*>(m_impl->buffer.data()),
             static_cast<std::streamsize>(size));

    if (!ifs) {
        setError("Failed to read file: " + filePath.string());
        return;
    }

    // 读取并验证文件头
    if (m_impl->buffer.size() < sizeof(BinaryFileHeader)) {
        setError("Binary data too small for header");
        return;
    }

    BinaryFileHeader header;
    std::memcpy(&header, m_impl->buffer.data(), sizeof(header));
    if (std::memcmp(header.magic, "MGAR", 4) != 0) {
        setError("Invalid binary archive magic");
        return;
    }

    m_impl->pos = sizeof(BinaryFileHeader);
    setVersion(header.version);
}

BinaryInputArchive::~BinaryInputArchive() = default;

ArchiveResult BinaryInputArchive::readRaw(void* out, size_t size) {
    if (hasError()) return {};
    if (!m_impl->hasMore(size)) {
        setError("Unexpected end of binary data");
        return tl::make_unexpected(
            ArchiveError::corrupted("Unexpected end of binary data"));
    }
    std::memcpy(out, m_impl->current(), size);
    m_impl->advance(size);
    return {};
}

ArchiveResult BinaryInputArchive::readTag(TypeTag& out) {
    uint8_t raw = 0;
    auto result = readRaw(&raw, sizeof(raw));
    if (!result) return result;
    out = static_cast<TypeTag>(raw);
    return {};
}

ArchiveResult BinaryInputArchive::expectTag(TypeTag expected) {
    TypeTag actual;
    auto result = readTag(actual);
    if (!result) return result;
    if (actual != expected) {
        return tl::make_unexpected(
            ArchiveError::corrupted("Expected tag " +
                                    std::to_string(static_cast<int>(expected)) +
                                    " but got " +
                                    std::to_string(static_cast<int>(actual))));
    }
    return {};
}

ArchiveResult BinaryInputArchive::beginObject() {
    if (hasError()) return {};
    return expectTag(TypeTag::ObjectStart);
}

ArchiveResult BinaryInputArchive::endObject() {
    if (hasError()) return {};
    return expectTag(TypeTag::ObjectEnd);
}

ArchiveResult BinaryInputArchive::key(std::string_view name) {
    if (hasError()) return {};

    // Binary 格式按顺序读取，key 必须匹配
    auto result = expectTag(TypeTag::Key);
    if (!result) return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result) return result;

    // Binary 格式严格匹配 key 名称
    if (len != name.size() || !m_impl->hasMore(len)) {
        // key 不匹配，不回退——这是严格顺序格式
        if (m_impl->hasMore(len)) m_impl->advance(len);
        return tl::make_unexpected(ArchiveError::missingKey(name));
    }

    // 比较内容
    if (std::memcmp(m_impl->current(), name.data(), len) != 0) {
        m_impl->advance(len);
        return tl::make_unexpected(ArchiveError::missingKey(name));
    }

    m_impl->advance(len);
    return {};
}

ArchiveResult BinaryInputArchive::beginArray(uint32_t& outSize) {
    if (hasError()) return {};

    auto result = expectTag(TypeTag::ArrayStart);
    if (!result) return result;

    result = readRaw(&outSize, sizeof(outSize));
    if (!result) return result;

    // 检查是否是 bulk 数组
    if (m_impl->hasMore(1)) {
        uint64_t savedPos = m_impl->pos;
        TypeTag nextTag;
        result = readTag(nextTag);
        if (!result) return result;

        if (nextTag == TypeTag::BulkFlag) {
            uint32_t stride = 0;
            result = readRaw(&stride, sizeof(stride));
            if (!result) return result;

            m_bulkMode = true;
            m_bulkStride = stride;
            m_bulkRemaining = outSize;
            return {};
        }

        // 不是 bulk，回退
        m_impl->pos = savedPos;
    }

    return {};
}

ArchiveResult BinaryInputArchive::endArray() {
    if (m_bulkMode) {
        m_bulkMode = false;
        // bulk 模式结束时可能还有剩余未读数据，跳过
        return expectTag(TypeTag::ArrayEnd);
    }
    if (hasError()) return {};
    return expectTag(TypeTag::ArrayEnd);
}

ArchiveResult BinaryInputArchive::read(int32_t& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        if (m_bulkRemaining == 0) {
            return tl::make_unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(int32_t));
        if (!result) return result;
        --m_bulkRemaining;
        return {};
    }

    auto result = expectTag(TypeTag::Int32);
    if (!result) return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(int64_t& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        if (m_bulkRemaining == 0) {
            return tl::make_unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(int64_t));
        if (!result) return result;
        --m_bulkRemaining;
        return {};
    }

    auto result = expectTag(TypeTag::Int64);
    if (!result) return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(float& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        if (m_bulkRemaining == 0) {
            return tl::make_unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(float));
        if (!result) return result;
        --m_bulkRemaining;
        return {};
    }

    auto result = expectTag(TypeTag::Float32);
    if (!result) return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(double& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        if (m_bulkRemaining == 0) {
            return tl::make_unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        auto result = readRaw(&out, sizeof(double));
        if (!result) return result;
        --m_bulkRemaining;
        return {};
    }

    auto result = expectTag(TypeTag::Float64);
    if (!result) return result;
    return readRaw(&out, sizeof(out));
}

ArchiveResult BinaryInputArchive::read(bool& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        if (m_bulkRemaining == 0) {
            return tl::make_unexpected(ArchiveError::corrupted("Bulk array overrun"));
        }
        uint8_t v = 0;
        auto result = readRaw(&v, sizeof(v));
        if (!result) return result;
        out = (v != 0);
        --m_bulkRemaining;
        return {};
    }

    auto result = expectTag(TypeTag::Bool);
    if (!result) return result;
    uint8_t v = 0;
    result = readRaw(&v, sizeof(v));
    if (!result) return result;
    out = (v != 0);
    return {};
}

ArchiveResult BinaryInputArchive::read(std::string& out) {
    if (hasError()) return {};

    if (m_bulkMode) {
        // bulk 模式下 string 无法按 stride 读取，报错
        return tl::make_unexpected(
            ArchiveError::corrupted("Cannot read string in bulk mode"));
    }

    auto result = expectTag(TypeTag::String);
    if (!result) return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result) return result;

    if (!m_impl->hasMore(len)) {
        return tl::make_unexpected(ArchiveError::corrupted("String data truncated"));
    }

    out.assign(reinterpret_cast<const char*>(m_impl->current()), len);
    m_impl->advance(len);
    return {};
}

ArchiveResult BinaryInputArchive::readBytes(std::vector<byte>& out) {
    if (hasError()) return {};

    auto result = expectTag(TypeTag::Bytes);
    if (!result) return result;

    uint32_t len = 0;
    result = readRaw(&len, sizeof(len));
    if (!result) return result;

    if (!m_impl->hasMore(len)) {
        return tl::make_unexpected(ArchiveError::corrupted("Bytes data truncated"));
    }

    out.assign(m_impl->current(), m_impl->current() + len);
    m_impl->advance(len);
    return {};
}

uint64_t BinaryInputArchive::tell() const {
    return m_impl->pos;
}

void BinaryInputArchive::seek(uint64_t pos) {
    if (pos <= m_impl->buffer.size()) {
        m_impl->pos = pos;
    }
}

uint64_t BinaryInputArchive::remaining() const {
    return m_impl->buffer.size() - m_impl->pos;
}

} // namespace mulan::core
