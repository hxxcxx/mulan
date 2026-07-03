#include <mulan/geo/geo.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using namespace mulan::geo;

int g_failures = 0;

void fail(const char* expr, const char* file, int line, const std::string& message = {}) {
    ++g_failures;
    std::cerr << file << ':' << line << ": CHECK failed: " << expr;
    if (!message.empty()) {
        std::cerr << " (" << message << ')';
    }
    std::cerr << '\n';
}

#define CHECK(expr) \
    do { \
        if (!(expr)) fail(#expr, __FILE__, __LINE__); \
    } while (false)

#define CHECK_NEAR(actual, expected, eps) \
    do { \
        const double actual_value = static_cast<double>(actual); \
        const double expected_value = static_cast<double>(expected); \
        const double eps_value = static_cast<double>(eps); \
        if (std::abs(actual_value - expected_value) > eps_value) { \
            fail(#actual " ~= " #expected, __FILE__, __LINE__, \
                 "actual=" + std::to_string(actual_value) + \
                 ", expected=" + std::to_string(expected_value)); \
        } \
    } while (false)

void checkVec2Near(const Vec2& actual, const Vec2& expected, double eps = 1e-9) {
    CHECK_NEAR(actual.x, expected.x, eps);
    CHECK_NEAR(actual.y, expected.y, eps);
}

void checkVec3Near(const Vec3& actual, const Vec3& expected, double eps = 1e-9) {
    CHECK_NEAR(actual.x, expected.x, eps);
    CHECK_NEAR(actual.y, expected.y, eps);
    CHECK_NEAR(actual.z, expected.z, eps);
}

void checkVec4Near(const Vec4& actual, const Vec4& expected, double eps = 1e-9) {
    CHECK_NEAR(actual.x, expected.x, eps);
    CHECK_NEAR(actual.y, expected.y, eps);
    CHECK_NEAR(actual.z, expected.z, eps);
    CHECK_NEAR(actual.w, expected.w, eps);
}

void testVectors() {
    Vec2 v2(3.0, 4.0);
    CHECK_NEAR(v2.length(), 5.0, 1e-12);
    checkVec2Near(v2.normalized(), Vec2(0.6, 0.8));
    checkVec2Near(Vec2::zero().normalizedOr(Vec2::unitX()), Vec2::unitX());
    CHECK_NEAR(dot(Vec2(1.0, 2.0), Vec2(3.0, 4.0)), 11.0, 1e-12);
    CHECK_NEAR(cross(Vec2(1.0, 0.0), Vec2(0.0, 1.0)), 1.0, 1e-12);
    checkVec2Near(min(Vec2(1.0, 5.0), Vec2(2.0, 3.0)), Vec2(1.0, 3.0));
    checkVec2Near(max(Vec2(1.0, 5.0), Vec2(2.0, 3.0)), Vec2(2.0, 5.0));
    checkVec2Near(clamp(Vec2(-1.0, 5.0), Vec2(0.0), Vec2(2.0)), Vec2(0.0, 2.0));

    Vec3 v3(1.0, 2.0, 3.0);
    CHECK_NEAR(v3.lengthSq(), 14.0, 1e-12);
    checkVec3Near(cross(Vec3::unitX(), Vec3::unitY()), Vec3::unitZ());
    CHECK_NEAR(dot(v3, Vec3(4.0, 5.0, 6.0)), 32.0, 1e-12);
    checkVec3Near(lerp(Vec3(0.0), Vec3(10.0), 0.25), Vec3(2.5));
    checkVec3Near(Vec3::zero().normalizedOr(Vec3::unitY()), Vec3::unitY());

    FVec3 fv(1.0f, 2.0f, 3.0f);
    Vec3 converted(fv);
    checkVec3Near(converted, Vec3(1.0, 2.0, 3.0));

    Vec4 h(v3, 1.0);
    checkVec4Near(h, Vec4(1.0, 2.0, 3.0, 1.0));
    checkVec3Near(Vec3(h), v3);
    checkVec3Near(h.xyz(), v3);
}

void testMatrices() {
    Mat2 r2 = Mat2::rotation(kHalfPi);
    checkVec2Near(r2 * Vec2::unitX(), Vec2(0.0, 1.0));

    Mat3 r3 = Mat3::rotation(Vec3(0.0, 0.0, 10.0), kHalfPi);
    checkVec3Near(r3 * Vec3::unitX(), Vec3::unitY());

    Mat4 identity(1.0);
    checkVec4Near(identity * Vec4(1.0, 2.0, 3.0, 1.0), Vec4(1.0, 2.0, 3.0, 1.0));

    Mat4 translated = Mat4::translate(Vec3(3.0, -2.0, 5.0));
    checkVec3Near(transformPoint(translated, Vec3(1.0, 2.0, 3.0)), Vec3(4.0, 0.0, 8.0));
    checkVec3Near(transformDir(translated, Vec3(1.0, 2.0, 3.0)), Vec3(1.0, 2.0, 3.0));

    Mat4 scaled = Mat4::scale(Vec3(2.0, 3.0, 4.0));
    checkVec3Near(transformPoint(scaled, Vec3(1.0, 2.0, 3.0)), Vec3(2.0, 6.0, 12.0));

    Mat4 combined = translated * scaled;
    Mat4 inv = inverse(combined);
    checkVec3Near(transformPoint(inv, transformPoint(combined, Vec3(2.0, 4.0, 6.0))), Vec3(2.0, 4.0, 6.0));

    Mat3 upper(combined);
    Mat4 lifted(upper);
    checkVec3Near(transformDir(lifted, Vec3(1.0, 1.0, 1.0)), Vec3(2.0, 3.0, 4.0));
    CHECK(value_ptr(combined) != nullptr);
}

void testQuaternionsAndTransforms() {
    Quat q = angleAxis(kHalfPi, Vec3::unitZ());
    checkVec3Near(q * Vec3::unitX(), Vec3::unitY());
    checkVec3Near(normalize(q).toMat3() * Vec3::unitX(), Vec3::unitY());
    checkVec3Near((mat4_cast(q) * Vec4(Vec3::unitX(), 0.0)).xyz(), Vec3::unitY());

    Quat fromTo = Quat::fromTwoVectors(Vec3::unitX(), Vec3::unitY());
    checkVec3Near(fromTo * Vec3::unitX(), Vec3::unitY());

    Transform3 t(Vec3(10.0, 0.0, 0.0), Quat::identity(), Vec3(2.0, 2.0, 2.0));
    Mat4 m = t.toMatrix();
    Mat4 inv = t.toInverseMatrix();
    Vec3 p(1.0, 2.0, 3.0);
    checkVec3Near(transformPoint(inv, transformPoint(m, p)), p);

    Transform3 parent(Vec3(1.0, 0.0, 0.0), Quat::identity(), Vec3(2.0, 2.0, 2.0));
    Transform3 child(Vec3(0.0, 3.0, 0.0), Quat::identity(), Vec3(1.0, 1.0, 1.0));
    Transform3 combined = parent.combine(child);
    checkVec3Near(combined.translation, Vec3(1.0, 6.0, 0.0));
}

void testBoundsAndPlanes() {
    AABB3 box = AABB3::empty();
    CHECK(box.isEmpty());
    box.expand(Vec3(-1.0, -2.0, -3.0));
    box.expand(Vec3(3.0, 2.0, 1.0));
    CHECK(!box.isEmpty());
    checkVec3Near(box.center(), Vec3(1.0, 0.0, -1.0));
    checkVec3Near(box.extents(), Vec3(2.0, 2.0, 2.0));
    CHECK(box.contains(Vec3(0.0, 0.0, 0.0)));
    CHECK(!box.contains(Vec3(4.0, 0.0, 0.0)));

    AABB3 moved = box.transformed(Mat4::translate(Vec3(10.0, 0.0, 0.0)));
    CHECK(moved.contains(Vec3(10.0, 0.0, 0.0)));
    CHECK(!moved.contains(Vec3(0.0, 0.0, 0.0)));

    Sphere3 sphere = Sphere3::fromAABB(AABB3::fromCenterExtents(Vec3(0.0), Vec3(1.0)));
    CHECK(sphere.isValid());
    CHECK(sphere.contains(Vec3(1.0, 0.0, 0.0)));
    CHECK(sphere.intersects(AABB3::fromCenterExtents(Vec3(1.9, 0.0, 0.0), Vec3(0.25))));

    Plane3 plane = Plane3::fromPointNormal(Vec3(0.0, 0.0, 2.0), Vec3(0.0, 0.0, 10.0));
    CHECK_NEAR(plane.signedDistance(Vec3(0.0, 0.0, 5.0)), 3.0, 1e-12);
    checkVec3Near(plane.project(Vec3(1.0, 2.0, 5.0)), Vec3(1.0, 2.0, 2.0));
}

void testIntersections() {
    AABB3 box = AABB3::fromCenterExtents(Vec3(0.0), Vec3(1.0));
    Ray3 ray(Vec3(-3.0, 0.0, 0.0), Vec3::unitX());
    Hit3 hit = intersect(ray, box);
    CHECK(hit.hit);
    CHECK_NEAR(hit.t, 2.0, 1e-12);
    checkVec3Near(hit.point, Vec3(-1.0, 0.0, 0.0));

    Sphere3 sphere(Vec3(0.0), 1.0);
    hit = intersect(ray, sphere);
    CHECK(hit.hit);
    CHECK_NEAR(hit.t, 2.0, 1e-12);

    Plane3 plane = Plane3::fromPointNormal(Vec3(0.0), Vec3::unitX());
    hit = intersect(ray, plane);
    CHECK(hit.hit);
    CHECK_NEAR(hit.t, 3.0, 1e-12);

    Vec3 bary;
    hit = intersect(Ray3(Vec3(0.25, 0.25, -1.0), Vec3::unitZ()),
                    Vec3(0.0, 0.0, 0.0),
                    Vec3(1.0, 0.0, 0.0),
                    Vec3(0.0, 1.0, 0.0),
                    &bary);
    CHECK(hit.hit);
    checkVec3Near(bary, Vec3(0.5, 0.25, 0.25));

    double sa = -1.0;
    double sb = -1.0;
    CHECK(intersect(Segment2(Vec2(0.0, 0.0), Vec2(1.0, 1.0)),
                    Segment2(Vec2(0.0, 1.0), Vec2(1.0, 0.0)),
                    &sa, &sb));
    CHECK_NEAR(sa, 0.5, 1e-12);
    CHECK_NEAR(sb, 0.5, 1e-12);

    CHECK_NEAR(distance(Vec3(2.0, 0.0, 0.0), Segment3(Vec3(0.0), Vec3(1.0, 0.0, 0.0))), 1.0, 1e-12);
}

void testFrustum() {
    Frustum3 clip = Frustum3::fromViewProjection(Mat4(1.0));
    CHECK(clip.contains(Vec3(0.0)));
    CHECK(!clip.contains(Vec3(2.0, 0.0, 0.0)));
    CHECK(clip.intersects(AABB3::fromCenterExtents(Vec3(0.0), Vec3(0.5))));
    CHECK(!clip.intersects(AABB3::fromCenterExtents(Vec3(3.0, 0.0, 0.0), Vec3(0.5))));
    CHECK(clip.intersects(Sphere3(Vec3(0.0), 0.5)));
    CHECK(!clip.intersects(Sphere3(Vec3(3.0, 0.0, 0.0), 0.5)));
}

} // namespace

int main() {
    testVectors();
    testMatrices();
    testQuaternionsAndTransforms();
    testBoundsAndPlanes();
    testIntersections();
    testFrustum();

    if (g_failures != 0) {
        std::cerr << g_failures << " geo test failure(s)\n";
        return EXIT_FAILURE;
    }

    std::cout << "Geo tests passed\n";
    return EXIT_SUCCESS;
}
