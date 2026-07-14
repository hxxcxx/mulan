#include <gtest/gtest.h>

#include <mulan/rhi/transient_uniform_allocator.h>

namespace mulan::engine {
namespace {

TEST(TransientUniformAllocatorTest, RequiresARecording) {
    TransientUniformAllocator allocator({ 1024, 256, 512 });

    const auto result = allocator.allocate(128);

    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), UniformAllocationError::RecordingNotStarted);
}

TEST(TransientUniformAllocatorTest, RejectsAllocationsAfterRecordingEnds) {
    TransientUniformAllocator allocator({ 1024, 256, 512 });
    const uint64_t generation = allocator.beginRecording();
    ASSERT_TRUE(allocator.allocate(128));

    allocator.endRecording();

    EXPECT_FALSE(allocator.isRecording());
    EXPECT_FALSE(allocator.owns(generation));
    const auto result = allocator.allocate(128);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), UniformAllocationError::RecordingNotStarted);
}

TEST(TransientUniformAllocatorTest, AlignsAllocationsWithinAPage) {
    TransientUniformAllocator allocator({ 1024, 256, 512 });
    const uint64_t generation = allocator.beginRecording();

    const auto first = allocator.allocate(64);
    const auto second = allocator.allocate(128);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->offset, 0u);
    EXPECT_EQ(first->reservedSize, 256u);
    EXPECT_EQ(second->offset, 256u);
    EXPECT_EQ(second->reservedSize, 256u);
    EXPECT_EQ(first->recordingGeneration, generation);
    EXPECT_TRUE(allocator.owns(generation));
}

TEST(TransientUniformAllocatorTest, RollsOverToAnotherPage) {
    TransientUniformAllocator allocator({ 512, 256, 512 });
    allocator.beginRecording();

    ASSERT_TRUE(allocator.allocate(256));
    ASSERT_TRUE(allocator.allocate(256));
    const auto third = allocator.allocate(1);

    ASSERT_TRUE(third);
    EXPECT_EQ(third->pageIndex, 1u);
    EXPECT_EQ(third->offset, 0u);
    EXPECT_EQ(allocator.stats().pagesUsed, 2u);
}

TEST(TransientUniformAllocatorTest, ResetsCurrentStatsButPreservesPeaks) {
    TransientUniformAllocator allocator({ 512, 256, 512 });
    allocator.beginRecording();
    ASSERT_TRUE(allocator.allocate(256));
    ASSERT_TRUE(allocator.allocate(256));
    ASSERT_TRUE(allocator.allocate(256));
    const uint64_t oldGeneration = allocator.recordingGeneration();

    const uint64_t newGeneration = allocator.beginRecording();

    EXPECT_NE(newGeneration, oldGeneration);
    EXPECT_FALSE(allocator.owns(oldGeneration));
    EXPECT_EQ(allocator.stats().allocationCount, 0u);
    EXPECT_EQ(allocator.stats().pagesUsed, 0u);
    EXPECT_EQ(allocator.stats().peakPagesUsed, 2u);
    EXPECT_EQ(allocator.stats().peakReservedBytes, 768u);
}

TEST(TransientUniformAllocatorTest, RejectsInvalidAndOversizedRequests) {
    TransientUniformAllocator invalid({ 0, 256, 512 });
    invalid.beginRecording();
    EXPECT_EQ(invalid.allocate(1).error(), UniformAllocationError::InvalidConfiguration);

    TransientUniformAllocator allocator({ 1024, 256, 512 });
    allocator.beginRecording();
    EXPECT_EQ(allocator.allocate(0).error(), UniformAllocationError::EmptyAllocation);
    EXPECT_EQ(allocator.allocate(513).error(), UniformAllocationError::AllocationTooLarge);
}

TEST(TransientUniformAllocatorTest, SupportsNonPowerOfTwoAlignment) {
    TransientUniformAllocator allocator({ 1024, 192, 512 });
    allocator.beginRecording();

    const auto first = allocator.allocate(1);
    const auto second = allocator.allocate(1);

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->reservedSize, 192u);
    EXPECT_EQ(second->offset, 192u);
}

}  // namespace
}  // namespace mulan::engine
