/**
 * @file VersionBlock.cpp
 * @brief VersionBlock RAII 实现（仅用于 BinaryArchive）
 */

#include "../serialization/VersionBlock.h"
#include "../serialization/BinaryArchive.h"

namespace mulan::core {

// ============================================================
// WriteVersionBlock
// ============================================================

WriteVersionBlock::WriteVersionBlock(BinaryOutputArchive& ar, uint32_t version)
    : m_archive(ar) {
    // 写入 VersionBlockStart 标记
    ar.writeTag(TypeTag::VersionBlockStart);

    // 写入版本号（用 writeRaw 直接写入，不写 TypeTag）
    ar.writeRaw(&version, sizeof(version));

    // 记录 size 占位符的位置，写入 4 字节占位
    m_startPos = ar.tell();
    uint32_t placeholder = 0;
    ar.writeRaw(&placeholder, sizeof(placeholder));

    m_dataStartPos = ar.tell();
}

WriteVersionBlock::~WriteVersionBlock() {
    // 回填实际写入的字节数
    uint64_t endPos = m_archive.tell();
    uint32_t actualSize = static_cast<uint32_t>(endPos - m_dataStartPos);
    m_archive.writeAt(m_startPos, &actualSize, sizeof(actualSize));
}

// ============================================================
// ReadVersionBlock
// ============================================================

ReadVersionBlock::ReadVersionBlock(BinaryInputArchive& ar)
    : m_archive(ar)
    , m_version(0)
    , m_blockSize(0)
    , m_endPos(0) {
    // 读取版本号（直接读 raw，不经过 TypeTag）
    auto result = ar.readRaw(&m_version, sizeof(m_version));
    if (!result) return;

    // 读取块大小
    result = ar.readRaw(&m_blockSize, sizeof(m_blockSize));
    if (!result) return;

    // 记录块结束位置
    m_endPos = ar.tell() + m_blockSize;
}

ReadVersionBlock::~ReadVersionBlock() {
    // 跳到块尾（即使没有读完所有数据）
    uint64_t currentPos = m_archive.tell();
    if (currentPos < m_endPos) {
        m_archive.seek(m_endPos);
    }
}

} // namespace mulan::core
