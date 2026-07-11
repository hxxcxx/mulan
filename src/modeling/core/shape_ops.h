/**
 * @file shape_ops.h
 * @brief IShapeOps —— 中立建模操作接口(拉伸/布尔/...)+ 注册表。
 * @author hxxcxx
 * @date 2026-07-10
 *
 * 后端(OCCT/truck)以名称注册到 ShapeOpsRegistry,经 mulan_load_backend
 * 自注册。runtime 只选择 IShapeOps 后端；文件读写使用独立注册表，不受此处
 * 配置影响。编辑层经注册表拿到 IShapeOps,调操作产出 Shape,不接触任何内核类型。
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

#include <mulan/core/result/error.h>
#include <mulan/math/math.h>
#include <mulan/modeling/core/shape.h>

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mulan::modeling {

enum class BooleanOp : uint8_t {
    Union,
    Difference,
    Intersection,
};

/// 轮廓所在的平面坐标系。origin 为原点，x/y/normal 构成正交基。
struct ProfileFrame {
    math::Point3 origin = math::Point3::origin();
    math::Vec3 x = math::Vec3::unitX();
    math::Vec3 y = math::Vec3::unitY();
    math::Vec3 normal = math::Vec3::unitZ();

    math::Plane3 plane() const { return math::Plane3::fromPointNormal(origin, normal); }
};

/// 闭合环：世界坐标点序列。用于外环或孔环。
struct ProfileLoop {
    std::vector<math::Point3> points;

    bool empty() const { return points.empty(); }
    size_t size() const { return points.size(); }
};

/// 中立平面轮廓：坐标系 + 外环 + 孔环。由调用方从 asset::FaceDefinition 转换得到，
/// 仅由纯 math 类型组成，建模层不依赖 asset。
struct PlanarProfile {
    ProfileFrame frame;
    ProfileLoop outer;
    std::vector<ProfileLoop> holes;

    bool hasOuterLoop() const { return outer.points.size() >= 3; }
};

/// 拉伸参数:平面轮廓 + 方向距离。
/// direction 为零向量时用 profile.frame.normal;inward 反向。
struct ExtrudeParams {
    PlanarProfile profile;
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

/// 建模操作注册表。多个后端按名称共存，当前选择与插件加载顺序无关。
class MODELING_CORE_API ShapeOpsRegistry {
public:
    static ShapeOpsRegistry& instance();

    /// 注册或替换一个命名后端。名称不区分大小写。
    void registerOps(std::string backend, std::unique_ptr<IShapeOps> ops);

    /// 选择建模操作后端。允许先选择、后加载插件；未加载时 ops() 返回 nullptr。
    void selectBackend(std::string backend);

    /// 当前选择的后端名称。
    std::string selectedBackend() const;

    /// 已注册的后端名称。
    std::vector<std::string> availableBackends() const;

    /// 返回当前选择后端的 ops;该后端未注册时返回 nullptr，不隐式回退。
    IShapeOps* ops() const;

    ShapeOpsRegistry(const ShapeOpsRegistry&) = delete;
    ShapeOpsRegistry& operator=(const ShapeOpsRegistry&) = delete;

private:
    ShapeOpsRegistry() = default;
    std::unordered_map<std::string, std::unique_ptr<IShapeOps>> backends_;
    std::string selectedBackend_ = "occt";
};

}  // namespace mulan::modeling
