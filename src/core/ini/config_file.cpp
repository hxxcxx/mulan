#include "config_file.h"

#include <toml++/toml.hpp>
#include <fstream>
#include <sstream>

namespace mulan::core {

// ============================================================
// 构造 / 析构 / 移动
// ============================================================

ConfigFile::ConfigFile() = default;

ConfigFile::~ConfigFile() {
    delete table_;
}

ConfigFile::ConfigFile(ConfigFile&& o) noexcept : path_(std::move(o.path_)), table_(o.table_), loaded_(o.loaded_) {
    o.table_ = nullptr;
    o.loaded_ = false;
}

ConfigFile& ConfigFile::operator=(ConfigFile&& o) noexcept {
    if (this != &o) {
        delete table_;
        path_ = std::move(o.path_);
        table_ = o.table_;
        loaded_ = o.loaded_;
        o.table_ = nullptr;
        o.loaded_ = false;
    }
    return *this;
}

// ============================================================
// 加载 / 保存
// ============================================================

bool ConfigFile::load(std::string_view path) {
    path_ = path;
    delete table_;

    try {
        auto result = toml::parse_file(std::string(path));
        table_ = new toml::table(std::move(result));
        loaded_ = true;
        return true;
    } catch (const toml::parse_error& err) {
        // 解析失败，保持空表
        table_ = new toml::table();
        loaded_ = false;
        return false;
    }
}

bool ConfigFile::parse(std::string_view content) {
    delete table_;

    try {
        auto result = toml::parse(content);
        table_ = new toml::table(std::move(result));
        loaded_ = true;
        return true;
    } catch (const toml::parse_error&) {
        table_ = new toml::table();
        loaded_ = false;
        return false;
    }
}

bool ConfigFile::save() const {
    return saveAs(path_);
}

bool ConfigFile::saveAs(std::string_view path) const {
    if (!table_)
        return false;

    std::ofstream file(std::string(path), std::ios::out | std::ios::trunc);
    if (!file.is_open())
        return false;

    file << *table_;
    return true;
}

// ============================================================
// 读取
// ============================================================

std::string ConfigFile::getString(std::string_view section, std::string_view key, std::string_view defaultValue) const {
    if (!table_)
        return std::string(defaultValue);

    // section 为空 → 查全局 key；非空 → 查 section.key
    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    if (auto val = table_->at_path(path).value<std::string>()) {
        return *val;
    }
    return std::string(defaultValue);
}

int ConfigFile::getInt(std::string_view section, std::string_view key, int defaultValue) const {
    if (!table_)
        return defaultValue;

    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    if (auto val = table_->at_path(path).value<int64_t>()) {
        return static_cast<int>(*val);
    }
    return defaultValue;
}

int64_t ConfigFile::getInt64(std::string_view section, std::string_view key, int64_t defaultValue) const {
    if (!table_)
        return defaultValue;

    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    if (auto val = table_->at_path(path).value<int64_t>()) {
        return *val;
    }
    return defaultValue;
}

double ConfigFile::getDouble(std::string_view section, std::string_view key, double defaultValue) const {
    if (!table_)
        return defaultValue;

    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    if (auto val = table_->at_path(path).value<double>()) {
        return *val;
    }
    return defaultValue;
}

bool ConfigFile::getBool(std::string_view section, std::string_view key, bool defaultValue) const {
    if (!table_)
        return defaultValue;

    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    if (auto val = table_->at_path(path).value<bool>()) {
        return *val;
    }
    return defaultValue;
}

bool ConfigFile::hasKey(std::string_view section, std::string_view key) const {
    if (!table_)
        return false;

    std::string path = section.empty() ? std::string(key) : std::string(section) + "." + std::string(key);

    return !!table_->at_path(path);
}

bool ConfigFile::hasSection(std::string_view section) const {
    if (!table_)
        return false;
    return table_->get(std::string(section)) != nullptr;
}

std::vector<std::string> ConfigFile::keys(std::string_view section) const {
    std::vector<std::string> result;
    if (!table_)
        return result;

    auto* tbl = table_->get_as<toml::table>(std::string(section));
    if (!tbl)
        return result;

    for (auto& [k, v] : *tbl) {
        result.emplace_back(k.str());
    }
    return result;
}

std::vector<std::string> ConfigFile::sections() const {
    std::vector<std::string> result;
    if (!table_)
        return result;

    for (auto& [k, v] : *table_) {
        if (v.is_table()) {
            result.emplace_back(k.str());
        }
    }
    return result;
}

// ============================================================
// 写入
// ============================================================

void ConfigFile::setString(std::string_view section, std::string_view key, std::string_view value) {
    if (!table_)
        table_ = new toml::table();

    if (section.empty()) {
        table_->emplace<std::string>(std::string(key), std::string(value));
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (!sub) {
            table_->emplace<toml::table>(std::string(section));
            sub = table_->get_as<toml::table>(std::string(section));
        }
        sub->emplace<std::string>(std::string(key), std::string(value));
    }
}

void ConfigFile::setInt(std::string_view section, std::string_view key, int value) {
    if (!table_)
        table_ = new toml::table();

    if (section.empty()) {
        table_->emplace<int64_t>(std::string(key), static_cast<int64_t>(value));
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (!sub) {
            table_->emplace<toml::table>(std::string(section));
            sub = table_->get_as<toml::table>(std::string(section));
        }
        sub->emplace<int64_t>(std::string(key), static_cast<int64_t>(value));
    }
}

void ConfigFile::setInt64(std::string_view section, std::string_view key, int64_t value) {
    if (!table_)
        table_ = new toml::table();

    if (section.empty()) {
        table_->emplace<int64_t>(std::string(key), value);
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (!sub) {
            table_->emplace<toml::table>(std::string(section));
            sub = table_->get_as<toml::table>(std::string(section));
        }
        sub->emplace<int64_t>(std::string(key), value);
    }
}

void ConfigFile::setDouble(std::string_view section, std::string_view key, double value) {
    if (!table_)
        table_ = new toml::table();

    if (section.empty()) {
        table_->emplace<double>(std::string(key), value);
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (!sub) {
            table_->emplace<toml::table>(std::string(section));
            sub = table_->get_as<toml::table>(std::string(section));
        }
        sub->emplace<double>(std::string(key), value);
    }
}

void ConfigFile::setBool(std::string_view section, std::string_view key, bool value) {
    if (!table_)
        table_ = new toml::table();

    if (section.empty()) {
        table_->emplace<bool>(std::string(key), value);
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (!sub) {
            table_->emplace<toml::table>(std::string(section));
            sub = table_->get_as<toml::table>(std::string(section));
        }
        sub->emplace<bool>(std::string(key), value);
    }
}

void ConfigFile::removeKey(std::string_view section, std::string_view key) {
    if (!table_)
        return;

    if (section.empty()) {
        table_->erase(std::string(key));
    } else {
        auto* sub = table_->get_as<toml::table>(std::string(section));
        if (sub)
            sub->erase(std::string(key));
    }
}

void ConfigFile::removeSection(std::string_view section) {
    if (!table_)
        return;
    table_->erase(std::string(section));
}

void ConfigFile::clear() {
    delete table_;
    table_ = nullptr;
    loaded_ = false;
}

}  // namespace mulan::core
