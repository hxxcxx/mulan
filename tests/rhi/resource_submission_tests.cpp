#include <gtest/gtest.h>

#include <mulan/rhi/resource.h>

namespace mulan::engine {
namespace {

class TestResource final : public RHITrackedResource {};

TEST(RHITrackedResourceTest, KeepsLatestTokenPerQueue) {
    TestResource resource;

    resource.markUsed(SubmissionToken{ 7, QueueType::Graphics, 12 });
    resource.markUsed(SubmissionToken{ 7, QueueType::Graphics, 9 });
    resource.markUsed(SubmissionToken{ 7, QueueType::Compute, 4 });

    EXPECT_EQ(resource.lastUseToken(), (SubmissionToken{ 7, QueueType::Graphics, 12 }));
    EXPECT_EQ(resource.lastUseToken(QueueType::Compute), (SubmissionToken{ 7, QueueType::Compute, 4 }));
    EXPECT_FALSE(resource.lastUseToken(QueueType::Copy));
}

TEST(RHITrackedResourceTest, IgnoresEmptyToken) {
    TestResource resource;

    resource.markUsed({});

    EXPECT_FALSE(resource.lastUseToken());
}

}  // namespace
}  // namespace mulan::engine
