/**
 * @file WorldTest.cpp
 * @brief World + Entity + System 全链路内存测试
 * @author hxxcxx
 * @date 2026-05-29
 */

#include "World.h"
#include "system/TransformSystem.h"
#include "system/BoundsSystem.h"
#include "geometry/BoxGeometryData.h"

#include <cassert>
#include <cstdio>
#include <cmath>

using namespace mulan;
using namespace mulan::world;
using namespace mulan::engine;

static int g_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::fprintf(stderr, "  FAIL: %s\n", msg); ++g_failed; } \
    else { std::printf("  OK:   %s\n", msg); } \
} while(0)

#define CHECK_CLOSE(a, b, eps, msg) do { \
    if (std::abs((a) - (b)) > (eps)) { \
        std::fprintf(stderr, "  FAIL: %s (%.3f != %.3f)\n", msg, (double)(a), (double)(b)); \
        ++g_failed; \
    } else { std::printf("  OK:   %s\n", msg); } \
} while(0)

// ─── 1. Entity 创建 + 脏标记 ──────────────────────────────────

static void test_entityCreation() {
    std::printf("\n--- test_entityCreation ---\n");
    World w;
    auto* e = w.createEntity("Box");
    CHECK(e != nullptr, "entity created");
    CHECK(e->id() != Entity::INVALID_ID, "id != INVALID");
    CHECK(e->name() == "Box", "name == Box");
    CHECK(e->visible() == true, "visible default true");
    CHECK(e->isDirty(EntityDirty::Created), "has Created dirty");
    CHECK(w.entityCount() == 1, "entityCount == 1");
}

// ─── 2. Id generation 编码 ─────────────────────────────────────

static void test_idGeneration() {
    std::printf("\n--- test_idGeneration ---\n");
    World w;
    auto* e1 = w.createEntity("a");
    auto* e2 = w.createEntity("b");
    CHECK(e1->index() == 0 && e2->index() == 1, "sequential indices");

    auto e1Id  = e1->id();
    auto e1Gen = e1->generation();
    w.destroyEntity(e1Id);
    auto* e3 = w.createEntity("c");
    CHECK(e3->index() == 0, "slot reuse same index");
    CHECK(e3->generation() > e1Gen, "generation incremented on reuse");
    CHECK(e3->id() != e1Id, "different Id after reuse");
    CHECK(w.isValid(e1Id) == false, "old handle no longer valid");
    CHECK(w.isValid(e3->id()) == true, "new handle is valid");
}

// ─── 3. GeometryData 设置 + 网格缓存 ──────────────────────────

static void test_geometryData() {
    std::printf("\n--- test_geometryData ---\n");
    World w;
    auto* e = w.createEntity("Box");

    auto geo = std::make_unique<BoxGeometryData>(2.0, 3.0, 4.0);
    e->setGeometry(std::move(geo));

    CHECK(e->hasGeometry(), "hasGeometry true");
    CHECK(e->geometry()->type() == GeometryData::Type::Box, "type == Box");

    const auto& mesh = e->cachedFaceMesh();
    CHECK(mesh.vertexCount() > 0, "faceMesh has vertices");
    CHECK(mesh.indexCount() > 0, "faceMesh has indices");

    // bounds 应该是局部空间 [-w/2,w/2] 等
    auto b = e->geometry()->bounds();
    CHECK_CLOSE(b.min.x, -1.0, 0.01, "bounds min.x == -1.0");
    CHECK_CLOSE(b.min.y, -1.5, 0.01, "bounds min.y == -1.5");
    CHECK_CLOSE(b.max.x,  1.0, 0.01, "bounds max.x == 1.0");
    CHECK_CLOSE(b.max.y,  1.5, 0.01, "bounds max.y == 1.5");
}

// ─── 4. TransformSystem 世界变换 ───────────────────────────────

static void test_transformSystem() {
    std::printf("\n--- test_transformSystem ---\n");
    World w;
    w.addSystem(std::make_unique<TransformSystem>());

    auto* parent = w.createEntity("parent");
    auto* child  = w.createEntity("child");

    // parent 位移到 (10, 0, 0)
    parent->setLocalTransform(glm::translate(Mat4(1.0), Vec3(10, 0, 0)));
    // child 局部在 (1, 2, 0)
    child->setLocalTransform(glm::translate(Mat4(1.0), Vec3(1, 2, 0)));

    // 建立父子关系
    w.setParent(child->id(), parent->id());

    w.updateLogic(0.016f);

    // parent 世界 = 局部 × identity = (10, 0, 0)
    auto pw = parent->worldTransform();
    CHECK_CLOSE(pw[3][0], 10.0, 0.01, "parent world x == 10");
    CHECK_CLOSE(pw[3][1],  0.0, 0.01, "parent world y == 0");

    // child 世界 = parent.world * child.local = (11, 2, 0)
    auto cw = child->worldTransform();
    CHECK_CLOSE(cw[3][0], 11.0, 0.01, "child world x == 11");
    CHECK_CLOSE(cw[3][1],  2.0, 0.01, "child world y == 2");
}

// ─── 5. BoundsSystem ──────────────────────────────────────────

static void test_boundsSystem() {
    std::printf("\n--- test_boundsSystem ---\n");
    World w;
    w.addSystem(std::make_unique<TransformSystem>());
    w.addSystem(std::make_unique<BoundsSystem>());

    auto* e = w.createEntity("Box");
    e->setGeometry(std::make_unique<BoxGeometryData>(2.0, 2.0, 2.0));
    e->setLocalTransform(glm::translate(Mat4(1.0), Vec3(5, 0, 0)));

    w.updateLogic(0.016f);

    // world bounds = local bounds [-1,1]³ transformed by (5,0,0) → [4,6]×[-1,1]×[-1,1]
    auto& b = e->cachedBounds();
    CHECK_CLOSE(b.min.x, 4.0, 0.01, "world bounds min.x == 4");
    CHECK_CLOSE(b.max.x, 6.0, 0.01, "world bounds max.x == 6");
}

// ─── 6. destroy 自动提升 children ─────────────────────────────

static void test_destroyPromoteChildren() {
    std::printf("\n--- test_destroyPromoteChildren ---\n");
    World w;
    w.addSystem(std::make_unique<TransformSystem>());

    auto* root   = w.createEntity("root");
    auto* mid    = w.createEntity("mid");
    auto* leaf   = w.createEntity("leaf");

    w.setParent(mid->id(),  root->id());
    w.setParent(leaf->id(), mid->id());

    root->setLocalTransform(glm::translate(Mat4(1.0), Vec3(10, 0, 0)));
    mid->setLocalTransform (glm::translate(Mat4(1.0), Vec3(1, 0, 0)));
    leaf->setLocalTransform(glm::translate(Mat4(1.0), Vec3(2, 0, 0)));

    w.updateLogic(0.016f);
    CHECK_CLOSE(leaf->worldTransform()[3][0], 13.0, 0.01, "leaf world x == 13 before destroy");

    // 删除 mid → leaf 提升为根节点
    w.destroyEntity(mid->id());
    CHECK(leaf->parentId() == Entity::INVALID_ID, "leaf parentId == INVALID after destroy");

    w.updateLogic(0.016f);
    CHECK_CLOSE(leaf->worldTransform()[3][0], 2.0, 0.01, "leaf world x == 2 after promotion (no parent)");
}

// ─── 7. 循环检测 ──────────────────────────────────────────────

static void test_cycleDetection() {
    std::printf("\n--- test_cycleDetection ---\n");
    World w;

    auto* a = w.createEntity("A");
    auto* b = w.createEntity("B");

    CHECK(w.setParent(b->id(), a->id()) == true, "A → B ok");
    CHECK(w.setParent(a->id(), b->id()) == false, "B → A blocked (cycle)");

    auto* c = w.createEntity("C");
    CHECK(w.setParent(c->id(), b->id()) == true, "B → C ok");
    CHECK(w.setParent(a->id(), c->id()) == false, "C → A blocked (cycle 3)");
}

// ─── main ──────────────────────────────────────────────────────

int main() {
    test_entityCreation();
    test_idGeneration();
    test_geometryData();
    test_transformSystem();
    test_boundsSystem();
    test_destroyPromoteChildren();
    test_cycleDetection();

    std::printf("\n=== %d failed ===\n", g_failed);
    return g_failed > 0 ? 1 : 0;
}
