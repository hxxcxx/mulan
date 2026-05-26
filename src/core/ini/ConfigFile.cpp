/**
 * @file ConfigFile.cpp
 * @brief TOML 配置文件读写实现 — 基于 toml++
 * @author hxxcxx
 * @date 2026-05-26
 */

#include "ConfigFile.h"

#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>
#include <cstring>

namespace mulan::core {

// ============================================================
// 构造 / 析构 / 移动
// ============================================================

ConfigFile::ConfigFile() = default;

ConfigFile::~ConfigFile() {
    delete m_table;
}

ConfigFile::ConfigFile(ConfigFile&& o) noexcept
    : m_path(std::move(o.m_path))
    , m_table(o.m_table)
    , m_loaded(o.m_loaded) {
    o.m_table = nullptr;
    o.m_loaded = false;
}

ConfigFile& ConfigFile::operator=(ConfigFile&& o) noexcept {
    if (this != &o) {
        delete m_table;
        m_path = std::move(o.m_path);
        m_table = o.m_table;
        m_loaded = o.m_loaded;
        o.m_table = nullptr;
        o.m_loaded = false;
    }
    return *this;
}

// ============================================================
// 加载 / 保存
// ============================================================

bool ConfigFile::load(std::string_view path) {
    m_path = path;
    delete m_table;

    try {
        auto result = toml::parse_file(std::string(path));
        m_table = new toml::table(std::move(result));
        m_loaded = true;
        return true;
    } catch (const toml::parse_error& err) {
        // 解析失败，保持空表
        m_table = new toml::table();
        m_loaded = false;
        return false;
    }
}

bool ConfigFile::parse(std::string_view content) {
    delete m_table;

    try {
        auto result = toml::parse(content);
        m_table = new toml::table(std::move(result));
        m_loaded = true;
        return true;
    } catch (const toml::parse_error&) {
        m_table = new toml::table();
        m_loaded = false;
        return false;
    }
}

bool ConfigFile::save() const {
    return saveAs(m_path);
}

bool ConfigFile::saveAs(std::string_view path) const {
    if (!m_table) return false;

    std::ofstream file(std::string(path), std::ios::out | std::ios::trunc);
    if (!file.is_open()) return false;

    file << *m_table;
    return true;
}

// ============================================================
// 读取
// ============================================================

std::string ConfigFile::getString(std::string_view section, std::string_view key,
                                   std::string_view defaultValue) const {
    if (!m_table) return std::string(defaultValue);

    // 支持嵌套路径: "server" + "port" → server.port
    auto node = m_table->get_qualified(std::string(section) + "." + std::string(key));
    if (!node) {
        // 也尝试直接键（无 section）
        node = m_table->get(std::string(key));
    }

    if (auto* val = node->as_string()) {
        return std::string(val->get());
    }
    return std::string(defaultValue);
}

int ConfigFile::getInt(std::string_view section, std::string_view key,
                       int defaultValue) const {
    if (!m_table) return defaultValue;

    auto dotted = std::string(section) + "." + std::string(key);
    if (auto val = m_table->get_qualified_as<int64_t>(dotted)) {
        return static_cast<int>(*val);
    }
    // 回退：直接键
    if (auto val = m_table->get_as<int64_t>(std::string(key))) {
        return static_cast<int>(*val);
    }
    return defaultValue;
}

int64_t ConfigFile::getInt64(std::string_view section, std::string_view key,
                              int64_t defaultValue) const {
    if (!m_table) return defaultValue;

    auto dotted = std::string(section) + "." + std::string(key);
    if (auto val = m_table->get_qualified_as<int64_t>(dotted)) {
        return *val;
    }
    if (auto val = m_table->get_as<int64_t>(std::string(key))) {
        return *val;
    }
    return defaultValue;
}

double ConfigFile::getDouble(std::string_view section, std::string_view key,
                              double defaultValue) const {
    if (!m_table) return defaultValue;

    auto dotted = std::string(section) + "." + std::string(key);
    if (auto val = m_table->get_qualified_as<double>(dotted)) {
        return *val;
    }
    if (auto val = m_table->get_as<double>(std::string(key))) {
        return *val;
    }
    return defaultValue;
}

bool ConfigFile::getBool(std::string_view section, std::string_view key,
                          bool defaultValue) const {
    if (!m_table) return defaultValue;

    auto dotted = std::string(section) + "." + std::string(key);
    if (auto val = m_table->get_qualified_as<bool>(dotted)) {
        return *val;
    }
    if (auto val = m_table->get_as<bool>(std::string(key))) {
        return *val;
    }
    return defaultValue;
}

bool ConfigFile::hasKey(std::string_view section, std::string_view key) const {
    if (!m_table) return false;

    auto dotted = std::string(section) + "." + std::string(key);
    return m_table->get_qualified(dotted) != nullptr
        || m_table->get(std::string(key)) != nullptr;
}

bool ConfigFile::hasSection(std::string_view section) const {
    if (!m_table) return false;
    return m_table->get(std::string(section)) != nullptr;
}

std::vector<std::string> ConfigFile::keys(std::string_view section) const {
    std::vector<std::string> result;
    if (!m_table) return result;

    auto* tbl = m_table->get_as<toml::table>(std::string(section));
    if (!tbl) return result;

    for (auto& [k, v] : *tbl) {
        result.emplace_back(k.str());
    }
    return result;
}

std::vector<std::string> ConfigFile::sections() const {
    std::vector<std::string> result;
    if (!m_table) return result;

    for (auto& [k, v] : *m_table) {
        if (v.is_table()) {
            result.emplace_back(k.str());
        }
    }
    return result;
}

// ============================================================
// 写入
// ============================================================

void ConfigFile::setString(std::string_view section, std::string_view key,
                            std::string_view value) {
    if (!m_table) m_table = new toml::table();

    if (section.empty()) {
        m_table->emplace<std::string>(std::string(key), std::string(value));
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (!sub) {
            m_table->emplace<toml::table>(std::string(section));
            sub = m_table->get_as<toml::table>(std::string(section));
        }
        sub->emplace<std::string>(std::string(key), std::string(value));
    }
}

void ConfigFile::setInt(std::string_view section, std::string_view key,
                         int value) {
    if (!m_table) m_table = new toml::table();

    if (section.empty()) {
        m_table->emplace<int64_t>(std::string(key), static_cast<int64_t>(value));
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (!sub) {
            m_table->emplace<toml::table>(std::string(section));
            sub = m_table->get_as<toml::table>(std::string(section));
        }
        sub->emplace<int64_t>(std::string(key), static_cast<int64_t>(value));
    }
}

void ConfigFile::setInt64(std::string_view section, std::string_view key,
                           int64_t value) {
    if (!m_table) m_table = new toml::table();

    if (section.empty()) {
        m_table->emplace<int64_t>(std::string(key), value);
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (!sub) {
            m_table->emplace<toml::table>(std::string(section));
            sub = m_table->get_as<toml::table>(std::string(section));
        }
        sub->emplace<int64_t>(std::string(key), value);
    }
}

void ConfigFile::setDouble(std::string_view section, std::string_view key,
                            double value) {
    if (!m_table) m_table = new toml::table();

    if (section.empty()) {
        m_table->emplace<double>(std::string(key), value);
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (!sub) {
            m_table->emplace<toml::table>(std::string(section));
            sub = m_table->get_as<toml::table>(std::string(section));
        }
        sub->emplace<double>(std::string(key), value);
    }
}

void ConfigFile::setBool(std::string_view section, std::string_view key,
                          bool value) {
    if (!m_table) m_table = new toml::table();

    if (section.empty()) {
        m_table->emplace<bool>(std::string(key), value);
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (!sub) {
            m_table->emplace<toml::table>(std::string(section));
            sub = m_table->get_as<toml::table>(std::string(section));
        }
        sub->emplace<bool>(std::string(key), value);
    }
}

void ConfigFile::removeKey(std::string_view section, std::string_view key) {
    if (!m_table) return;

    if (section.empty()) {
        m_table->erase(std::string(key));
    } else {
        auto* sub = m_table->get_as<toml::table>(std::string(section));
        if (sub) sub->erase(std::string(key));
    }
}

void ConfigFile::removeSection(std::string_view section) {
    if (!m_table) return;
    m_table->erase(std::string(section));
}

void ConfigFile::clear() {
    delete m_table;
    m_table = nullptr;
    m_loaded = false;
}

} // namespace mulan::core
