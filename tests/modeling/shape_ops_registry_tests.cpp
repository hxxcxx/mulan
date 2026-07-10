#include <gtest/gtest.h>

#include <mulan/modeling/core/shape_ops.h>

#include <algorithm>
#include <memory>
#include <string>

namespace mulan::modeling {
namespace {

class StubShapeOps final : public IShapeOps {
public:
    explicit StubShapeOps(int id) : id_(id) {}

    int id() const { return id_; }

    core::Result<Shape> extrude(const ExtrudeParams&) override { return Shape{}; }

    core::Result<Shape> boolean(const Shape&, const Shape&, BooleanOp) override { return Shape{}; }

private:
    int id_ = 0;
};

TEST(ShapeOpsRegistryTest, SelectsNamedBackendIndependentlyOfRegistrationOrder) {
    auto& registry = ShapeOpsRegistry::instance();

    auto truck = std::make_unique<StubShapeOps>(2);
    StubShapeOps* truckPtr = truck.get();
    registry.registerOps("truck-test", std::move(truck));

    auto occt = std::make_unique<StubShapeOps>(1);
    StubShapeOps* occtPtr = occt.get();
    registry.registerOps("occt-test", std::move(occt));

    registry.selectBackend("OCCT-TEST");
    EXPECT_EQ(registry.selectedBackend(), "occt-test");
    EXPECT_EQ(registry.ops(), occtPtr);

    registry.selectBackend("Truck-Test");
    EXPECT_EQ(registry.selectedBackend(), "truck-test");
    EXPECT_EQ(registry.ops(), truckPtr);

    const auto available = registry.availableBackends();
    EXPECT_NE(std::find(available.begin(), available.end(), "occt-test"), available.end());
    EXPECT_NE(std::find(available.begin(), available.end(), "truck-test"), available.end());

    registry.selectBackend("missing-test");
    EXPECT_EQ(registry.ops(), nullptr);
}

}  // namespace
}  // namespace mulan::modeling
