/**
 * @file step_reader.h
 * @brief OCCT 后端的 STEP/IGES 读取器：实现中立 IShapeFileReader。
 * @author hxxcxx
 * @date 2026-07-09
 *
 * 公共头零 OCCT 类型。OCCT 的 STEPControl_Reader/IGESControl_Reader 只在
 * step_reader.cpp 出现。OccStepReader 实现 modeling_core 的中立 IShapeFileReader，
 * 产出 NamedShape（不接触 Document）。
 *
 * registerOccStepReader() 由 app 启动时调用，把本 reader 注册进中立注册表。
 */
#pragma once

#include "modeling_occt_export.h"

#include <mulan/modeling/core/shape_file_reader.h>

namespace mulan::modeling {

/// OCCT 后端的 STEP/IGES 读取器。
class MODELING_OCCT_API OccStepReader final : public IShapeFileReader {
public:
    core::Result<std::vector<NamedShape>> read(const std::string& path) override;
    std::vector<std::string> supportedExtensions() const override;
    std::string name() const override;
};

/// 把 OccStepReader 注册进中立 ShapeFileReaderRegistry。app 启动时调用一次。
MODELING_OCCT_API void registerOccStepReader();

}  // namespace mulan::modeling
