/**
 * @file geometry_asset.h
 * @brief GeometryAsset —— 场景实体引用的几何资产基类
 * @author hxxcxx
 * @date 2026-07-02
 *
 * collectDrawables 是统一展开接口：每种子类（Tessellated / Mesh / 未来的自研格式）
 * 自行决定如何把自身拆成可绘制网格段，调用方（DrawCommandBuilder）无需知道
 * 具体子类结构，从而消除对 TessellatedAsset/MeshAsset 的 dynamic_cast 分支。
 */

#pragma once

#include "asset.h"
#include "asset_id.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace mulan::engine {
class Mesh;
}

namespace mulan::asset {

/// 可绘制段的渲染角色：实体填充（三角面片）或线框（线段）。
/// 这是渲染通道的分类，不是 CAD 拓扑术语——Solid 指 solid shader 画三角，
/// Wire 指 edge shader 画线。
enum class DrawableRole : uint8_t {
    Solid,
    Wire,
};

/// 一段可绘制网格：网格本体（资产持有，不拥有）+ 材质 + 渲染角色。
/// material 为 invalid 表示该段无专属材质（如 TessellatedAsset 的线框），由调用方回退默认。
struct Drawable {
    const engine::Mesh* mesh = nullptr;
    AssetId             material = AssetId::invalid();
    DrawableRole        role = DrawableRole::Solid;
};

class GeometryAsset : public Asset {
public:
    GeometryAsset(AssetId id, AssetKind kind, std::string name)
        : Asset(id, kind, std::move(name)) {}

    /// 把自身展开为可绘制网格段，追加到 out。
    /// 子类按自身结构实现：TessellatedAsset 产出 face + edge，Mesh 产出每个 primitive。
    virtual void collectDrawables(std::vector<Drawable>& out) const = 0;
};

} // namespace mulan::asset
