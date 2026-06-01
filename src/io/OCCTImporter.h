/**
 * @file OCCTImporter.h
 * @brief OCCT 文件导入器 — STEP/IGES 解析为 world::Entity
 * @author hxxcxx
 * @date 2026-04-22
 *
 * 读取 STEP/IGES，解析为 TopoDS_Shape，
 * 创建 world::Entity 并设置 SolidGeometryData（B-Rep，不做三角化）。
 */
#pragma once

#include "IFileImporter.h"

namespace mulan::world {
class World;
}

namespace mulan::io {

class IO_API OCCTImporter : public IFileImporter {
public:
    bool import(const std::string& path, mulan::world::World& world) override;
    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

} // namespace mulan::io
