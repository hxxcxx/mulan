/**
 * @file ArchiveError.h
 * @brief 序列化错误类型定义
 */
#pragma once

#include "../CoreExport.h"

#include <cstdint>
#include <string>
#include <string_view>

#include <tl/expected.hpp>

namespace mulan::core {

// ============================================================
// ArchiveError — 序列化失败时的错误描述
// ============================================================

struct CORE_API ArchiveError {
    enum class Code : uint8_t {
        MissingKey,       // 必需的 key 不存在
        TypeMismatch,     // 期望 int32 但读到 string 等
        CorruptedData,    // 校验不匹配、长度溢出
        VersionTooNew,    // 数据版本高于当前程序支持
        OutOfMemory,      // 数组 size 异常大（防恶意文件）
        IoError,          // 底层 I/O 失败（磁盘、网络等）
        FormatError,      // 格式不合法（JSON 语法错误等）
    };

    Code        code    = Code::CorruptedData;
    std::string message;          // 人类可读描述
    std::string context;          // 当前读取路径，如 "Document.entities[3].geometry"

    static ArchiveError make(Code code, std::string_view msg) {
        return ArchiveError{code, std::string(msg), {}};
    }

    static ArchiveError missingKey(std::string_view key) {
        return ArchiveError{Code::MissingKey,
                            std::string("Missing required key: ") + std::string(key),
                            {}};
    }

    static ArchiveError typeMismatch(std::string_view expected, std::string_view actual) {
        return ArchiveError{Code::TypeMismatch,
                            std::string("Type mismatch: expected ") + std::string(expected) +
                            " but got " + std::string(actual),
                            {}};
    }

    static ArchiveError corrupted(std::string_view msg) {
        return ArchiveError{Code::CorruptedData, std::string(msg), {}};
    }

    static ArchiveError versionTooNew(uint32_t found, uint32_t supported) {
        return ArchiveError{Code::VersionTooNew,
                            "Data version " + std::to_string(found) +
                            " exceeds supported version " + std::to_string(supported),
                            {}};
    }

    static ArchiveError outOfMemory(size_t size) {
        return ArchiveError{Code::OutOfMemory,
                            "Suspicious array size: " + std::to_string(size),
                            {}};
    }
};

// 序列化 read 操作的返回类型
using ArchiveResult = tl::expected<void, ArchiveError>;

} // namespace mulan::Core
