/**
 * @file BinaryArchive.h
 * @brief Binary 格式的 OutputArchive / InputArchive 实现
 *
 * 二进制格式规约：
 *   文件头：Magic "MGAR"(4B) + Version(2B) + Flags(2B)
 *   Payload：TypeTag(1B) + 定长/长度前缀 + 数据
 *
 * TypeTag 标记：
 *   Null=0x00, ObjectStart=0x01, ObjectEnd=0x02,
 *   ArrayStart=0x03, ArrayEnd=0x04,
 *   Key=0x05,
 *   Int32=0x10, Int64=0x11, Float32=0x12, Float64=0x13,
 *   Bool=0x14, String=0x15, Bytes=0x16,
 *   VersionBlockStart=0x20
 *
 * bulk 模式（beginBulkArray with elementStride > 0）：
 *   ArrayStart + BulkFlag(0xFE) + elementStride(4B) + 紧凑数据（无 TypeTag）
 */
#pragma once

#include "../CoreExport.h"
#include "../Serialization/Archive.h"
#include "../Serialization/ArchiveError.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <streambuf>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::core {

// ============================================================
// TypeTag — 二进制格式类型标记
// ============================================================

enum class TypeTag : uint8_t {
    Null               = 0x00,
    ObjectStart        = 0x01,
    ObjectEnd          = 0x02,
    ArrayStart         = 0x03,
    ArrayEnd           = 0x04,
    Key                = 0x05,
    Int32              = 0x10,
    Int64              = 0x11,
    Float32            = 0x12,
    Float64            = 0x13,
    Bool               = 0x14,
    String             = 0x15,
    Bytes              = 0x16,
    VersionBlockStart  = 0x20,
    BulkFlag           = 0xFE,
};

// ============================================================
// 文件头
// ============================================================

struct BinaryFileHeader {
    char magic[4] = {'M', 'G', 'A', 'R'};
    uint32_t version = 1;
    uint32_t flags = 0;     // bit0=big-endian, bit1=has-index-table
};

static_assert(sizeof(BinaryFileHeader) == 12);

// ============================================================
// BinaryOutputArchive
// ============================================================

class CORE_API BinaryOutputArchive final : public OutputArchive {
public:
    /// 写入到内存缓冲区
    BinaryOutputArchive();

    /// 写入到文件
    explicit BinaryOutputArchive(const std::filesystem::path& filePath);

    ~BinaryOutputArchive() override;

    // --- 结构标记 ---

    void beginObject() override;
    void endObject() override;
    void key(std::string_view name) override;
    void beginArray(uint32_t size) override;
    void endArray() override;

    // --- 批量写入（Binary 特化：memcpy 快路径）---
    void beginBulkArray(uint32_t count, uint32_t elementStride) override;
    void endBulkArray() override;

    // --- 原始类型写入 ---

    void write(int32_t value) override;
    void write(int64_t value) override;
    void write(float value) override;
    void write(double value) override;
    void write(bool value) override;
    void write(std::string_view value) override;
    void writeBytes(std::span<const byte> data) override;

    // --- 输出 ---

    /// 获取二进制数据（内存模式）
    const std::vector<byte>& data() const;

    /// 直接写入文件
    bool saveToFile(const std::filesystem::path& filePath) const;

    // --- 流位置（供 VersionBlock 使用）---

    uint64_t tell() const;
    void seek(uint64_t pos);
    void writeAt(uint64_t pos, const void* data, size_t size);

    /// 是否处于 bulk 模式
    bool isBulkMode() const { return m_bulkMode; }

    // --- 友元（VersionBlock 需要访问 writeTag/writeRaw）---
    friend class WriteVersionBlock;
    friend class ReadVersionBlock;

private:
    void writeRaw(const void* data, size_t size);
    void writeTag(TypeTag tag);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_bulkMode = false;
    uint32_t m_bulkStride = 0;
    uint32_t m_bulkCount = 0;
    uint32_t m_bulkWritten = 0;
};

// ============================================================
// BinaryInputArchive
// ============================================================

class CORE_API BinaryInputArchive final : public InputArchive {
public:
    /// 从内存缓冲区读取
    explicit BinaryInputArchive(std::span<const byte> data);

    /// 从文件读取
    explicit BinaryInputArchive(const std::filesystem::path& filePath);

    ~BinaryInputArchive() override;

    // --- 结构标记 ---

    ArchiveResult beginObject() override;
    ArchiveResult endObject() override;
    ArchiveResult key(std::string_view name) override;
    ArchiveResult beginArray(uint32_t& outSize) override;
    ArchiveResult endArray() override;

    // --- 原始类型读取 ---

    ArchiveResult read(int32_t& out) override;
    ArchiveResult read(int64_t& out) override;
    ArchiveResult read(float& out) override;
    ArchiveResult read(double& out) override;
    ArchiveResult read(bool& out) override;
    ArchiveResult read(std::string& out) override;
    ArchiveResult readBytes(std::vector<byte>& out) override;

    // --- 流位置（供 VersionBlock 使用）---

    uint64_t tell() const;
    void seek(uint64_t pos);
    uint64_t remaining() const;

    /// 是否处于 bulk 模式
    bool isBulkMode() const { return m_bulkMode; }

    // --- 友元 ---
    friend class ReadVersionBlock;

private:
    ArchiveResult readRaw(void* out, size_t size);
    ArchiveResult readTag(TypeTag& out);
    ArchiveResult expectTag(TypeTag expected);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    bool m_bulkMode = false;
    uint32_t m_bulkStride = 0;
    uint32_t m_bulkRemaining = 0;
};

} // namespace mulan::Core
