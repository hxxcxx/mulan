/**
 * @file VersionBlock.h
 * @brief RAII 版本块助手（仅用于 BinaryArchive）
 *
 * 写模式：写入版本号 + size 占位 → 写入数据 → 析构时回填实际 size。
 * 读模式：读取版本号 + size → 记录块结束位置 → 析构时跳转到块尾。
 *
 * Reader 遇到不认识的 version 时按 size 跳过整个块，天然支持前向兼容。
 * JSON 格式不使用 VersionBlock，其版本兼容依赖 hasKey() + skipValue()。
 */
#pragma once

#include "../CoreExport.h"
#include "ArchiveError.h"

namespace mulan::core {

// ============================================================
// 前向声明（Binary 格式的完整 Archive 定义在 BinaryArchive.h）
// ============================================================

class BinaryOutputArchive;
class BinaryInputArchive;

// ============================================================
// WriteVersionBlock — 写模式 RAII
// ============================================================

/// 构造时写入 version + size 占位符，析构时回填实际写入的字节数。
/// 用法：
///   {
///       WriteVersionBlock block(ar, 3);  // 写 version=3 + 4字节 size 占位
///       ar << data1 << data2;
///   }  // 析构：回填实际 size
class CORE_API WriteVersionBlock {
public:
    WriteVersionBlock(BinaryOutputArchive& ar, uint32_t version);
    ~WriteVersionBlock();

    WriteVersionBlock(const WriteVersionBlock&) = delete;
    WriteVersionBlock& operator=(const WriteVersionBlock&) = delete;

private:
    BinaryOutputArchive& m_archive;
    uint64_t m_startPos;      // size 占位符的位置
    uint64_t m_dataStartPos;  // 数据开始的位置
};

// ============================================================
// ReadVersionBlock — 读模式 RAII
// ============================================================

/// 构造时读取 version + size，析构时跳转到块尾。
/// 用法：
///   {
///       ReadVersionBlock block(ar);
///       if (block.version() <= kSupportedVersion) {
///           ar >> data1 >> data2;
///       }
///       // 即使不读取数据，析构时也会跳到块尾
///   }
class CORE_API ReadVersionBlock {
public:
    explicit ReadVersionBlock(BinaryInputArchive& ar);
    ~ReadVersionBlock();

    uint32_t version() const { return m_version; }
    uint32_t blockSize() const { return m_blockSize; }

    ReadVersionBlock(const ReadVersionBlock&) = delete;
    ReadVersionBlock& operator=(const ReadVersionBlock&) = delete;

private:
    BinaryInputArchive& m_archive;
    uint32_t m_version;
    uint32_t m_blockSize;
    uint64_t m_endPos;        // 块结束位置
};

} // namespace mulan::core
