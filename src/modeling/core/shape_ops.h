/**
 * @file shape_ops.h
 * @brief IShapeOps —— 中立建模操作接口(拉伸/布尔/...)+ 注册表。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 与 IShapeFileReader 同构:后端(OCCT/truck)实现并注册到 ShapeOpsRegistry,经
 * mulan_load_backend 自注册。编辑层经注册表拿到 IShapeOps,调操作产出 Shape,
 * 不接触任何内核类型。
 *
 * 两个并列 registry:
 *   IShapeFileReader + ShapeFileReaderRegistry —— 从外部源造 Shape(读 STEP)
 *   IShapeOps       + ShapeOpsRegistry         —— 建模操作造 Shape(拉伸/布尔/...)
 *
 * 扩展方式:新增操作(如倒角/扫掠/抽壳)只需三步 ——
 *   1. 本头加参数结构 + IShapeOps 虚方法
 *   2. 后端 OccShapeOps 实现
 *   3. 编辑层加 Operation
 * 框架不动。
 */
#pragma once

#include "modeling_core_export.h"

#include <mulan/asset/face_asset.h>
#include <mulan/core/result/error.h>
#include <mulan/math/math.h>
#include <mulan/modeling/core/shape.h>

#include <memory>
#include <optional>

namespace mulan::modeling {

enum class BooleanOp : uint8_t {
    Union,
    Difference,
    Intersection,
};

/// 拉伸参数:平面轮廓 + 方向距离。
/// direction 为零向量时用 profile.frame.normal;inward 反向。
struct ExtrudeParams {
    asset::FaceDefinition profile;
    /// 可选的精确圆轮廓。提供时后端应构造解析圆面而非使用 profile 的离散多边形。
    std::optional<math::Circle3> circleProfile;
    math::Vec3 direction{ 0.0, 0.0, 0.0 };
    double distance = 0.0;
    bool inward = false;
};

/// 中立建模操作接口。后端实现并注册到 ShapeOpsRegistry。
/// 后端不能实现的操作返回 core::ErrorCode::NotSupported。
class IShapeOps {
public:
    virtual ~IShapeOps() = default;

    /// 拉伸平面轮廓为实体/片体。
    virtual core::Result<Shape> extrude(const ExtrudeParams& params) = 0;

    /// 两体布尔。target ⊕ tool。
    virtual core::Result<Shape> boolean(const Shape& target, const Shape& tool, BooleanOp op) = 0;

    // 后续操作(逐个加,框架不变):
    //   virtual core::Result<Shape> sweep(const SweepParams&) = 0;        // 扫掠
    //   virtual core::Result<Shape> revolve(const RevolveParams&) = 0;    // 旋转
    //   virtual core::Result<Shape> fillet(const FilletParams&) = 0;      // 圆角
    //   virtual core::Result<Shape> chamfer(const ChamferParams&) = 0;    // 倒角
    //   virtual core::Result<Shape> shell(const ShellParams&) = 0;        // 抽壳
};

/// 建模操作注册表(单后端:直接注册实例)。
class MODELING_CORE_API ShapeOpsRegistry {
public:
    static ShapeOpsRegistry& instance();

    void registerOps(std::unique_ptr<IShapeOps> ops);

    /// 返回已注册的 ops;未注册返回 nullptr。
    IShapeOps* ops() const;

    ShapeOpsRegistry(const ShapeOpsRegistry&) = delete;
    ShapeOpsRegistry& operator=(const ShapeOpsRegistry&) = delete;

private:
    ShapeOpsRegistry() = default;
    std::unique_ptr<IShapeOps> ops_;
};

}  // namespace mulan::modeling
