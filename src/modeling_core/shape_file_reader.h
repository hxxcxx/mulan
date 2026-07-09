/**
 * @file shape_file_reader.h
 * @brief 中立形状文件读取体系：B-Rep 格式(STEP/IGES/...)的读取分发。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * STEP/IGES 本身就是 B-Rep 结构，读取是建模内核的能力（OCCT/truck 都能读），
 * 因此读取分发归 modeling_core，与 ShapeStorage::tessellate 同构：
 *
 *   - IShapeFileReader 是中立虚接口，产出 NamedShape（不接触 io::Document）
 *   - 后端（modeling_occt 等）实现该接口，在 app 启动时注册到 ShapeFileReaderRegistry
 *   - io 的导入调度经 ShapeFileReaderRegistry 查找 reader，拿到 NamedShape 后
 *     灌进 Document::addBody —— io 只依赖 modeling_core，任何后端不可见
 *
 * reader 刻意不接触 Document：把"读 B-Rep"和"装进文档"解耦，前者是内核职责，
 * 后者是 io 职责。
 */
#pragma once

#include "modeling_core_export.h"

#include <mulan/core/result/error.h>
#include <mulan/modeling_core/shape.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::modeling {

/// 命名形状：一个 B-Rep Shape + 显示名（来自文件的实体级粒度）。
struct NamedShape {
    std::string name;
    Shape shape;
};

/// 中立形状文件读取接口：把 B-Rep 格式文件解析为命名形状列表。
/// 不接触 io::Document；Document 装载是 io 的职责。
class IShapeFileReader {
public:
    virtual ~IShapeFileReader() = default;

    virtual core::Result<std::vector<NamedShape>> read(const std::string& path) = 0;
    virtual std::vector<std::string> supportedExtensions() const = 0;
    virtual std::string name() const = 0;
};

/// 形状文件读取器注册表：扩展名 -> reader 工厂。
/// io 经此查找 reader，从而不依赖任何具体后端。后端在 app 启动时注册自己。
class MODELING_CORE_API ShapeFileReaderRegistry {
public:
    using Creator = std::function<std::unique_ptr<IShapeFileReader>()>;

    static ShapeFileReaderRegistry& instance();

    /// 注册一个 reader 工厂，绑定到该 reader 声明的所有扩展名（小写，不含点）。
    void registerReader(Creator creator);

    /// 按扩展名（小写，不含点）创建 reader；未注册返回 nullptr。
    std::unique_ptr<IShapeFileReader> create(const std::string& extension) const;

    /// 所有已注册扩展名（用于全局过滤提示）。
    std::vector<std::string> allSupportedExtensions() const;

    ShapeFileReaderRegistry(const ShapeFileReaderRegistry&) = delete;
    ShapeFileReaderRegistry& operator=(const ShapeFileReaderRegistry&) = delete;

private:
    ShapeFileReaderRegistry() = default;

    struct Entry {
        Creator creator;
        std::vector<std::string> extensions;
    };
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> byExtension_;
};

}  // namespace mulan::modeling
