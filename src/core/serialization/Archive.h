/**
 * @file Archive.h
 * @brief OutputArchive / InputArchive 纯虚基类
 *
 * 格式无关的结构化数据 I/O 接口。
 * Archive 不认识任何具体 C++ 类型，只负责原始数据和结构标记。
 */
#pragma once

#include "../CoreExport.h"
#include "ArchiveError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mulan::core {

// ============================================================
// 类型别名
// ============================================================

using byte = std::byte;

// ============================================================
// OutputArchive（纯虚基类 — 写方向）
// ============================================================

class CORE_API OutputArchive {
public:
    virtual ~OutputArchive() = default;

    // --- 结构标记 ---

    virtual void beginObject() = 0;
    virtual void endObject() = 0;

    virtual void key(std::string_view name) = 0;

    virtual void beginArray(uint32_t size) = 0;
    virtual void endArray() = 0;

    // --- 批量写入（BinaryArchive 优化路径，其他格式退化为 beginArray/endArray）---
    virtual void beginBulkArray(uint32_t count, uint32_t elementStride) {
        beginArray(count);
        (void)elementStride;
    }
    virtual void endBulkArray() { endArray(); }

    // --- 原始类型写入 ---

    virtual void write(int32_t value) = 0;
    virtual void write(int64_t value) = 0;
    virtual void write(float value) = 0;
    virtual void write(double value) = 0;
    virtual void write(bool value) = 0;
    virtual void write(std::string_view value) = 0;
    virtual void writeBytes(std::span<const byte> data) = 0;

    // --- 版本信息 ---

    virtual void setVersion(uint32_t version) { m_version = version; }
    virtual uint32_t version() const { return m_version; }

    // --- 语法糖 ---

    template<typename T>
    OutputArchive& operator<<(const T& value);

protected:
    uint32_t m_version = 0;
};

// ============================================================
// InputArchive（纯虚基类 — 读方向）
// ============================================================

class CORE_API InputArchive {
public:
    virtual ~InputArchive() = default;

    // --- 结构标记 ---

    virtual ArchiveResult beginObject() = 0;
    virtual ArchiveResult endObject() = 0;

    virtual ArchiveResult key(std::string_view name) = 0;

    /// @return 数组元素个数
    virtual ArchiveResult beginArray(uint32_t& outSize) = 0;
    virtual ArchiveResult endArray() = 0;

    // --- 原始类型读取 ---

    virtual ArchiveResult read(int32_t& out) = 0;
    virtual ArchiveResult read(int64_t& out) = 0;
    virtual ArchiveResult read(float& out) = 0;
    virtual ArchiveResult read(double& out) = 0;
    virtual ArchiveResult read(bool& out) = 0;
    virtual ArchiveResult read(std::string& out) = 0;
    virtual ArchiveResult readBytes(std::vector<byte>& out) = 0;

    // --- 字段兼容性 ---

    /// 判断当前对象中是否存在指定 key。
    /// 默认返回 true（BinaryArchive 无法按 key 查找）。
    /// JsonInputArchive 重写以实现字段级前向兼容。
    virtual bool hasKey(std::string_view /*name*/) const { return true; }

    /// 跳过当前值。默认空实现，用于兼容旧格式废弃字段。
    virtual ArchiveResult skipValue() { return {}; }

    // --- 版本信息 ---

    virtual void setVersion(uint32_t version) { m_version = version; }
    virtual uint32_t version() const { return m_version; }

    // --- 错误状态 ---

    bool hasError() const { return m_hasError; }
    const std::string& errorMessage() const { return m_errorMessage; }

    // --- 语法糖 ---

    template<typename T>
    InputArchive& operator>>(T& value);

protected:
    uint32_t m_version = 0;
    /// 子类调用：设置错误状态，后续操作短路返回
    void setError(std::string_view msg) {
        if (!m_hasError) {
            m_hasError = true;
            m_errorMessage = msg;
            // 错误发生时自动记录路径上下文
            m_errorMessage += " at " + buildPath();
        }
    }

    // --- 路径上下文追踪（子类在 key/array 操作时调用）---

    /// key() 解析成功时调用
    void onPathKey(std::string_view name);
    /// 进入数组时调用
    void onPathBeginArray();
    /// 数组元素前进时调用（每读完一个元素）
    void onPathAdvanceIndex();
    /// 离开数组或对象时调用
    void onPathPop();

private:
    bool m_hasError = false;
    std::string m_errorMessage;

    std::string buildPath() const;

    // 路径栈
    struct PathEntry {
        enum Type { Key, Array };
        Type type;
        std::string name;
        uint32_t index = 0;
    };
    std::vector<PathEntry> m_pathStack;
};

} // namespace mulan::Core
