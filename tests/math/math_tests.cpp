#include <gtest/gtest.h>
#include <mulan/math/math.h>

using namespace mulan::math;

// ============================================================
// 向量
// ============================================================

TEST(VectorTest, Vec2Length) {
    Vec2 v2(3.0, 4.0);
    EXPECT_NEAR(v2.length(), 5.0, 1e-12);
}

TEST(VectorTest, Vec2Normalized) {
    EXPECT_NEAR(Vec2(3, 4).normalized().x, 0.6, 1e-9);
    EXPECT_NEAR(Vec2(3, 4).normalized().y, 0.8, 1e-9);
}

TEST(VectorTest, Vec2NormalizedOr) {
    EXPECT_EQ(Vec2::zero().normalizedOr(Vec2::unitX()), Vec2::unitX());
}

TEST(VectorTest, Vec2Dot) {
    EXPECT_NEAR(Vec2(1.0, 2.0).dot(Vec2(3.0, 4.0)), 11.0, 1e-12);
}

TEST(VectorTest, Vec2Cross) {
    EXPECT_NEAR(Vec2(1.0, 0.0).cross(Vec2(0.0, 1.0)), 1.0, 1e-12);
}

TEST(VectorTest, Vec2MinMaxClamp) {
    EXPECT_EQ(min(Vec2(1.0, 5.0), Vec2(2.0, 3.0)), Vec2(1.0, 3.0));
    EXPECT_EQ(max(Vec2(1.0, 5.0), Vec2(2.0, 3.0)), Vec2(2.0, 5.0));
    EXPECT_EQ(clamp(Vec2(-1.0, 5.0), Vec2(0.0), Vec2(2.0)), Vec2(0.0, 2.0));
}

TEST(VectorTest, Vec3LengthSq) {
    Vec3 v3(1.0, 2.0, 3.0);
    EXPECT_NEAR(v3.lengthSq(), 14.0, 1e-12);
}

TEST(VectorTest, Vec3Cross) {
    EXPECT_EQ(Vec3::unitX().cross(Vec3::unitY()), Vec3::unitZ());
}

TEST(VectorTest, Vec3Dot) {
    EXPECT_NEAR(Vec3(1.0, 2.0, 3.0).dot(Vec3(4.0, 5.0, 6.0)), 32.0, 1e-12);
}

TEST(VectorTest, Vec3Lerp) {
    EXPECT_EQ(Vec3(0.0).lerp(Vec3(10.0), 0.25), Vec3(2.5));
}

TEST(VectorTest, FVec3Conversion) {
    FVec3 fv(1.0f, 2.0f, 3.0f);
    Vec3 converted(fv);
    EXPECT_EQ(converted, Vec3(1.0, 2.0, 3.0));
}

TEST(VectorTest, Vec4FromVec3) {
    Vec3 v3(1.0, 2.0, 3.0);
    Vec4 h(v3, 1.0);
    EXPECT_EQ(h, Vec4(1.0, 2.0, 3.0, 1.0));
    EXPECT_EQ(Vec3(h), v3);
    EXPECT_EQ(h.xyz(), v3);
}

// ============================================================
// 矩阵
// ============================================================

TEST(MatrixTest, Mat2Rotation) {
    Mat2 r2 = Mat2::rotation(kHalfPi);
    EXPECT_NEAR((r2 * Vec2::unitX()).x, 0.0, 1e-12);
    EXPECT_NEAR((r2 * Vec2::unitX()).y, 1.0, 1e-12);
}

TEST(MatrixTest, Mat3Rotation) {
    Mat3 r3 = Mat3::rotation(Vec3(0.0, 0.0, 10.0), kHalfPi);
    EXPECT_NEAR((r3 * Vec3::unitX()).x, 0.0, 1e-9);
    EXPECT_NEAR((r3 * Vec3::unitX()).y, 1.0, 1e-9);
}

TEST(MatrixTest, Mat4Identity) {
    Mat4 identity(1.0);
    Vec4 v(1.0, 2.0, 3.0, 1.0);
    EXPECT_EQ(identity * v, v);
}

TEST(MatrixTest, Mat4Translate) {
    Mat4 translated = Mat4::translate(Vec3(3.0, -2.0, 5.0));
    EXPECT_EQ(Point3(1.0, 2.0, 3.0).transformedBy(translated).asVec(), Vec3(4.0, 0.0, 8.0));
    EXPECT_EQ(Vec3(1.0, 2.0, 3.0).transformedAsDir(translated), Vec3(1.0, 2.0, 3.0));
}

TEST(MatrixTest, Mat4Scale) {
    Mat4 scaled = Mat4::scale(Vec3(2.0, 3.0, 4.0));
    auto sv = Point3(1.0, 2.0, 3.0).transformedBy(scaled).asVec();
    EXPECT_NEAR(sv.x, 2.0, 1e-9);
    EXPECT_NEAR(sv.y, 6.0, 1e-9);
    EXPECT_NEAR(sv.z, 12.0, 1e-9);
}

TEST(MatrixTest, Mat4Inverse) {
    Mat4 combined = Mat4::translate(Vec3(3, -2, 5)) * Mat4::scale(Vec3(2, 3, 4));
    Mat4 inv = combined.inverse();
    auto rv = Point3(2, 4, 6).transformedBy(combined).transformedBy(inv).asVec();
    EXPECT_NEAR(rv.x, 2.0, 1e-9);
    EXPECT_NEAR(rv.y, 4.0, 1e-9);
    EXPECT_NEAR(rv.z, 6.0, 1e-9);
}

TEST(MatrixTest, Mat4ToMat3) {
    Mat4 combined = Mat4::translate(Vec3(3, -2, 5)) * Mat4::scale(Vec3(2, 3, 4));
    Mat3 upper(combined);
    Mat4 lifted(upper);
    auto lv = Vec3(1, 1, 1).transformedAsDir(lifted);
    EXPECT_NEAR(lv.x, 2.0, 1e-9);
    EXPECT_NEAR(lv.y, 3.0, 1e-9);
    EXPECT_NEAR(lv.z, 4.0, 1e-9);
}

TEST(MatrixTest, Mat4DataNotNull) {
    Mat4 m(1.0);
    EXPECT_NE(m.data(), nullptr);
}

// ============================================================
// 四元数与变换
// ============================================================

TEST(QuaternionTest, FromAxisAngle) {
    Quat q = Quat::fromAxisAngle(Vec3::unitZ(), kHalfPi);
    EXPECT_NEAR((q * Vec3::unitX()).x, 0.0, 1e-9);
    EXPECT_NEAR((q * Vec3::unitX()).y, 1.0, 1e-9);
}

TEST(QuaternionTest, ToMat3) {
    Quat q = Quat::fromAxisAngle(Vec3::unitZ(), kHalfPi);
    EXPECT_NEAR((q.normalized().toMat3() * Vec3::unitX()).y, 1.0, 1e-9);
}

TEST(QuaternionTest, ToMat4) {
    Quat q = Quat::fromAxisAngle(Vec3::unitZ(), kHalfPi);
    Vec4 r = q.toMat4() * Vec4(Vec3::unitX(), 0.0);
    EXPECT_NEAR(r.xyz().y, 1.0, 1e-9);
}

TEST(QuaternionTest, FromTwoVectors) {
    Quat fromTo = Quat::fromTwoVectors(Vec3::unitX(), Vec3::unitY());
    EXPECT_NEAR((fromTo * Vec3::unitX()).y, 1.0, 1e-9);
}

TEST(TransformTest, Roundtrip) {
    Transform3 t(Vec3(10, 0, 0), Quat::identity(), Vec3(2, 2, 2));
    Mat4 m = t.toMatrix();
    Mat4 inv = t.toInverseMatrix();
    Vec3 p(1, 2, 3);
    EXPECT_EQ(Point3(p).transformedBy(m).transformedBy(inv).asVec(), p);
}

TEST(TransformTest, Combine) {
    Transform3 parent(Vec3(1, 0, 0), Quat::identity(), Vec3(2, 2, 2));
    Transform3 child(Vec3(0, 3, 0), Quat::identity(), Vec3(1, 1, 1));
    Transform3 combined = parent.combine(child);
    EXPECT_EQ(combined.translation, Vec3(1, 6, 0));
}

// ============================================================
// AABB / Sphere / Plane
// ============================================================

TEST(BoundsAndPlanesTest, AABBEmpty) {
    AABB3 box = AABB3::empty();
    EXPECT_TRUE(box.isEmpty());
}

TEST(BoundsAndPlanesTest, AABBExpand) {
    AABB3 box = AABB3::empty();
    box.expand(Point3(-1, -2, -3));
    box.expand(Point3(3, 2, 1));
    EXPECT_FALSE(box.isEmpty());
    EXPECT_EQ(box.center().asVec(), Vec3(1, 0, -1));
    EXPECT_EQ(box.extents(), Vec3(2, 2, 2));
}

TEST(BoundsAndPlanesTest, AABBContains) {
    AABB3 box = AABB3::empty();
    box.expand(Point3(-1, -2, -3));
    box.expand(Point3(3, 2, 1));
    EXPECT_TRUE(box.contains(Point3(0, 0, 0)));
    EXPECT_FALSE(box.contains(Point3(4, 0, 0)));
}

TEST(BoundsAndPlanesTest, AABBTransformed) {
    AABB3 box = AABB3::empty();
    box.expand(Point3(-1, -2, -3));
    box.expand(Point3(3, 2, 1));
    AABB3 moved = box.transformed(Mat4::translate(Vec3(10, 0, 0)));
    EXPECT_TRUE(moved.contains(Point3(10, 0, 0)));
    EXPECT_FALSE(moved.contains(Point3(0, 0, 0)));
}

TEST(BoundsAndPlanesTest, SphereBasic) {
    Sphere3 sphere = Sphere3::fromAABB(AABB3::fromCenterExtents(Point3(0), Vec3(1)));
    EXPECT_TRUE(sphere.isValid());
    EXPECT_TRUE(sphere.contains(Point3(1, 0, 0)));
    EXPECT_TRUE(sphere.intersects(AABB3::fromCenterExtents(Point3(1.9, 0, 0), Vec3(0.25))));
}

TEST(BoundsAndPlanesTest, PlaneSignedDistance) {
    Plane3 plane = Plane3::fromPointNormal(Point3(0, 0, 2), Vec3(0, 0, 10));
    EXPECT_NEAR(plane.signedDistance(Point3(0, 0, 5)), 3.0, 1e-12);
    EXPECT_EQ(plane.project(Point3(1, 2, 5)).asVec(), Vec3(1, 2, 2));
}

// ============================================================
// 求交
// ============================================================

TEST(IntersectionTest, RayAABB) {
    AABB3 box = AABB3::fromCenterExtents(Point3(0), Vec3(1));
    Ray3 ray(Point3(-3, 0, 0), Vec3::unitX());
    Hit3 hit = intersect(ray, box);
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 2.0, 1e-12);
    EXPECT_EQ(hit.point.asVec(), Vec3(-1, 0, 0));
}

TEST(IntersectionTest, RaySphere) {
    Ray3 ray(Point3(-3, 0, 0), Vec3::unitX());
    Sphere3 sphere(Point3(0), 1.0);
    Hit3 hit = intersect(ray, sphere);
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 2.0, 1e-12);
}

TEST(IntersectionTest, RayPlane) {
    Ray3 ray(Point3(-3, 0, 0), Vec3::unitX());
    Plane3 plane = Plane3::fromPointNormal(Point3(0), Vec3::unitX());
    Hit3 hit = intersect(ray, plane);
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(hit.t, 3.0, 1e-12);
}

TEST(IntersectionTest, RayTriangle) {
    Vec3 bary;
    Hit3 hit = intersect(Ray3(Point3(0.25, 0.25, -1), Vec3::unitZ()), Point3(0, 0, 0), Point3(1, 0, 0), Point3(0, 1, 0),
                         &bary);
    EXPECT_TRUE(hit.hit);
    EXPECT_NEAR(bary.x, 0.5, 1e-9);
    EXPECT_NEAR(bary.y, 0.25, 1e-9);
    EXPECT_NEAR(bary.z, 0.25, 1e-9);
}

TEST(IntersectionTest, Segment2DSegment2D) {
    double sa = -1.0, sb = -1.0;
    EXPECT_TRUE(intersect(Segment2(Point2(0, 0), Point2(1, 1)), Segment2(Point2(0, 1), Point2(1, 0)), &sa, &sb));
    EXPECT_NEAR(sa, 0.5, 1e-12);
    EXPECT_NEAR(sb, 0.5, 1e-12);
}

TEST(IntersectionTest, PointSegmentDistance) {
    EXPECT_NEAR(distance(Point3(2, 0, 0), Segment3(Point3(0), Point3(1, 0, 0))), 1.0, 1e-12);
}

// ============================================================
// 视锥体裁剪
// ============================================================

TEST(FrustumTest, Contains) {
    Frustum3 clip = Frustum3::fromViewProjection(Mat4(1.0));
    EXPECT_TRUE(clip.contains(Point3(0)));
    EXPECT_FALSE(clip.contains(Point3(2, 0, 0)));
}

TEST(FrustumTest, IntersectsAABB) {
    Frustum3 clip = Frustum3::fromViewProjection(Mat4(1.0));
    EXPECT_TRUE(clip.intersects(AABB3::fromCenterExtents(Point3(0), Vec3(0.5))));
    EXPECT_FALSE(clip.intersects(AABB3::fromCenterExtents(Point3(3, 0, 0), Vec3(0.5))));
}

TEST(FrustumTest, IntersectsSphere) {
    Frustum3 clip = Frustum3::fromViewProjection(Mat4(1.0));
    EXPECT_TRUE(clip.intersects(Sphere3(Point3(0), 0.5)));
    EXPECT_FALSE(clip.intersects(Sphere3(Point3(3, 0, 0), 0.5)));
}
