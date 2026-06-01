/**
 * @file IFileImporter.h
 * @brief 文件导入器接口 — 解析文件并填充 World
 * @author hxxcxx
 * @date 2026-04-22
 */
#pragma once

#include "DocumentExport.h"

#include <memory>
#include <string>
#include <vector>

namespace mulan::world {
class World;
}

namespace mulan::document {

/// 文件导入器接口 — 直接创建 world::Entity 填充 World
class DOCUMENT_API IFileImporter {
public:
    virtual ~IFileImporter() = default;

    /// 导入文件，直接创建 world::Entity 到 World 中
    /// @return true 成功，false 失败（调用 lastError() 获取原因）
    virtual bool import(const std::string& path, mulan::world::World& world) = 0;

    /// 支持的文件扩展名（小写，不含点）
    virtual std::vector<std::string> supportedExtensions() const = 0;

    /// 导入器名称
    virtual std::string name() const = 0;

    const std::string& lastError() const { return m_lastError; }

protected:
    std::string m_lastError;
};

} // namespace mulan::document
