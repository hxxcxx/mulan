/**
 * @file config_file.h
 * @brief TOML 配置文件读写 — 基于 toml++ (header-only, C++20)
 * @author hxxcxx
 * @date 2026-05-26
 *
 * 对 toml++ 的薄封装，提供 Core 层的配置读写能力。
 * TOML 是 INI 的现代超集，支持：
 *  - [section] / [section.subsection] 分组
 *  - string, int, float, bool 基本类型
 *  - 数组、表（嵌套）
 *  - 日期时间等高级类型
 *
 * 用法：
 *   ConfigFile cfg;
 *   cfg.load("settings.toml");
 *   int port = cfg.getInt("server", "port", 8080);
 *   cfg.setInt("server", "port", 9090);
 *   cfg.save();
 */

#pragma once

#include "../core_export.h"

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>

#include <toml++/toml.hpp>

namespace mulan::core {

/// TOML 配置文件读写
class CORE_API ConfigFile {
public:
    ConfigFile();
    ~ConfigFile();

    ConfigFile(const ConfigFile&) = delete;
    ConfigFile& operator=(const ConfigFile&) = delete;
    ConfigFile(ConfigFile&&) noexcept;
    ConfigFile& operator=(ConfigFile&&) noexcept;

    /// 从文件加载，返回 true 表示成功
    bool load(std::string_view path);

    /// 从字符串解析（用于测试或内存中的配置）
    bool parse(std::string_view content);

    /// 保存到文件（原路径）
    bool save() const;

    /// 保存到指定路径
    bool saveAs(std::string_view path) const;

    /// 是否已加载
    bool isLoaded() const { return loaded_; }

    // ==================== 读取 ====================

    std::string getString(std::string_view section, std::string_view key, std::string_view defaultValue = {}) const;

    int getInt(std::string_view section, std::string_view key, int defaultValue = 0) const;

    int64_t getInt64(std::string_view section, std::string_view key, int64_t defaultValue = 0) const;

    double getDouble(std::string_view section, std::string_view key, double defaultValue = 0.0) const;

    bool getBool(std::string_view section, std::string_view key, bool defaultValue = false) const;

    /// 键是否存在
    bool hasKey(std::string_view section, std::string_view key) const;

    /// section 是否存在
    bool hasSection(std::string_view section) const;

    /// 获取 section 下所有 key
    std::vector<std::string> keys(std::string_view section) const;

    /// 获取所有 section 名
    std::vector<std::string> sections() const;

    // ==================== 写入 ====================

    void setString(std::string_view section, std::string_view key, std::string_view value);

    void setInt(std::string_view section, std::string_view key, int value);

    void setInt64(std::string_view section, std::string_view key, int64_t value);

    void setDouble(std::string_view section, std::string_view key, double value);

    void setBool(std::string_view section, std::string_view key, bool value);

    /// 删除键
    void removeKey(std::string_view section, std::string_view key);

    /// 删除整个 section
    void removeSection(std::string_view section);

    /// 清空所有
    void clear();

private:
    std::string path_;
    toml::table* table_ = nullptr;
    bool loaded_ = false;
};

}  // namespace mulan::core
