/**
 * @file ImporterFactory.h
 * @brief 文件导入器工厂 — 按扩展名创建导入器
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "DocumentExport.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace MulanGeo::document {

class IFileImporter;

class DOCUMENT_API ImporterFactory {
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
    std::unordered_map<std::string, Creator> m_creators;
};

/// 自动注册辅助类 — 放在 .cpp 文件全局作用域即可完成注册
class DOCUMENT_API AutoRegisterImporter {
public:
    AutoRegisterImporter(const std::string& extension, ImporterFactory::Creator creator);
};

} // namespace MulanGeo::Document
