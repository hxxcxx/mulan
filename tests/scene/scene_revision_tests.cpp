/**
 * @file scene_revision_tests.cpp
 * @brief 场景变更日志、多消费者游标与实体 generation 契约测试
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <gtest/gtest.h>

#include <mulan/math/math.h>
#include <mulan/scene/scene.h>
#include <mulan/scene/scene_change.h>

namespace mulan::scene {
namespace {

template <typename T>
concept HasPublicMarkDirty = requires(T& value, EntityId entity) { value.markDirty(entity, EntityDirty::Created); };

static_assert(!HasPublicMarkDirty<Scene>);

TEST(SceneChangeJournal, IndependentCursorsDoNotConsumeEachOther) {
    Scene scene(8);
    SceneChangeCursor renderCursor;
    SceneChangeCursor inspectorCursor;

    const EntityId entity = scene.createEntity("Initial");
    const SceneChangeSet firstRenderRead = scene.readChanges(renderCursor);
    ASSERT_EQ(firstRenderRead.status, SceneChangeStatus::Changes);
    ASSERT_EQ(firstRenderRead.changes.size(), 1u);
    EXPECT_EQ(firstRenderRead.changes.front().entity, entity);
    EXPECT_EQ(firstRenderRead.changes.front().dirty, EntityDirty::Created);
    renderCursor = firstRenderRead.cursorAfterApply();

    ASSERT_TRUE(scene.setName(entity, "Renamed"));

    const SceneChangeSet secondRenderRead = scene.readChanges(renderCursor);
    ASSERT_EQ(secondRenderRead.status, SceneChangeStatus::Changes);
    ASSERT_EQ(secondRenderRead.changes.size(), 1u);
    EXPECT_EQ(secondRenderRead.changes.front().dirty, EntityDirty::Name);
    renderCursor = secondRenderRead.cursorAfterApply();

    const SceneChangeSet inspectorRead = scene.readChanges(inspectorCursor);
    ASSERT_EQ(inspectorRead.status, SceneChangeStatus::Changes);
    ASSERT_EQ(inspectorRead.changes.size(), 2u);
    EXPECT_EQ(inspectorRead.changes[0].dirty, EntityDirty::Created);
    EXPECT_EQ(inspectorRead.changes[1].dirty, EntityDirty::Name);
    EXPECT_EQ(inspectorCursor.revision, 0u);
    inspectorCursor = inspectorRead.cursorAfterApply();
    EXPECT_EQ(renderCursor.revision, inspectorCursor.revision);
}

TEST(SceneChangeJournal, OverflowRequiresFullResyncBeforeIncrementalReadsResume) {
    Scene scene(2);
    SceneChangeCursor cursor;

    const EntityId entity = scene.createEntity("Initial");
    ASSERT_TRUE(scene.setName(entity, "Second"));
    ASSERT_TRUE(scene.setVisible(entity, false));

    const SceneChangeSet overflow = scene.readChanges(cursor);
    EXPECT_EQ(overflow.status, SceneChangeStatus::FullResyncRequired);
    EXPECT_TRUE(overflow.requiresFullResync());
    EXPECT_TRUE(overflow.changes.empty());
    EXPECT_EQ(cursor.revision, 0u);
    cursor = overflow.cursorAfterApply();  // 模拟消费者成功完成全量重建后确认。

    ASSERT_TRUE(scene.setSelected(entity, true));
    const SceneChangeSet incremental = scene.readChanges(cursor);
    ASSERT_EQ(incremental.status, SceneChangeStatus::Changes);
    ASSERT_EQ(incremental.changes.size(), 1u);
    EXPECT_EQ(incremental.changes.front().dirty, EntityDirty::Selection);
}

TEST(SceneChangeJournal, NoOpSettersDoNotAdvanceRevision) {
    Scene scene;
    const EntityId entity = scene.createEntity("Stable");
    const SceneRevision initialRevision = scene.revision();

    EXPECT_TRUE(scene.setName(entity, "Stable"));
    EXPECT_TRUE(scene.setLocalTransform(entity, math::Mat4::identity()));
    EXPECT_TRUE(scene.setWorldTransform(entity, math::Mat4::identity()));
    EXPECT_TRUE(scene.setParent(entity, EntityId::invalid()));
    EXPECT_TRUE(scene.setVisible(entity, true));
    EXPECT_TRUE(scene.setMaterialSlots(entity, {}));
    EXPECT_TRUE(scene.setSelected(entity, false));
    EXPECT_EQ(scene.revision(), initialRevision);

    const LightComponent light;
    EXPECT_TRUE(scene.setLight(entity, light));
    const SceneRevision lightRevision = scene.revision();
    EXPECT_TRUE(scene.setLight(entity, light));
    EXPECT_EQ(scene.revision(), lightRevision);
}

TEST(SceneChangeJournal, DestroyAndReusePreservePublishedGeneration) {
    Scene scene(8);
    SceneChangeCursor cursor;

    const EntityId oldEntity = scene.createEntity("Old");
    scene.destroyEntity(oldEntity);
    const EntityId newEntity = scene.createEntity("New");

    EXPECT_EQ(oldEntity.index(), newEntity.index());
    EXPECT_NE(oldEntity.generation(), newEntity.generation());
    EXPECT_FALSE(scene.isValid(oldEntity));
    EXPECT_TRUE(scene.isValid(newEntity));

    const SceneChangeSet changes = scene.readChanges(cursor);
    ASSERT_EQ(changes.status, SceneChangeStatus::Changes);
    ASSERT_EQ(changes.changes.size(), 3u);
    EXPECT_EQ(changes.changes[0].entity, oldEntity);
    EXPECT_EQ(changes.changes[0].dirty, EntityDirty::Created);
    EXPECT_EQ(changes.changes[1].entity, oldEntity);
    EXPECT_EQ(changes.changes[1].dirty, EntityDirty::Destroyed);
    EXPECT_EQ(changes.changes[2].entity, newEntity);
    EXPECT_EQ(changes.changes[2].dirty, EntityDirty::Created);
}

TEST(SceneHierarchyRevision, DestroyingParentReparentsAndPublishesChangedChildWorld) {
    Scene scene;
    const EntityId parent = scene.createEntity("Parent");
    const EntityId child = scene.createEntity("Child");
    ASSERT_TRUE(scene.setLocalTransform(parent, math::Mat4::translate(math::Vec3{ 10.0, 0.0, 0.0 })));
    ASSERT_TRUE(scene.setLocalTransform(child, math::Mat4::translate(math::Vec3{ 2.0, 0.0, 0.0 })));
    ASSERT_TRUE(scene.setParent(child, parent));

    const math::Point3 attachedOrigin = math::Point3::origin().transformedBy(scene.transform(child)->world);
    EXPECT_DOUBLE_EQ(attachedOrigin.x, 12.0);

    SceneChangeCursor cursor;
    cursor = scene.readChanges(cursor).cursorAfterApply();
    scene.destroyEntity(parent);

    ASSERT_TRUE(scene.isValid(child));
    ASSERT_NE(scene.hierarchy(child), nullptr);
    EXPECT_FALSE(scene.hierarchy(child)->parent);
    const math::Point3 detachedOrigin = math::Point3::origin().transformedBy(scene.transform(child)->world);
    EXPECT_DOUBLE_EQ(detachedOrigin.x, 2.0);

    const SceneChangeSet changes = scene.readChanges(cursor);
    ASSERT_EQ(changes.status, SceneChangeStatus::Changes);
    uint64_t childFlags = 0;
    uint64_t parentFlags = 0;
    for (const SceneChange& change : changes.changes) {
        if (change.entity == child)
            childFlags |= dirtyValue(change.dirty);
        if (change.entity == parent)
            parentFlags |= dirtyValue(change.dirty);
    }
    EXPECT_TRUE(hasDirty(childFlags, EntityDirty::Hierarchy));
    EXPECT_TRUE(hasDirty(childFlags, EntityDirty::Transform));
    EXPECT_TRUE(hasDirty(parentFlags, EntityDirty::Destroyed));
}

TEST(SceneChangeJournal, CursorFromAnotherSceneRequiresFullResyncEvenAtTheSameRevision) {
    Scene first;
    Scene second;
    ASSERT_NE(first.changeDomain(), second.changeDomain());

    first.createEntity("First");
    second.createEntity("Second");
    ASSERT_EQ(first.revision(), second.revision());

    SceneChangeCursor cursor;
    const SceneChangeSet firstRead = first.readChanges(cursor);
    ASSERT_EQ(firstRead.status, SceneChangeStatus::Changes);
    cursor = firstRead.cursorAfterApply();

    const SceneChangeSet foreignRead = second.readChanges(cursor);
    EXPECT_EQ(foreignRead.status, SceneChangeStatus::FullResyncRequired);
    EXPECT_TRUE(foreignRead.changes.empty());
    EXPECT_EQ(cursor.domain, first.changeDomain());
}

TEST(SceneChangeJournal, ZeroCapacityAheadCursorAndRetentionBoundaryFailSafely) {
    Scene noRetention(0);
    noRetention.createEntity("NoRetention");
    SceneChangeCursor zeroCursor;
    const SceneChangeSet noJournal = noRetention.readChanges(zeroCursor);
    EXPECT_EQ(noJournal.status, SceneChangeStatus::FullResyncRequired);

    Scene retained(2);
    const EntityId entity = retained.createEntity("First");  // revision 1
    ASSERT_TRUE(retained.setName(entity, "Second"));         // revision 2
    ASSERT_TRUE(retained.setVisible(entity, false));         // revision 3
    SceneChangeCursor boundary{ retained.changeDomain(), 1 };
    const SceneChangeSet readableBoundary = retained.readChanges(boundary);
    ASSERT_EQ(readableBoundary.status, SceneChangeStatus::Changes);
    ASSERT_EQ(readableBoundary.changes.size(), 2u);
    EXPECT_EQ(readableBoundary.changes.front().revision, 2u);
    EXPECT_EQ(readableBoundary.changes.back().revision, 3u);

    SceneChangeCursor ahead{ retained.changeDomain(), retained.revision() + 1 };
    const SceneChangeSet aheadRead = retained.readChanges(ahead);
    EXPECT_EQ(aheadRead.status, SceneChangeStatus::FullResyncRequired);
    EXPECT_TRUE(aheadRead.changes.empty());
}

TEST(SceneHierarchyRevision, ReassigningTheSameParentIsANoOp) {
    Scene scene;
    const EntityId parent = scene.createEntity("Parent");
    const EntityId child = scene.createEntity("Child");
    ASSERT_TRUE(scene.setParent(child, parent));
    const SceneRevision revision = scene.revision();

    EXPECT_TRUE(scene.setParent(child, parent));
    EXPECT_EQ(scene.revision(), revision);
    ASSERT_EQ(scene.childrenOf(parent).size(), 1u);
    EXPECT_EQ(scene.childrenOf(parent).front(), child);
}

TEST(SceneSelectionRevision, SelectSingleOnlyPublishesEntitiesWhoseStateChanges) {
    Scene scene;
    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");
    ASSERT_TRUE(scene.setSelected(first, true));
    const SceneRevision selectedRevision = scene.revision();

    EXPECT_FALSE(scene.selectSingle(first));
    EXPECT_EQ(scene.revision(), selectedRevision);

    EXPECT_TRUE(scene.selectSingle(second));
    EXPECT_EQ(scene.revision(), selectedRevision + 2);
    EXPECT_FALSE(scene.selection(first)->selected);
    EXPECT_TRUE(scene.selection(second)->selected);
}

}  // namespace
}  // namespace mulan::scene
