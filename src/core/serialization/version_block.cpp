#include "../serialization/version_block.h"
#include "../serialization/binary_archive.h"

namespace mulan::core {

// ============================================================
// WriteVersionBlock
// ============================================================

WriteVersionBlock::WriteVersionBlock(BinaryOutputArchive& ar, uint32_t version) : archive_(ar) {
    // 写入 VersionBlockStart 标记
    ar.writeTag(TypeTag::VersionBlockStart);

    // 写入版本号（用 writeRaw 直接写入，不写 TypeTag）
    ar.writeRaw(&version, sizeof(version));

    // 记录 size 占位符的位置，写入 4 字节占位
    start_pos_ = ar.tell();
    uint32_t placeholder = 0;
    ar.writeRaw(&placeholder, sizeof(placeholder));

    data_start_pos_ = ar.tell();
}

WriteVersionBlock::~WriteVersionBlock() {
    // 回填实际写入的字节数
    uint64_t endPos = archive_.tell();
    uint32_t actualSize = static_cast<uint32_t>(endPos - data_start_pos_);
    archive_.writeAt(start_pos_, &actualSize, sizeof(actualSize));
}

// ============================================================
// ReadVersionBlock
// ============================================================

ReadVersionBlock::ReadVersionBlock(BinaryInputArchive& ar) : archive_(ar), version_(0), block_size_(0), end_pos_(0) {
    // 读取版本号（直接读 raw，不经过 TypeTag）
    auto result = ar.readRaw(&version_, sizeof(version_));
    if (!result)
        return;

    // 读取块大小
    result = ar.readRaw(&block_size_, sizeof(block_size_));
    if (!result)
        return;

    // 记录块结束位置
    end_pos_ = ar.tell() + block_size_;
}

ReadVersionBlock::~ReadVersionBlock() {
    // 跳到块尾（即使没有读完所有数据）
    uint64_t currentPos = archive_.tell();
    if (currentPos < end_pos_) {
        archive_.seek(end_pos_);
    }
}

}  // namespace mulan::core
