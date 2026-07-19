/**
 * @file importer_factory.h
 * @brief 文件导入器工厂 — 按扩展名创建导入器
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "io_export.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::io {

class IFileImporter;

class IO_API ImporterFactory {
public:
    using Creator = std::function<std::unique_ptr<IFileImporter>()>;

    static ImporterFactory& instance();

    void registerImporter(const std::string& extension, Creator creator);

    std::unique_ptr<IFileImporter> create(const std::string& extension) const;

    std::vector<std::string> allSupportedExtensions() const;

    ImporterFactory(const ImporterFactory&) = delete;
    ImporterFactory& operator=(const ImporterFactory&) = delete;

private:
    ImporterFactory() = default;
    std::unordered_map<std::string, Creator> creators_;
};

}  // namespace mulan::io
